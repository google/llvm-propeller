#include "PropellerChainClustering.h"

using llvm::detail::DenseMapPair;

namespace lld {
namespace propeller {

void ChainClustering::addChain(std::unique_ptr<NodeChain>&& chain_ptr){
  for(CFGNode *n: chain_ptr->Nodes)
    n->Chain = chain_ptr.get();
  auto& chainList = ((propellerConfig.optReorderIP || propellerConfig.optSplitFuncs || propellerConfig.optReorderFuncs) && chain_ptr->Freq==0) ? ColdChains : HotChains;
  chainList.push_back(std::move(chain_ptr));
}

// Initialize a cluster containing a single chain an associates it with a unique
// id.
ChainClustering::Cluster::Cluster(NodeChain *chain)
    : Chains(1, chain), DelegateChain(chain) {}

// Returns the most frequent caller of a function. This function also gets as
// the second parameter the cluster containing this function to save a lookup
// into the ChainToClusterMap.
ChainClustering::Cluster *
CallChainClustering::getMostLikelyPredecessor(NodeChain * chain,
                                              Cluster *cluster) {
  DenseMap<Cluster*, uint64_t> clusterEdge;

  for(CFGNode * n: chain->Nodes){
    if (!propellerConfig.optReorderIP && !n->isEntryNode())
      continue;
    auto visit = [&clusterEdge, n, chain, cluster, this] (CFGEdge& edge){
      if (!edge.Weight)
        return;
      if (edge.isReturn())
        return;
      auto *caller = edge.Src->Chain;
      if (!caller)
        return;
      auto * callerCluster = ChainToClusterMap[caller];
      assert(caller->Freq);
      if (caller == chain || callerCluster == cluster)
        return;
      if (callerCluster->Size > ClusterMergeSizeThreshold)
        return;
      // Ignore calls which are cold relative to the callee
      if (edge.Weight * 10 < n->Freq)
        return;
      // Do not merge if the caller cluster's density would degrade by more than
      // 1/8.
      if (8 * callerCluster->Size * (cluster->Weight * callerCluster->Weight) <
          callerCluster->Weight * (cluster->Size + callerCluster->Size))
        return;
      clusterEdge[callerCluster] += edge.Weight;
    };
    n->forEachInEdgeRef(visit);

  }

  auto bestCaller = std::max_element(clusterEdge.begin(), clusterEdge.end(), [] (const DenseMapPair<Cluster*, uint64_t>& p1,
                                                               const DenseMapPair<Cluster*, uint64_t>& p2) {
    if (p1.second == p2.second)
      return std::less<Cluster*>()(p1.first, p2.first);
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

void NoOrdering::doOrder(std::vector<CFGNode*> &hotOrder,
                                  std::vector<CFGNode*> &coldOrder){
  auto chainComparator = [](const std::unique_ptr<NodeChain> &c_ptr1,
                            const std::unique_ptr<NodeChain> &c_ptr2) -> bool {
    return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
  };

  std::sort(HotChains.begin(), HotChains.end(), chainComparator);
  std::sort(ColdChains.begin(), ColdChains.end(), chainComparator);

  for(auto& c_ptr: HotChains)
    for(CFGNode* n: c_ptr->Nodes)
      hotOrder.push_back(n);

  for(auto& c_ptr: ColdChains)
    for(CFGNode* n: c_ptr->Nodes)
    coldOrder.push_back(n);
}

// Merge clusters together based on the CallChainClustering algorithm.
void CallChainClustering::mergeClusters() {
  // Build a map for the execution density of each chain.
  DenseMap<NodeChain *, double> chainWeightMap;

  for(auto& c_ptr: HotChains){
    NodeChain * chain = c_ptr.get();
    chainWeightMap.try_emplace(chain, chain->execDensity());
  }

  // Sort the hot chains in decreasing order of their execution density.
  std::sort(HotChains.begin(), HotChains.end(),
            [&chainWeightMap] (const std::unique_ptr<NodeChain> &c_ptr1,
                               const std::unique_ptr<NodeChain> &c_ptr2){
              auto chain1Weight = chainWeightMap[c_ptr1.get()];
              auto chain2Weight = chainWeightMap[c_ptr2.get()];
              if (chain1Weight == chain2Weight)
                return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
              return chain1Weight > chain2Weight;
            });

  for (auto& c_ptr : HotChains){
    NodeChain* chain = c_ptr.get();
    if (chainWeightMap[chain] <= 0.005)
      break;
    auto *cluster = ChainToClusterMap[chain];
    // Ignore merging if the cluster containing this function is bigger than
    // 2MBs (size of a large page).
    if (cluster->Size > ClusterMergeSizeThreshold)
      continue;
    assert(cluster);

    Cluster *predecessorCluster = getMostLikelyPredecessor(chain, cluster);
    if (!predecessorCluster)
      continue;

    //assert(predecessorCluster != cluster && predecessorChain != chain);
    mergeTwoClusters(predecessorCluster, cluster);
  }
}


void ChainClustering::doOrder(std::vector<CFGNode*> &hotOrder,
                                  std::vector<CFGNode*> &coldOrder){
  //warn("[propeller]" + Twine(HotChains.size())+ " Hot chains and " + Twine(ColdChains.size()) + " Cold chains.");
  initClusters();
  mergeClusters();
  std::vector<Cluster *> clusterOrder;
  DenseMap<ControlFlowGraph *, size_t> ChainOrder;
  sortClusters(clusterOrder);
  for (Cluster *cl: clusterOrder)
    for(NodeChain* c: cl->Chains)
      for(CFGNode *n: c->Nodes) {
        ChainOrder.try_emplace(n->CFG, hotOrder.size());
        hotOrder.push_back(n);
      }

  auto coldChainComparator = [&ChainOrder](const std::unique_ptr<NodeChain> &c_ptr1,
                                           const std::unique_ptr<NodeChain> &c_ptr2) -> bool {
    if (c_ptr1->CFG && c_ptr2->CFG) {
      if (c_ptr1->CFG->isHot() != c_ptr2->CFG->isHot())
        return c_ptr1->CFG->isHot();
      if (c_ptr1->CFG->isHot() && c_ptr2->CFG->isHot() && (c_ptr1->CFG != c_ptr2->CFG))
        return ChainOrder[c_ptr1->CFG] < ChainOrder[c_ptr2->CFG];
    }
    return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
  };

  std::sort(ColdChains.begin(), ColdChains.end(), coldChainComparator);

  for(auto &c_ptr: ColdChains)
    for(CFGNode* n: c_ptr->Nodes)
    coldOrder.push_back(n);
}


}
}
