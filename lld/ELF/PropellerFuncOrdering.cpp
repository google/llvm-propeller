//===- PropellerFuncReordering.cpp
//-------------------------------------------===//
////
//// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
//// See https://llvm.org/LICENSE.txt for license information.
//// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
////
////===----------------------------------------------------------------------===//
// This file is part of the Propeller infrastcture for doing code layout
// optimization and includes the implementation of function reordering based on
// the CallChainClustering algorithm as published in [1].
//
// This algorithm keeps merging functions together into clusters until the
// cluster sizes reach a limit. The algorithm iterates over functions in
// decreasing order of their execution density (total frequency divided by size)
// and for each function, it first finds the cluster containing the
// most-frequent caller of that function and then places the caller's cluster
// right before the callee's cluster. Finally, all the remaining clusters are
// ordered in decreasing order of their execution density.
//
// [1] G.Ottoni and B.Maher, Optimizing Function Placement for Large-Scale
// Data-Center Applications, CGO 2017. available at
// https://research.fb.com/wp-content/uploads/2017/01/cgo2017-hfsort-final1.pdf
#include "PropellerFuncOrdering.h"

#include "Config.h"
#include "PropellerELFCfg.h"

#include <algorithm>
#include <map>

using lld::elf::config;

namespace lld {
namespace propeller {

template <class CfgContainerTy>
void CCubeAlgorithm::init(CfgContainerTy &CfgContainer) {
  CfgContainer.forEachCfgRef([this](ELFCFG &CFG) {
    if (CFG.isHot())
      this->HotCfgs.push_back(&CFG);
    else
      this->ColdCfgs.push_back(&CFG);
  });

  auto CfgComparator = [](ELFCFG *Cfg1, ELFCFG *Cfg2) ->bool {
    return Cfg1->getEntryNode()->MappedAddr < Cfg2->getEntryNode()->MappedAddr;
  };
  auto sortCfg = [&CfgComparator](vector<ELFCFG *> &CFG) {
    std::sort(CFG.begin(), CFG.end(), CfgComparator);
  };
  sortCFGs(HotCFGs);
  sortCFGs(ColdCFGs);
}

CCubeAlgorithm::Cluster::Cluster(ELFCFG *CFG)
    : CFGs(1, CFG) {}

CCubeAlgorithm::Cluster::~Cluster() {}

ELFCFG *CCubeAlgorithm::getMostLikelyPredecessor(
    Cluster *Cluster, ELFCFG *CFG,
    map<ELFCFG *, CCubeAlgorithm::Cluster *>
        &ClusterMap) {
  ELFCFGNode *Entry = CFG->getEntryNode();
  if (!Entry)
    return nullptr;
  ELFCFGEdge *E = nullptr;
  for (ELFCFGEdge *CallIn : Entry->CallIns) {
    auto *Caller = CallIn->Src->CFG;
    auto *CallerCluster = ClusterMap[Caller];
    assert(Caller->isHot());
    if (Caller == CFG || CallerCluster == Cluster)
      continue;
    // Ignore calls which are cold relative to the callee
    if (callIn->Weight * 10 < entry->Freq)
      continue;
    // Do not merge if the caller cluster's density would degrade by more than
    // 1/8 if merged.
    if (8 * callerCluster->Size * (cluster->Weight * callerCluster->Weight) <
        callerCluster->Weight * (cluster->Size + callerCluster->Size))
      continue;
    if (!E || E->Weight < CallIn->Weight) {
      if (ClusterMap[CallIn->Src->CFG]->Size > (1 << 21)) continue;
      E = CallIn;
    }
  }
  return E ? E->Src->CFG : nullptr;
}

void CCubeAlgorithm::mergeClusters() {
  // Signed key is used here, because negated density are used as
  // sorting keys.
  map<ELFCFG *, double> CfgWeightMap;
  map<ELFCFG *, Cluster *> ClusterMap;
  for(ELFCFG * CFG: HotCfgs){
    uint64_t CfgWeight = 0;
    uint64_t CfgSize = config->propellerSplitFuncs ? 0 : (double)CFG->Size;
    CFG->forEachNodeRef([&CfgSize, &CfgWeight](ELFCFGNode &N) {
      CfgWeight += N.Freq * N.ShSize;
      if (config->propellerSplitFuncs && N.Freq)
        CfgSize += N.ShSize;
    });

    assert(CfgSize!=0);
    Cluster *C = new Cluster(CFG);
    C->Weight = CfgWeight;
    C->Size = std::max(CfgSize, (uint64_t)1);
    CfgWeightMap.emplace(CFG, C->getDensity());
    C->Handler = Clusters.emplace(Clusters.end(), C);
    ClusterMap[CFG] = C;
  }

  std::stable_sort(HotCfgs.begin(), HotCfgs.end(),
            [&CfgWeightMap] (ELFCFG* Cfg1, ELFCFG* Cfg2){
              return CfgWeightMap[Cfg1] > CfgWeightMap[Cfg2];
            });
  for (ELFCFG* CFG : HotCfgs){
    if (CfgWeightMap[CFG] <= 0.005)
      break;
    auto *Cluster = ClusterMap[CFG];
    if (Cluster->Size > (1 << 21))
      continue;
    assert(cluster);

    ELFCFG *PredecessorCfg =
        getMostLikelyPredecessor(Cluster, CFG, ClusterMap);
    if (!PredecessorCfg)
      continue;
    assert(PredecessorCfg != CFG);
    // log("propeller: most-likely caller of " + Twine(CFG->Name) + " -> " + Twine(PredecessorCfg->Name));
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    assert(PredecessorCluster);

    assert(predecessorCluster != cluster && predecessorCfg != cfg);

    // Join the two clusters into predecessorCluster.
    predecessorCluster->mergeWith(*cluster);

    // Update CFG <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (ELFCFG *CFG : Cluster->CFGs) {
      ClusterMap[CFG] = PredecessorCluster;
    }

    // Delete the defunct cluster
    Clusters.erase(cluster->Id);
  }
}

void CCubeAlgorithm::sortClusters() {
  Clusters.sort([](unique_ptr<Cluster> &C1, unique_ptr<Cluster> &C2) {
    if (C1->getDensity() == C2->getDensity())
      return C1->CFGs.front()->getEntryNode()->MappedAddr <
             C2->CFGs.front()->getEntryNode()->MappedAddr;
    return C1->getDensity() > C2->getDensity();
  });
}

unsigned CCubeAlgorithm::doOrder(list<ELFCFG *>& CfgOrder) {
  mergeClusters();
  sortClusters();
  for (auto &Cptr : Clusters) {
    CfgOrder.insert(CfgOrder.end(), Cptr->CFGs.begin(), Cptr->CFGs.end());
  }

  cfgOrder.insert(cfgOrder.end(), ColdCFGs.begin(), ColdCFGs.end());
  return HotCFGs.size();
}

} // namespace propeller
} // namespace lld
