//===- PropellerChainClustering.h  ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_CHAIN_CLUSTERING_H
#define LLD_ELF_PROPELLER_CHAIN_CLUSTERING_H

#include "PropellerNodeChain.h"

#include "llvm/ADT/DenseMap.h"

#include <vector>

using llvm::DenseMap;

namespace lld {
namespace propeller {
const unsigned ClusterMergeSizeThreshold = 1 << 22;

class ChainClustering {
public:
  class Cluster {
  public:
    Cluster(NodeChain *);
    std::vector<NodeChain *> Chains;
    NodeChain *DelegateChain;
    uint64_t Size;
    uint64_t Weight;

    Cluster &mergeWith(Cluster &other) {
      Chains.insert(Chains.end(), other.Chains.begin(), other.Chains.end());
      this->Weight += other.Weight;
      this->Size += other.Size;
      return *this;
    }

    double getDensity() { return ((double)Weight / Size); }
  };

  void mergeTwoClusters(Cluster *predecessorCluster, Cluster *cluster) {
    // Join the two clusters into predecessorCluster.
    predecessorCluster->mergeWith(*cluster);

    // Update chain to cluster mapping, because all chains that were
    // previsously in cluster are now in predecessorCluster.
    for (NodeChain *c : cluster->Chains) {
      ChainToClusterMap[c] = predecessorCluster;
    }

    // Delete the defunct cluster
    Clusters.erase(cluster->DelegateChain->DelegateNode->MappedAddr);
  }

  void addChain(std::unique_ptr<NodeChain> &&chain_ptr);

  virtual void doOrder(std::vector<CFGNode *> &hotOrder,
                       std::vector<CFGNode *> &coldOrder);

  virtual ~ChainClustering() = default;

protected:
  virtual void mergeClusters(){};
  void sortClusters(std::vector<Cluster *> &);

  void initClusters() {
    for (auto &c_ptr : HotChains) {
      NodeChain *chain = c_ptr.get();
      Cluster *cl = new Cluster(chain);
      cl->Weight = chain->Freq;
      cl->Size = std::max(chain->Size, (uint64_t)1);
      ChainToClusterMap[chain] = cl;
      Clusters.try_emplace(cl->DelegateChain->DelegateNode->MappedAddr, cl);
    }
  }

  std::vector<std::unique_ptr<NodeChain>> HotChains, ColdChains;
  DenseMap<uint64_t, std::unique_ptr<Cluster>> Clusters;
  DenseMap<NodeChain *, Cluster *> ChainToClusterMap;
};

class NoOrdering : public ChainClustering {
public:
  void doOrder(std::vector<CFGNode *> &hotOrder,
               std::vector<CFGNode *> &coldOrder);
};

class CallChainClustering : public ChainClustering {
private:
  Cluster *getMostLikelyPredecessor(NodeChain *chain, Cluster *cluster);
  void mergeClusters();
};



} // namespace propeller
} // namespace lld

namespace std {
template <> struct less<lld::propeller::ChainClustering::Cluster *> {
  bool operator()(const lld::propeller::ChainClustering::Cluster *c1,
                  const lld::propeller::ChainClustering::Cluster *c2) const {
    return less<lld::propeller::NodeChain *>()(c1->DelegateChain,
                                               c2->DelegateChain);
  }
};

} // namespace std

#endif
