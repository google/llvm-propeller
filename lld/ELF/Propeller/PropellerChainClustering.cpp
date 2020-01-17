//===- PropellerChainClustering.cpp  --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file is part of the Propeller infrastructure for doing code layout
// optimization and includes the implementation of the Call-Chain-Clustering
// algorithm as described in [1].
//
// The algorithm iteratively merges chains into clusters. To do so, it processes
// the basic block chains in decreasing order of
// their execution density and for each chain, merges its cluster with its
// most-frequent predecessor cluster. The merging of a cluster stops when it
// reaches the -propeller-cluster-merge-size-threshold (set to 2MB by default).
//
//   [1] Guilherme Ottoni and Bertrand Maher. Optimizing Function Placement for
//   Large-Scale Data-Center Applications. Available at
//   https://research.fb.com/wp-content/uploads/2017/01/cgo2017-hfsort-final1.pdf
//===----------------------------------------------------------------------===//
#include "PropellerChainClustering.h"
#include "PropellerConfig.h"

using llvm::detail::DenseMapPair;

namespace lld {
namespace propeller {

void ChainClustering::addChain(std::unique_ptr<NodeChain> &&chain_ptr) {
  for (CFGNode *n : chain_ptr->Nodes)
    n->Chain = chain_ptr.get();
  auto &chainList =
      ((propellerConfig.optReorderIP || propellerConfig.optSplitFuncs ||
        propellerConfig.optReorderFuncs) &&
       chain_ptr->Freq == 0)
          ? ColdChains
          : HotChains;
  chainList.push_back(std::move(chain_ptr));
}

// Initializes a cluster containing a single chain an associates it with a unique
// id.
ChainClustering::Cluster::Cluster(NodeChain *chain)
    : Chains(1, chain), DelegateChain(chain) {}

// Returns the most frequent predecessor of a chain. This function also gets as
// the second parameter the cluster containing the chain to save a lookup
// into ChainToClusterMap.
ChainClustering::Cluster *
CallChainClustering::getMostLikelyPredecessor(NodeChain *chain,
                                              Cluster *cluster) {
  // This map stores the total weight of the incoming edges to the given cluster
  // from every other cluster.
  DenseMap<Cluster *, uint64_t> clusterEdge;

  for (CFGNode *n : chain->Nodes) {
    // For non-inter-procedural, we only consider the function entries.
    if (!propellerConfig.optReorderIP && !n->isEntryNode())
      continue;
    auto visit = [&clusterEdge, n, chain, cluster, this](CFGEdge &edge) {
      if (!edge.Weight || edge.isReturn())
        return;
      auto *caller = edge.Src->Chain;
      if (!caller)
        return;
      auto *callerCluster = ChainToClusterMap[caller];
      assert(caller->Freq);
      if (caller == chain || callerCluster == cluster)
        return;
      // Ignore clusters which are too big
      if (callerCluster->Size > propellerConfig.optClusterMergeSizeThreshold)
        return;
      // Ignore edges which are cold relative to the sink node
      if (edge.Weight * 10 < n->Freq)
        return;
      // Do not merge if the caller cluster's density would degrade by more than
      // 1/8 by the merge.
      if (8 * callerCluster->Size * (cluster->Freq * callerCluster->Freq) <
          callerCluster->Freq * (cluster->Size + callerCluster->Size))
        return;
      clusterEdge[callerCluster] += edge.Weight;
    };
    n->forEachInEdgeRef(visit);
  }

  // Get the highest-frequency caller
  auto bestCaller =
      std::max_element(clusterEdge.begin(), clusterEdge.end(),
                       [](const DenseMapPair<Cluster *, uint64_t> &p1,
                          const DenseMapPair<Cluster *, uint64_t> &p2) {
                         if (p1.second == p2.second) // Consistently break ties
                           return std::less<Cluster *>()(p1.first, p2.first);
                         return p1.second < p2.second;
                       });

  if (bestCaller == clusterEdge.end())
    return nullptr;
  return bestCaller->first;
}

void ChainClustering::sortClusters(std::vector<Cluster *> &clusterOrder) {
  for (auto &p : Clusters)
    clusterOrder.push_back(p.second.get());

  auto clusterComparator = [](Cluster *c1, Cluster *c2) -> bool {
    // Set a deterministic order when execution densities are equal.
    if (c1->getDensity() == c2->getDensity())
      return c1->DelegateChain->DelegateNode->MappedAddr <
             c2->DelegateChain->DelegateNode->MappedAddr;
    return c1->getDensity() > c2->getDensity();
  };

  std::sort(clusterOrder.begin(), clusterOrder.end(), clusterComparator);
}

void NoOrdering::doOrder(std::vector<CFGNode *> &hotOrder,
                         std::vector<CFGNode *> &coldOrder) {
  auto chainComparator = [](const std::unique_ptr<NodeChain> &c_ptr1,
                            const std::unique_ptr<NodeChain> &c_ptr2) -> bool {
    return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
  };

  std::sort(HotChains.begin(), HotChains.end(), chainComparator);
  std::sort(ColdChains.begin(), ColdChains.end(), chainComparator);

  for (auto &c_ptr : HotChains)
    for (CFGNode *n : c_ptr->Nodes)
      hotOrder.push_back(n);

  for (auto &c_ptr : ColdChains)
    for (CFGNode *n : c_ptr->Nodes)
      coldOrder.push_back(n);
}

// Merge clusters together based on the CallChainClustering algorithm.
void CallChainClustering::mergeClusters() {
  // Build a map for the execution density of each chain.
  DenseMap<NodeChain *, double> chainWeightMap;

  for (auto &c_ptr : HotChains) {
    NodeChain *chain = c_ptr.get();
    chainWeightMap.try_emplace(chain, chain->execDensity());
  }

  // Sort the hot chains in decreasing order of their execution density.
  std::sort(HotChains.begin(), HotChains.end(),
            [&chainWeightMap](const std::unique_ptr<NodeChain> &c_ptr1,
                              const std::unique_ptr<NodeChain> &c_ptr2) {
              auto chain1Weight = chainWeightMap[c_ptr1.get()];
              auto chain2Weight = chainWeightMap[c_ptr2.get()];
              if (chain1Weight == chain2Weight)
                return c_ptr1->DelegateNode->MappedAddr <
                       c_ptr2->DelegateNode->MappedAddr;
              return chain1Weight > chain2Weight;
            });

  for (auto &c_ptr : HotChains) {
    NodeChain *chain = c_ptr.get();
    if (chainWeightMap[chain] <= 0.005)
      break;
    auto *cluster = ChainToClusterMap[chain];
    // Ignore merging if the cluster containing this function is bigger than
    // 2MBs (size of a large page).
    if (cluster->Size > propellerConfig.optClusterMergeSizeThreshold)
      continue;
    assert(cluster);

    Cluster *predecessorCluster = getMostLikelyPredecessor(chain, cluster);
    if (!predecessorCluster)
      continue;

    // assert(predecessorCluster != cluster && predecessorChain != chain);
    mergeTwoClusters(predecessorCluster, cluster);
  }
}

// This function orders the hot chains based on mergeClusters and sortClusters
// and uses a consistent ordering for the cold part.
void ChainClustering::doOrder(std::vector<CFGNode *> &hotOrder,
                              std::vector<CFGNode *> &coldOrder) {
  initClusters();
  mergeClusters();
  std::vector<Cluster *> clusterOrder;
  sortClusters(clusterOrder);
  // This maps every CFG to the position of its first hot node in the layout.
  DenseMap<ControlFlowGraph *, size_t> hotCFGOrder;

  for (Cluster *cl : clusterOrder)
    for (NodeChain *c : cl->Chains)
      for (CFGNode *n : c->Nodes) {
        hotCFGOrder.try_emplace(n->CFG, hotOrder.size());
        hotOrder.push_back(n);
      }

  auto coldChainComparator =
      [&hotCFGOrder](const std::unique_ptr<NodeChain> &c_ptr1,
                    const std::unique_ptr<NodeChain> &c_ptr2) -> bool {
    // If each of the chains comes from a single CFG, we can order the chains
    // consistently based on the hot layout
    if (c_ptr1->CFG && c_ptr2->CFG) {
      // If one CFG is cold and the other is hot, make sure the hot CFG's chain
      // comes earlier in the order.
      if (c_ptr1->CFG->isHot() != c_ptr2->CFG->isHot())
        return c_ptr1->CFG->isHot();
      // If both CFGs are hot, make the order for cold chains consistent with
      // the order for hot ones.
      if (c_ptr1->CFG->isHot() && c_ptr2->CFG->isHot() &&
          (c_ptr1->CFG != c_ptr2->CFG))
        return hotCFGOrder[c_ptr1->CFG] < hotCFGOrder[c_ptr2->CFG];
      // Otherwise, use a consistent order based on the representative nodes.
      return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
    }
    // Use an order consistent with the initial ordering of the representative
    // nodes.
    return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
  };

  std::sort(ColdChains.begin(), ColdChains.end(), coldChainComparator);

  for (auto &c_ptr : ColdChains)
    for (CFGNode *n : c_ptr->Nodes)
      coldOrder.push_back(n);
}

} // namespace propeller
} // namespace lld
