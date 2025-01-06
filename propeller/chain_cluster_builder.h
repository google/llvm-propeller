// Copyright 2024 The Propeller Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PROPELLER_CHAIN_CLUSTER_BUILDER_H_
#define PROPELLER_CHAIN_CLUSTER_BUILDER_H_

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"
#include "propeller/node_chain.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {

// Represents an ordered cluster of chains.
class ChainCluster {
 public:
  explicit ChainCluster(std::unique_ptr<const NodeChain> chain)
      : id_(chain->id()), size_(chain->size()), freq_(chain->freq()) {
    chains_.push_back(std::move(chain));
  }

  // ChainCluster is a moveonly type.
  ChainCluster(ChainCluster &&) = default;
  ChainCluster &operator=(ChainCluster &&) = default;

  ChainCluster(const ChainCluster &) = delete;
  ChainCluster &operator=(const ChainCluster &) = delete;

  const std::vector<std::unique_ptr<const NodeChain>> &chains() const {
    return chains_;
  }

  // Returns the total binary size of the cluster.
  int size() const { return size_; }

  // Returns the total frequency of the cluster.
  int freq() const { return freq_; }

  // Returns the unique identifier for this cluster.
  InterCfgId id() const { return id_; }

  // Returns the execution density for this cluster.
  double exec_density() const {
    return static_cast<double>(freq_) / std::max(size_, 1);
  }

  // Merges the chains in `other` cluster into `this` cluster. `other`
  // ChainCluster will be consumed by this call.
  void MergeWith(ChainCluster other) {
    absl::c_move(other.chains_, std::back_inserter(chains_));
    this->freq_ += other.freq_;
    this->size_ += other.size_;
  }

  // Iterates over all nodes in this cluster (in order) and applies the given
  // `func` on every node.
  void VisitEachNodeRef(absl::FunctionRef<void(const CFGNode &)> func) const {
    for (const std::unique_ptr<const NodeChain> &chain : chains_)
      chain->VisitEachNodeRef(func);
  }

 private:
  // The chains in this cluster in the merged order.
  std::vector<std::unique_ptr<const NodeChain>> chains_;

  // Unique id of the cluster.
  InterCfgId id_;

  // Total size of the cluster.
  int size_;

  // Total frequency of the cluster.
  int freq_;
};

class ChainClusterBuilder {
 public:
  // ChainClusterBuilder constructor: This initializes one cluster per each
  // chain and transfers the ownership of the NodeChain pointer to their
  // associated clusters.
  explicit ChainClusterBuilder(
      const PropellerCodeLayoutParameters &code_layout_params,
      std::vector<std::unique_ptr<const NodeChain>> chains);

  // Builds and returns the clusters of chains.
  // This function builds clusters of node chains according to the
  // call-chain-clustering algorithm[1] and returns them in a vector. After this
  // is called, all clusters are moved to the vector and the `clusters_`
  // map becomes empty.
  // [1] https://dl.acm.org/doi/10.5555/3049832.3049858
  std::vector<std::unique_ptr<const ChainCluster>> BuildClusters() &&;

  // Finds the most frequent predecessor cluster of `chain` and merges it with
  // `chain`'s cluster.
  void MergeWithBestPredecessorCluster(const NodeChain &chain);

  // Merges `right_cluster` into `left_cluster`. This call consumes
  // `right_cluster`.
  void MergeClusters(ChainCluster &left_cluster, ChainCluster right_cluster);

 private:
  PropellerCodeLayoutParameters code_layout_params_;
  const absl::flat_hash_map<const CFGNode *, const NodeChain *>
      node_to_chain_map_;

  // All clusters currently in process.
  absl::flat_hash_map<InterCfgId, std::unique_ptr<const ChainCluster>>
      clusters_;

  // This maps every chain to its containing cluster.
  absl::flat_hash_map<const NodeChain *, ChainCluster *> chain_to_cluster_map_;
};

}  // namespace propeller

#endif  //  THIRD_PARTY_LLVM_PROPELLER_CHAIN_CLUSTER_BUILDER_H_
