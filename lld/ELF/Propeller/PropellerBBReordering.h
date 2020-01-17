//===- PropellerBBReordering.h  -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_BB_REORDERING_H
#define LLD_ELF_PROPELLER_BB_REORDERING_H

#include "PropellerChainClustering.h"
#include "PropellerCFG.h"
#include "PropellerConfig.h"

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
  void doSplitOrder(std::list<StringRef> &symbolList,
                    std::list<StringRef>::iterator hotPlaceHolder,
                    std::list<StringRef>::iterator coldPlaceHolder);

  void printStats();
};

} // namespace propeller
} // namespace lld

#endif
