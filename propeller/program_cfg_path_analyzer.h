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

#ifndef PROPELLER_PROGRAM_CFG_PATH_ANALYZER_H_
#define PROPELLER_PROGRAM_CFG_PATH_ANALYZER_H_
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/cfg.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"

namespace propeller {
// Represents a path along with its predecessor block. This path starts from
// the predecessor block associated with `pred_node_bb_index_` and then
// follows the path from the root of the path tree containing `path_node_` to
// `path_node_`.
struct BasePathProbe {
  // Current path node in the tree.
  propeller::PathNode *path_node;
  // Predecessor of the root of the tree.
  int pred_node_bb_index;

  // Returns true if the path associated with `*this` is a suffix of the path
  // associated with `other`.
  bool IsSuffixOf(const BasePathProbe &other) const {
    if (path_node->path_length() > other.path_node->path_length()) return false;
    const propeller::PathNode *path_node_ptr = path_node;
    const propeller::PathNode *other_path_node_ptr = other.path_node;
    while (path_node_ptr != nullptr) {
      if (path_node_ptr->node_bb_index() !=
          other_path_node_ptr->node_bb_index()) {
        return false;
      }
      path_node_ptr = path_node_ptr->parent();
      other_path_node_ptr = other_path_node_ptr->parent();
    }
    return pred_node_bb_index == (other_path_node_ptr == nullptr
                                      ? other.pred_node_bb_index
                                      : other_path_node_ptr->node_bb_index());
  }

  bool operator==(const BasePathProbe &other) const {
    return path_node == other.path_node &&
           pred_node_bb_index == other.pred_node_bb_index;
  }
  bool operator!=(const BasePathProbe &other) const {
    return !(*this == other);
  }
  template <typename H>
  friend H AbslHashValue(H h, const BasePathProbe &path_probe) {
    return H::combine(std::move(h), path_probe.path_node,
                      path_probe.pred_node_bb_index);
  }
};

// This represents a `BasePathProbe` along with the set of bb_indexes in that
// path.
class PathProbe {
 public:
  PathProbe(propeller::PathNode *ABSL_NONNULL path_node
                ABSL_ATTRIBUTE_LIFETIME_BOUND,
            int pred_node_bb_index)
      : base_path_probe_{.path_node = path_node,
                         .pred_node_bb_index = pred_node_bb_index},
        nodes_in_path_({path_node->node_bb_index()}) {}

  PathProbe(const PathProbe &) = default;
  PathProbe &operator=(const PathProbe &) = default;
  PathProbe(PathProbe &&) = default;
  PathProbe &operator=(PathProbe &&) = default;

  propeller::PathNode *path_node() const { return base_path_probe_.path_node; }
  int pred_node_bb_index() const { return base_path_probe_.pred_node_bb_index; }
  const absl::flat_hash_set<int> &nodes_in_path() const {
    return nodes_in_path_;
  }
  const BasePathProbe &base_path_probe() const { return base_path_probe_; }

  void set_path_node(propeller::PathNode *path_node) {
    base_path_probe_.path_node = path_node;
  }

  // Inserts `bb_index` in `nodes_in_path`. Returns true if insertion happens,
  // i.e., `bb_index` is not already in `nodes_in_path_`.
  bool AddToNodesInPath(int bb_index) {
    return nodes_in_path_.insert(bb_index).second;
  }

  // Returns the length of the path (number of blocks in the path excluding the
  // path predecessor block).
  int GetPathLength() const { return nodes_in_path_.size(); }

 private:
  BasePathProbe base_path_probe_;
  // All node indexes in the path from the predecessor block to `path_node_`.
  absl::flat_hash_set<int> nodes_in_path_;
};

// This struct represents the path probes encountered for a single block at a
// single time.
struct PathProbeSampleInfo {
  absl::Time sample_time;
  std::vector<BasePathProbe> path_probes;
  int path_length;

  // Returns true if either `path_probes` contains the given `probe` or if it
  // could have been included in `path_probes` if `path_length` was large
  // enough.
  bool CouldImply(const BasePathProbe &probe) const {
    // If this sample was from a short path, check if it could have potentially
    // included the path associated with `probe`.
    if (probe.path_node->path_length() > path_length) {
      // If no path probes were recorded, `probe` could have been reached.
      if (path_probes.empty()) return true;
      // If there are some recorded path probes, `probe` could not have been
      // reached unless the path associated with the longest probe (the first
      // one) is a suffix of the path associated with `probe`.
      return path_probes.front().IsSuffixOf(probe);
    }
    return absl::c_linear_search(path_probes, probe);
  }

