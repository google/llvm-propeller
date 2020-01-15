//===- PropellerBBReordering.h  -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_BB_REORDERING_H
#define LLD_ELF_PROPELLER_BB_REORDERING_H

#include "PropellerCFG.h"
#include "PropellerConfig.h"
#include "PropellerNodeChain.h"
#include "PropellerChainClustering.h"
#include "PropellerNodeChainAssembly.h"
#include "PropellerNodeChainBuilder.h"

#include "lld/Common/LLVM.h"
//#include <iostream>

#include <chrono>
#include <list>
#include <vector>

namespace lld {
namespace propeller {

class PropellerBBReordering {
private:
  std::vector<ControlFlowGraph *> HotCFGs, ColdCFGs;
  std::vector<CFGNode *> HotOrder, ColdOrder;
  std::unique_ptr<ChainClustering> CC;

public:
  PropellerBBReordering() {
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
          fprintf(stderr, "HISTOGRAM: %s,%u,%u\n", cfg.Name.str().c_str(),
                  allBBs, hotBBs);
        }
      } else {
        fprintf(stderr, "CFG IS COLD: %s\n", cfg.Name.str().c_str());
        ColdCFGs.push_back(&cfg);
      }
    });
  }

  void doSplitOrder(std::list<StringRef> &symbolList,
                    std::list<StringRef>::iterator hotPlaceHolder,
                    std::list<StringRef>::iterator coldPlaceHolder) {

    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

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

  void printStats();
};

} // namespace propeller
} // namespace lld

#endif
