//===- PropellerFuncReordering.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file is part of the Propeller infrastructure for doing code layout
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
// References:
//   * [1] G.Ottoni and B.Maher, Optimizing Function Placement for Large-Scale
//        Data-Center Applications, CGO 2017. available at
//        https://research.fb.com/wp-content/uploads/2017/01/cgo2017-hfsort-final1.pdf
//===----------------------------------------------------------------------===//
#include "PropellerFuncOrdering.h"

#include "Config.h"
#include "Propeller.h"
#include "PropellerCfg.h"

#include <algorithm>
#include <map>

using lld::elf::config;

namespace lld {
namespace propeller {

const unsigned ClusterMergeSizeThreshold = 1 << 21;

// Initializes the CallChainClustering algorithm with the cfgs from propeller.
// It separates cfgs into hot and cold cfgs and initially orders each collection
// of cfgs based on the address of their corresponding functions in the original
// binary.
void CallChainClustering::init(Propeller &propeller) {
  propeller.forEachCfgRef([this](ControlFlowGraph &cfg) {
    if (cfg.isHot())
      this->HotCFGs.push_back(&cfg);
    else
      this->ColdCFGs.push_back(&cfg);
  });

  auto cfgComparator = [](ControlFlowGraph *cfg1,
                          ControlFlowGraph *cfg2) -> bool {
    return cfg1->getEntryNode()->MappedAddr < cfg2->getEntryNode()->MappedAddr;
  };
  auto sortCFGs = [&cfgComparator](std::vector<ControlFlowGraph *> &cfgs) {
    std::sort(cfgs.begin(), cfgs.end(), cfgComparator);
  };
  sortCFGs(HotCFGs);
  sortCFGs(ColdCFGs);
}

// Initialize a cluster containing a single cfg an associates it with a unique
// id.
CallChainClustering::Cluster::Cluster(ControlFlowGraph *cfg, unsigned id)
    : CFGs(1, cfg), Id(id) {}

// Returns the most frequent caller of a function. This function also gets as
// the second parameter the cluster containing this function to save a lookup
// into the CFGToClusterMap.
ControlFlowGraph *
CallChainClustering::getMostLikelyPredecessor(ControlFlowGraph *cfg,
                                              Cluster *cluster) {
  CFGNode *entry = cfg->getEntryNode();
  if (!entry)
    return nullptr;
  CFGEdge *bestCallIn = nullptr;

  // Iterate over all callers of the entry basic block of the function.
  for (CFGEdge *callIn : entry->CallIns) {
    auto *caller = callIn->Src->CFG;
    auto *callerCluster = CFGToClusterMap[caller];
    assert(caller->isHot());
    // Ignore callers from the same function, or the same cluster
    if (caller == cfg || callerCluster == cluster)
      continue;
    // Ignore callers with overly large clusters
    if (callerCluster->Size > ClusterMergeSizeThreshold)
      continue;
    // Ignore calls which are cold relative to the callee
    if (callIn->Weight * 10 < entry->Freq)
      continue;
    // Do not merge if the caller cluster's density would degrade by more than
    // 1/8 if merged.
    if (8 * callerCluster->Size * (cluster->Weight * callerCluster->Weight) <
        callerCluster->Weight * (cluster->Size + callerCluster->Size))
      continue;
    // Update the best CallIn edge if needed
    if (!bestCallIn || bestCallIn->Weight < callIn->Weight)
      bestCallIn = callIn;
  }
  return bestCallIn ? bestCallIn->Src->CFG : nullptr;
}

// Merge clusters together based on the CallChainClustering algorithm.
void CallChainClustering::mergeClusters() {
  // Build a map for the execution density of each cfg. This value will depend
  // on whether function-splitting is used or not.
  std::map<ControlFlowGraph *, double> cfgWeightMap;
  for (ControlFlowGraph *cfg : HotCFGs) {
    uint64_t cfgWeight = 0;
    uint64_t cfgSize = 0;
    cfg->forEachNodeRef([&cfgSize, &cfgWeight](CFGNode &n) {
      cfgWeight += n.Freq * n.ShSize;
      if (!config->propellerSplitFuncs || n.Freq)
        cfgSize += n.ShSize;
    });

    Cluster *c = new Cluster(cfg, ClusterCount++);
    c->Weight = cfgWeight;
    c->Size = std::max(cfgSize, (uint64_t)1);
    cfgWeightMap.emplace(cfg, c->getDensity());
    Clusters.emplace(c->Id, c);
    CFGToClusterMap[cfg] = c;
  }

  // Sort the hot cfgs in decreasing order of their execution density.
  std::stable_sort(
      HotCFGs.begin(), HotCFGs.end(),
      [&cfgWeightMap](ControlFlowGraph *cfg1, ControlFlowGraph *cfg2) {
        return cfgWeightMap[cfg1] > cfgWeightMap[cfg2];
      });

  for (ControlFlowGraph *cfg : HotCFGs) {
    if (cfgWeightMap[cfg] <= 0.005)
      break;
    auto *cluster = CFGToClusterMap[cfg];
    // Ignore merging if the cluster containing this function is bigger than
    // 2MBs (size of a large page).
    if (cluster->Size > ClusterMergeSizeThreshold)
      continue;
    assert(cluster);

    ControlFlowGraph *predecessorCfg = getMostLikelyPredecessor(cfg, cluster);
    if (!predecessorCfg)
      continue;
    auto *predecessorCluster = CFGToClusterMap[predecessorCfg];

    assert(predecessorCluster != cluster && predecessorCfg != cfg);

    // Join the two clusters into predecessorCluster.
    predecessorCluster->mergeWith(*cluster);

    // Update cfg to cluster mapping, because all cfgs that were
    // previsously in cluster are now in predecessorCluster.
    for (ControlFlowGraph *cfg : cluster->CFGs) {
      CFGToClusterMap[cfg] = predecessorCluster;
    }

    // Delete the defunct cluster
    Clusters.erase(cluster->Id);
  }
}

// This functions sorts all remaining clusters in decreasing order of their
// execution density.
void CallChainClustering::sortClusters(std::vector<Cluster *> &clusterOrder) {
  for (auto &p : Clusters)
    clusterOrder.push_back(p.second.get());
  std::sort(clusterOrder.begin(), clusterOrder.end(),
            [](Cluster *c1, Cluster *c2) {
              // Set a deterministic order when execution densities are equal.
              if (c1->getDensity() == c2->getDensity())
                return c1->CFGs.front()->getEntryNode()->MappedAddr <
                       c2->CFGs.front()->getEntryNode()->MappedAddr;
              return c1->getDensity() > c2->getDensity();
            });
}

// This function performs CallChainClustering on all cfgs and then orders all
// the built clusters based on their execution density. It places all cold
// functions after hot functions and returns the number of hot functions.
unsigned CallChainClustering::doOrder(std::list<ControlFlowGraph *> &cfgOrder) {
  mergeClusters();
  std::vector<Cluster *> clusterOrder;
  sortClusters(clusterOrder);
  for (Cluster *c : clusterOrder) {
    cfgOrder.insert(cfgOrder.end(), c->CFGs.begin(), c->CFGs.end());
  }

  cfgOrder.insert(cfgOrder.end(), ColdCFGs.begin(), ColdCFGs.end());
  return HotCFGs.size();
}

} // namespace propeller
} // namespace lld