  // Implementation of the `AbslStringify` interface for logging path clonings.
  template <typename Sink>
  friend void AbslStringify(Sink &sink,
                            const PathProbeSampleInfo &path_probe_sample_info) {
    absl::Format(&sink, "sample_time: %s\n",
                 absl::FormatTime(path_probe_sample_info.sample_time));
    absl::Format(&sink, "path_probes: %s\n",
                 absl::StrJoin(path_probe_sample_info.path_probes, ","));
    absl::Format(&sink, "path_length: %d\n",
                 path_probe_sample_info.path_length);
  }
};

// Represents the path context of the last execution of a block. Specifically,
// the time it was executed (`sample_time`) and the paths leading to its
// execution at that time (`path_probes`). If `path_probes` could be empty,
// which means the paths were not recorded (e.g., they did not include hot join
// blocks).
struct BlockPathInfo {
  PathProbeSampleInfo path_probe_sample_info;
};

// Represents the `BlockPathInfo`s for all blocks of a function.
class FunctionPathInfo {
 public:
  // Constructs a `FunctionPathInfo` for a function with `n_blocks` BBs.
  explicit FunctionPathInfo(int n_blocks) : block_path_info_(n_blocks) {}

  // Sets the path info of BB with index `bb_index` to `block_path_info`.
  void SetPathInfo(int bb_index, BlockPathInfo block_path_info) {
    block_path_info_[bb_index] = std::move(block_path_info);
  }

  // Updates `PathPredInfo::cache_pressure` upon executing `bb_index` at time
  // `sample_time` and under paths associated with `path_probes`. `path_length`
  // is the number of known blocks in the LBR path ending with `bb_index`.
  // `max_icache_penalty_interval` is the maximum interval time for which we
  // account for cache pressure.
  void UpdateCachePressure(int bb_index, absl::Time sample_time,
                           std::vector<BasePathProbe> path_probes,
                           int path_length,
                           absl::Duration max_icache_penalty_interval) {
    PathProbeSampleInfo new_path_probe_sample_info{
        .sample_time = sample_time,
        .path_probes = std::move(path_probes),
        .path_length = path_length};

    BlockPathInfo &bb_path_info = block_path_info_[bb_index];

    if (bb_path_info.path_probe_sample_info.sample_time !=
            absl::InfinitePast() &&
        // We might be processing perfdata files out of order, in which case
        // we skip accounting for cache pressure on the first access.
        sample_time >= bb_path_info.path_probe_sample_info.sample_time) {
      absl::Duration time_lapse =
          sample_time - bb_path_info.path_probe_sample_info.sample_time;
      if (time_lapse < max_icache_penalty_interval) {
        double pressure =
            1.0 - absl::FDivDuration(time_lapse, max_icache_penalty_interval);
        // Update cache pressure for the new path probes.
        for (auto &path_probe : new_path_probe_sample_info.path_probes) {
          if (!bb_path_info.path_probe_sample_info.CouldImply(path_probe)) {
            path_probe.path_node
                ->mutable_path_pred_info()[path_probe.pred_node_bb_index]
                .cache_pressure += pressure;
          }
        }
        // Update cache pressure for the latest visited path probes.
        for (auto &last_path_probe :
             bb_path_info.path_probe_sample_info.path_probes) {
          if (!new_path_probe_sample_info.CouldImply(last_path_probe)) {
            last_path_probe.path_node
                ->mutable_path_pred_info()[last_path_probe.pred_node_bb_index]
                .cache_pressure += pressure;
          }
        }
      }
    }
    bb_path_info.path_probe_sample_info = std::move(new_path_probe_sample_info);
  }

 private:
  std::vector<BlockPathInfo> block_path_info_;
};

// Path trace handler interface provided for `PathTracer` used to trace a path
// within a single function.
class PathTraceHandler {
 public:
  virtual ~PathTraceHandler() = default;

  // Visits the single block corresponding to `flat_bb_index` with sample time
  // `sample_time`.
  virtual void VisitBlock(int flat_bb_index, absl::Time sample_time) = 0;
  // Handles calls to callee functions `call_rets` from the current block.
  virtual void HandleCalls(
      absl::Span<const propeller::CallRetInfo> call_rets) = 0;
  // Handles a return to `bb_handle` from the current block.
  virtual void HandleReturn(const propeller::FlatBbHandle &bb_handle) = 0;
  // Finishes the current path and prepares to start a new path.
  virtual void ResetPath() = 0;
};

// Traces a single intra-function `FlatBbHandleBranchPath` using a
// `PathTraceHandler`.
// Usage:
//   FlatBBHandleBranchPath path = ...
//   ControlFlowGraph *cfg = ...
//   PathTraceHandler handler(...)
//   PathTracer(&cfg, &handler).TracePath(path);
class PathTracer {
 public:
  // Does not take any ownership, and all pointers must refer to valid objects
  // that outlive the one constructed.
  PathTracer(const propeller::ControlFlowGraph *cfg, PathTraceHandler *handler)
      : cfg_(cfg), handler_(handler) {}

  PathTracer(const PathTracer &) = delete;
  PathTracer &operator=(const PathTracer &) = delete;
  PathTracer(PathTracer &&) = delete;
  PathTracer &operator=(PathTracer &&) = delete;

  // Traces `path`.
  void TracePath(const propeller::FlatBbHandleBranchPath &path) &&;

