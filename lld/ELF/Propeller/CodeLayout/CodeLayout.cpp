//===- CodeLayout.cpp  ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file is part of the Propeller infrastructure for doing code layout
// optimization and implements the entry point of code layout optimization
// (doSplitOrder).
//===----------------------------------------------------------------------===//
#include "CodeLayout.h"

#include "NodeChain.h"
#include "NodeChainBuilder.h"
#include "NodeChainClustering.h"
#include "Propeller.h"
#include "PropellerCFG.h"
#include "PropellerConfig.h"

#include "llvm/ADT/DenseMap.h"

#include <vector>

using llvm::DenseMap;

namespace lld {
namespace propeller {
extern double getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
                                 uint64_t);

// This function iterates over the CFGs included in the Propeller profile and
// adds them to cold and hot cfg lists. Then it appropriately performs basic
// block reordering by calling NodeChainBuilder.doOrder() either on all CFGs (if
// -propeller-opt=reorder-ip) or individually on every CFG. After creating all
// the node chains, it hands the basic block chains to a ChainClustering
// instance for further rerodering.
void CodeLayout::doSplitOrder(std::list<StringRef> &symbolList,
                              std::list<StringRef>::iterator hotPlaceHolder,
                              std::list<StringRef>::iterator coldPlaceHolder) {
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();

  // Populate the hot and cold cfg lists by iterating over the CFGs in the
  // propeller profile.
  prop->forEachCfgRef([this](ControlFlowGraph &cfg) {
    if (cfg.isHot()) {
      HotCFGs.push_back(&cfg);
      if (propellerConfig.optPrintStats) {
        // Dump the number of basic blocks and hot basic blocks for every
        // function
        unsigned hotBBs = 0;
        unsigned allBBs = 0;
        cfg.forEachNodeRef([&hotBBs, &allBBs](CFGNode &node) {
          if (node.Freq)
            hotBBs++;
          allBBs++;
        });
        fprintf(stderr, "HISTOGRAM: %s,%u,%u\n", cfg.Name.str().c_str(), allBBs,
                hotBBs);
      }
    } else
      ColdCFGs.push_back(&cfg);
  });

  if (propellerConfig.optReorderIP || propellerConfig.optReorderFuncs)
    CC.reset(new CallChainClustering());
  else {
    // If function ordering is disabled, we want to conform the the initial
    // ordering of functions in both the hot and the cold layout.
    CC.reset(new NoOrdering());
  }

  if (propellerConfig.optReorderIP) {
    // If -propeller-opt=reorder-ip we want to run basic block reordering on all
    // the basic blocks of the hot CFGs.
    NodeChainBuilder(HotCFGs).doOrder(CC);
  } else if (propellerConfig.optReorderBlocks) {
    // Otherwise we apply reordering on every CFG separately
    for (ControlFlowGraph *cfg : HotCFGs)
      NodeChainBuilder(cfg).doOrder(CC);
  } else {
    // If reordering is not desired, we create changes according to the initial
    // order in the CFG.
    for (ControlFlowGraph *cfg : HotCFGs)
      CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));
  }

  // The order for cold cfgs remains unchanged.
  for (ControlFlowGraph *cfg : ColdCFGs)
    CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));

  // After building all the chains, let the chain clustering algorithm perform
  // the final reordering and populate the hot and cold cfg node orders.
  CC->doOrder(HotOrder, ColdOrder);

  // Transfter the order to the symbol list.
  for (CFGNode *n : HotOrder)
    symbolList.insert(hotPlaceHolder, n->ShName);

  for (CFGNode *n : ColdOrder)
    symbolList.insert(coldPlaceHolder, n->ShName);

  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  warn("[Propeller]: BB reordering took: " +
       Twine(std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                 .count()));

  if (propellerConfig.optPrintStats)
    printStats();
}

// Prints statistics of the computed layout, including the number of partitions
// for each function across the code layout, the edge count for each distance
// level, and the ExtTSP score achieved for each function.
void CodeLayout::printStats() {

  DenseMap<CFGNode *, uint64_t> nodeAddressMap;
  llvm::StringMap<unsigned> functionPartitions;
  uint64_t currentAddress = 0;
  ControlFlowGraph *currentCFG = nullptr;
  for (CFGNode *n : HotOrder) {
    if (currentCFG != n->CFG) {
      currentCFG = n->CFG;
      functionPartitions[currentCFG->Name]++;
    }
    nodeAddressMap[n] = currentAddress;
    currentAddress += n->ShSize;
  }

  for (auto &elem : functionPartitions)
    fprintf(stderr, "FUNCTION PARTITIONS: %s,%u\n", elem.first().str().c_str(),
            elem.second);

  std::vector<uint64_t> distances({0, 128, 640, 1028, 4096, 65536, 2 << 20,
                                   std::numeric_limits<uint64_t>::max()});
  std::map<uint64_t, uint64_t> histogram;
  llvm::StringMap<double> extTSPScoreMap;
  for (CFGNode *n : HotOrder) {
    auto scoreEntry = extTSPScoreMap.try_emplace(n->CFG->Name, 0).first;
    n->forEachOutEdgeRef([&nodeAddressMap, &distances, &histogram,
                          &scoreEntry](CFGEdge &edge) {
      if (!edge.Weight || edge.isReturn())
        return;
      if (nodeAddressMap.find(edge.Src) == nodeAddressMap.end() ||
          nodeAddressMap.find(edge.Sink) == nodeAddressMap.end()) {
        warn("Found a hot edge whose source and sink do not show up in the "
             "layout!");
        return;
      }
      uint64_t srcOffset = nodeAddressMap[edge.Src];
      uint64_t sinkOffset = nodeAddressMap[edge.Sink];
      bool edgeForward = srcOffset + edge.Src->ShSize <= sinkOffset;
      uint64_t srcSinkDistance =
          edgeForward ? sinkOffset - srcOffset - edge.Src->ShSize
                      : srcOffset - sinkOffset + edge.Src->ShSize;

      if (edge.Type == CFGEdge::EdgeType::INTRA_FUNC ||
          edge.Type == CFGEdge::EdgeType::INTRA_DYNA)
        scoreEntry->second +=
            getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);

      auto res =
          std::lower_bound(distances.begin(), distances.end(), srcSinkDistance);
      histogram[*res] += edge.Weight;
    });
  }

  for (auto &elem : extTSPScoreMap)
    fprintf(stderr, "Ext TSP Score: %s %.6f\n", elem.first().str().c_str(),
            elem.second);
  fprintf(stderr, "DISTANCE HISTOGRAM: ");
  for (auto elem : histogram)
    fprintf(stderr, "\t[%lu -> %lu]", elem.first, elem.second);
  fprintf(stderr, "\n");
}

} // namespace propeller
} // namespace lld
