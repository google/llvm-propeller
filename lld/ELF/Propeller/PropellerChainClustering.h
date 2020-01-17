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

// This is the base class for clustering chains. It provides the basic data
// structures and functions.
class ChainClustering {
public:
  class Cluster {
  public:
    // Constructor for building a cluster from a single chain.
    Cluster(NodeChain *);

    // The chains in this cluster in the merged order.
    std::vector<NodeChain *> Chains;

    // The representative chain for this cluster.
    NodeChain *DelegateChain;

    // Total size of the cluster.
    uint64_t Size;

    // Total frequency of the cluster.
    uint64_t Freq;

    // This function merges this cluster with another cluster
    Cluster &mergeWith(Cluster &other) {
      Chains.insert(Chains.end(), other.Chains.begin(), other.Chains.end());
      this->Freq += other.Freq;
      this->Size += other.Size;
      return *this;
    }

    // Returns the execution density of this cluster
    double getDensity() { return ((double)Freq / Size); }
  };

  // This function merges two clusters in the given order, and updates
  // ChainToClusterMap for the chains in the mergee cluster and also removes the
  // mergee cluster from the Clusters.
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

  // Adds a chain to the input of the clustering process
  void addChain(std::unique_ptr<NodeChain> &&chain_ptr);

  // Any subclass should override this function.
  virtual void doOrder(std::vector<CFGNode *> &hotOrder,
                       std::vector<CFGNode *> &coldOrder);

  virtual ~ChainClustering() = default;

protected:
  // Optional function for merging clusters, which could be overriden by
  // subclasses.
  virtual void mergeClusters(){};

  // This function sorts the final clusters in decreasing order of their execution density.
  void sortClusters(std::vector<Cluster *>&);

  // This function initializes clusters with each cluster including a single
  // chain.
  void initClusters() {
    for (auto &c_ptr : HotChains) {
      NodeChain *chain = c_ptr.get();
      Cluster *cl = new Cluster(chain);
      cl->Freq = chain->Freq;
      cl->Size = std::max(chain->Size, (uint64_t)1);
      ChainToClusterMap[chain] = cl;
      Clusters.try_emplace(cl->DelegateChain->DelegateNode->MappedAddr, cl);
    }
  }

  // Chains processed by the clustering algorithm separated into hot and cold
  // ones based on their frequency.
  std::vector<std::unique_ptr<NodeChain>> HotChains, ColdChains;

  // All clusters currently in process.
  DenseMap<uint64_t, std::unique_ptr<Cluster>> Clusters;

  // This maps every chain to its containing cluster.
  DenseMap<NodeChain *, Cluster *> ChainToClusterMap;
};

// This class computes an ordering for the input chains which conforms to the
// initial ordering of nodes in the binary.
class NoOrdering : public ChainClustering {
public:
  void doOrder(std::vector<CFGNode *> &hotOrder,
               std::vector<CFGNode *> &coldOrder);
};

// This class implements the call-chain-clustering algorithm.
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
