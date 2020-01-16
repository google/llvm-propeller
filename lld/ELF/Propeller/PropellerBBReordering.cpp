//===- PropellerBBReordering.cpp  -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "PropellerBBReordering.h"

#include "PropellerConfig.h"
#include "PropellerCFG.h"
#include "PropellerNodeChain.h"
#include "PropellerChainClustering.h"
#include "PropellerNodeChainAssembly.h"
#include "PropellerNodeChainBuilder.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <numeric>
#include <vector>

using llvm::DenseMap;
using llvm::DenseSet;

namespace lld {
namespace propeller {

void PropellerBBReordering::printStats() {

  DenseMap<CFGNode*, uint64_t> nodeAddressMap;
  llvm::StringMap<unsigned> functionPartitions;
  uint64_t currentAddress = 0;
  ControlFlowGraph* currentCFG = nullptr;
  for(CFGNode* n: HotOrder){
    if (currentCFG != n->CFG){
      currentCFG = n->CFG;
      functionPartitions[currentCFG->Name]++;
    }
    nodeAddressMap[n]=currentAddress;
    currentAddress += n->ShSize;
  }

  for(auto &elem: functionPartitions){
    fprintf(stderr, "FUNCTION PARTITIONS: %s,%u\n", elem.first().str().c_str(), elem.second);
  }

  std::vector<uint64_t> distances({0,128, 640, 1028, 4096, 65536, 2 << 20, std::numeric_limits<uint64_t>::max()});
  std::map<uint64_t, uint64_t> histogram;
  llvm::StringMap<double> extTSPScoreMap;
  for(CFGNode* n: HotOrder) {
    auto scoreEntry = extTSPScoreMap.try_emplace(n->CFG->Name, 0).first;
    n->forEachOutEdgeRef([&nodeAddressMap, &distances, &histogram, &scoreEntry](CFGEdge& edge){
      if (!edge.Weight)
        return;
      if (edge.isReturn())
        return;
      if (nodeAddressMap.find(edge.Src)==nodeAddressMap.end() || nodeAddressMap.find(edge.Sink)==nodeAddressMap.end())
        return;
      uint64_t srcOffset = nodeAddressMap[edge.Src];
      uint64_t sinkOffset = nodeAddressMap[edge.Sink];
      bool edgeForward = srcOffset + edge.Src->ShSize <= sinkOffset;
      uint64_t srcSinkDistance = edgeForward ? sinkOffset - srcOffset - edge.Src->ShSize: srcOffset - sinkOffset + edge.Src->ShSize;

      if (edge.Type == CFGEdge::EdgeType::INTRA_FUNC || edge.Type == CFGEdge::EdgeType::INTRA_DYNA)
        scoreEntry->second += getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);

      auto res = std::lower_bound(distances.begin(), distances.end(), srcSinkDistance);
      histogram[*res] += edge.Weight;
    });
  }

  for(auto& elem: extTSPScoreMap)
    fprintf(stderr, "Ext TSP Score: %s %.6f\n", elem.first().str().c_str(), elem.second);
  fprintf(stderr, "DISTANCE HISTOGRAM: ");
  for(auto elem: histogram)
    fprintf(stderr, "\t[%lu -> %lu]", elem.first, elem.second);
  fprintf(stderr, "\n");
}

void PropellerBBReordering::doSplitOrder(std::list<StringRef> &symbolList,
                    std::list<StringRef>::iterator hotPlaceHolder,
                    std::list<StringRef>::iterator coldPlaceHolder) {
    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    prop->forEachCfgRef([this](ControlFlowGraph &cfg) {
      if (cfg.isHot()) {
        HotCFGs.push_back(&cfg);
        if (propellerConfig.optPrintStats) {
          unsigned hotBBs = 0;
          unsigned allBBs = 0;
          cfg.forEachNodeRef([&hotBBs, &allBBs](CFGNode &node) {
            if (node.Freq)
              hotBBs++;
            allBBs++;
          });
          fprintf(stderr, "HISTOGRAM: %s,%u,%u\n", cfg.Name.str().c_str(), allBBs, hotBBs);
        }
      } else
        ColdCFGs.push_back(&cfg);
    });


    if (propellerConfig.optReorderIP)
      CC.reset(new CallChainClustering());
    else if (propellerConfig.optReorderFuncs)
      CC.reset(new CallChainClustering());
    else
      CC.reset(new NoOrdering());

    if (propellerConfig.optReorderIP)
      NodeChainBuilder(HotCFGs).doOrder(CC);
    else if (propellerConfig.optReorderBlocks) {
      for (ControlFlowGraph *cfg : HotCFGs)
        NodeChainBuilder(cfg).doOrder(CC);
    } else {
      for (ControlFlowGraph *cfg : HotCFGs)
        CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));
    }
    for (ControlFlowGraph *cfg : ColdCFGs)
      CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));

    CC->doOrder(HotOrder, ColdOrder);

    for (CFGNode *n : HotOrder)
      symbolList.insert(hotPlaceHolder, n->ShName);

    for (CFGNode *n : ColdOrder)
      symbolList.insert(coldPlaceHolder, n->ShName);

    std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
    warn(
        "[Propeller]: BB reordering took: " +
        Twine(std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                  .count()));

    if (propellerConfig.optPrintStats)
      printStats();
}

} // namespace propeller
} // namespace lld
