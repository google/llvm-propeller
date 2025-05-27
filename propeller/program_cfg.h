// Copyright 2025 The Propeller Authors.
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

#ifndef PROPELLER_PROGRAM_CFG_H_
#define PROPELLER_PROGRAM_CFG_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/cfg.h"

namespace propeller {

class ProgramCfg {
  // This class represents the whole-program control flow graph.
 public:
  explicit ProgramCfg(
      absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs)
      : cfgs_(std::move(cfgs)) {}

  ProgramCfg(const ProgramCfg &) = delete;
  ProgramCfg &operator=(const ProgramCfg &) = delete;
  ProgramCfg(ProgramCfg &&) = default;
  ProgramCfg &operator=(ProgramCfg &&) = default;

  // Builds and returns a map of cfgs keyed by their function indexes.
  absl::flat_hash_map<int, const ControlFlowGraph *> cfgs_by_index() const {
    absl::flat_hash_map<int, const ControlFlowGraph *> result;
    for (const auto &[function_index, cfg] : cfgs_) {
      result.emplace(function_index, cfg.get());
    }
    return result;
  }

  // Builds and returns a map of cfgs keyed by their function names.
  absl::flat_hash_map<std::string, const ControlFlowGraph *> cfgs_by_name()
      const {
    absl::flat_hash_map<std::string, const ControlFlowGraph *> result;
    for (const auto &[function_index, cfg] : cfgs_) {
      CHECK(result.emplace(cfg->GetPrimaryName().str(), cfg.get()).second)
          << " Duplicate function name: " << cfg->GetPrimaryName().str();
    }
    return result;
  }

  // Returns the CFGs in a vector, in increasing order of their function index.
  std::vector<const ControlFlowGraph *> GetCfgs() const;

  // Returns a map from section names to the CFGs associated with them.
  absl::flat_hash_map<llvm::StringRef, std::vector<const ControlFlowGraph *>>
  GetCfgsBySectionName() const;

  // Returns the cfg with function_index `index` or `nullptr` if it does not
  // exist.
  const ControlFlowGraph *GetCfgByIndex(int index) const {
    auto it = cfgs_.find(index);
    if (it == cfgs_.end()) return nullptr;
    return it->second.get();
  }

  // Returns the `node_frequency_cutoff_percentile` frequency percentile among
  // all nodes with non-zero frequencies. `node_frequency_cutoff_percentile`
  // must be between 0 and 100.
  int GetNodeFrequencyThreshold(int node_frequency_cutoff_percentile) const;

  // Returns the bb_indexes of hot join nodes in all CFGs. These are nodes which
  // have a frequency of at least `hot_node_frequency_threshold` and at least
  // two incoming intra-function edges at least as heavy as
  // `hot_edge_frequency_threshold`. Basic block indexes are returned in a map
  // keyed by their function index.
  absl::flat_hash_map<int, absl::btree_set<int>> GetHotJoinNodes(
      int hot_node_frequency_threshold, int hot_edge_frequency_threshold) const;

  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>>
  release_cfgs_by_index() && {
    absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> ret;
    for (auto &[function_index, cfg] : cfgs_) {
      ret.emplace(
          function_index,
          absl::WrapUnique(const_cast<ControlFlowGraph *>(cfg.release())));
    }
    return ret;
  }

 private:
  // Cfgs indexed by function index.
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs_;
};
}  // namespace propeller

#endif  // PROPELLER_PROGRAM_CFG_H_