 private:
  // If `from_bb` can fall through to `to_bb`, updates the path tree by mapping
  // blocks from `from_bb` to `to_bb` (excluding the endpoints). Otherwise,
  // cuts the current path by clearing `current_path_probes_` and setting
  // `prev_node_bb_index_` to -1.
  void HandleFallThroughBlocks(std::optional<propeller::FlatBbHandle> from_bb,
                               std::optional<propeller::FlatBbHandle> to_bb,
                               absl::Time sample_time);

 private:
  const propeller::ControlFlowGraph *cfg_;
  PathTraceHandler *handler_;
};

// Analyzes `FlatBBHandleBranchPath`s by mapping into a `ProgramCfg`.
class ProgramCfgPathAnalyzer {
 public:
  // Does not take ownership of any pointer parameters which should all point to
  // valid objects which will outlive the constructed object.
  ProgramCfgPathAnalyzer(
      const propeller::PathProfileOptions *ABSL_NONNULL path_profile_options,
      const propeller::ProgramCfg *ABSL_NONNULL program_cfg,
      propeller::ProgramPathProfile *ABSL_NONNULL program_path_profile)
      : path_profile_options_(path_profile_options),
        hot_threshold_(program_cfg->GetNodeFrequencyThreshold(
            path_profile_options->hot_cutoff_percentile())),
        program_cfg_(program_cfg),
        hot_join_bbs_(program_cfg->GetHotJoinNodes(
            hot_threshold_, /*hot_edge_frequency_threshold=*/1)),
        program_path_profile_(program_path_profile) {}

  ProgramCfgPathAnalyzer(const ProgramCfgPathAnalyzer &) = delete;
  ProgramCfgPathAnalyzer &operator=(const ProgramCfgPathAnalyzer &) = delete;
  ProgramCfgPathAnalyzer(ProgramCfgPathAnalyzer &&) = default;
  ProgramCfgPathAnalyzer &operator=(ProgramCfgPathAnalyzer &&) = default;

  const propeller::ProgramPathProfile &path_profile() const {
    return *program_path_profile_;
  }

  const std::deque<propeller::FlatBbHandleBranchPath> &bb_branch_paths() const {
    return bb_branch_paths_;
  }

  // Stores the paths in `bb_branch_paths` into `bb_branch_paths_`. If the
  // sampled times in `bb_branch_paths_` roughly exceeds
  // `path_profile_options_->max_time_diff_in_path_buffer_millis`, analyzes and
  // purges half of them by calling `ProgramCfgPathAnalyzer::AnalyzePaths`.
  void StoreAndAnalyzePaths(
      absl::Span<const propeller::FlatBbHandleBranchPath> bb_branch_paths);

  // Sorts all paths in `bb_branch_paths_` based on their sample_time. Then
  // analyzes and removes the first `paths_to_analyze` paths and updates
  // `program_path_profile_`. Each path tree represents many paths which share
  // their second block. The shared block corresponds to the root of this
  // tree. Every path node in the tree represents all the program paths
  // which follow the basic block path corresponding to the path from the
  // root. These paths may have different predecessor blocks. The associated
  // path node stores the frequency of the corresponding path given every
  // possible path predecessor block. It also stores the frequency of every
  // call from the corresponding ending block, given every possible path
  // predecessor block. If `paths_to_analyze == std::nullopt` analyzes all
  // paths in `bb_branch_paths_`.
  //
  void AnalyzePaths(std::optional<int> paths_to_analyze);

  // Returns the paths in `bb_branch_paths` which include hot join BBs, in the
  // same order as in the input.
  std::vector<propeller::FlatBbHandleBranchPath> GetPathsWithHotJoinBbs(
      absl::Span<const propeller::FlatBbHandleBranchPath> bb_branch_paths);

  // Returns whether `path` contains any hot join BBs.
  bool HasHotJoinBbs(const propeller::FlatBbHandleBranchPath &path) const;

  // Returns whether the intra-function `path` is from a function with hot join
  // BBs.
  bool IsFromFunctionWithHotJoinBbs(
      const propeller::FlatBbHandleBranchPath &path) const {
    const propeller::FlatBbHandle &first_bb =
        path.branches.front().from_bb.has_value()
            ? *path.branches.front().from_bb
            : *path.branches.front().to_bb;
    return hot_join_bbs_.contains(first_bb.function_index);
  }

 private:
  const propeller::PathProfileOptions *path_profile_options_;
  // CFGNode and CFGEdge frequency threshold to be considered hot.
  int64_t hot_threshold_;
  absl::flat_hash_map<int, FunctionPathInfo> all_function_path_info_;
  const propeller::ProgramCfg *program_cfg_;
  // Hot join basic blocks, stored as a map from function indexes to the set of
  // basic block indices.
  absl::flat_hash_map<int, absl::btree_set<int>> hot_join_bbs_;
  // Paths remaining to be analyzed.
  std::deque<propeller::FlatBbHandleBranchPath> bb_branch_paths_;
  // Program path profile for all functions.
  propeller::ProgramPathProfile *program_path_profile_;
};
}  // namespace propeller
#endif  // PROPELLER_PROGRAM_CFG_PATH_ANALYZER_H_
