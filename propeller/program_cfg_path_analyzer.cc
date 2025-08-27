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

#include "propeller/program_cfg_path_analyzer.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/cfg.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"

namespace propeller {

void PathTracer::HandleFallThroughBlocks(std::optional<FlatBbHandle> from_bb,
                                         std::optional<FlatBbHandle> to_bb,
                                         absl::Time sample_time) {
  if (!from_bb.has_value()) {
    if (to_bb.has_value())
      handler_->VisitBlock(to_bb->flat_bb_index, sample_time);
    return;
  }
  auto can_fallthrough = [&](int from_bb_index, int to_bb_index) {
    if (from_bb_index > to_bb_index) return false;
    for (int bb_index = from_bb_index; bb_index < to_bb_index; ++bb_index) {
      if (!cfg_->nodes().at(bb_index)->can_fallthrough()) return false;
    }
    return true;
  };
  CHECK(to_bb.has_value());
  CHECK_EQ(from_bb->function_index, to_bb->function_index);
  // If we can't fall through, drop the current paths and restart tracing
  // paths.
  if (!can_fallthrough(from_bb->flat_bb_index, to_bb->flat_bb_index)) {
    handler_->ResetPath();
    // PathTracer::TracePath will visit the destination of the next branch, so
    // we still need to visit the last block in the path here (which is the
    // source of the next branch).
    handler_->VisitBlock(to_bb->flat_bb_index, sample_time);
    return;
  }
  for (int bb_index = from_bb->flat_bb_index + 1;
       bb_index <= to_bb->flat_bb_index; ++bb_index) {
    handler_->VisitBlock(bb_index, sample_time);
  }
}

void PathTracer::TracePath(const FlatBbHandleBranchPath& path) && {
  std::optional<FlatBbHandle> last_to_bb = std::nullopt;
  for (const FlatBbHandleBranch& branch : path.branches) {
    HandleFallThroughBlocks(last_to_bb, branch.from_bb, path.sample_time);
    if (branch.is_callsite()) handler_->HandleCalls(branch.call_rets);
    if (branch.to_bb.has_value()) {
      if (branch.is_callsite()) {
        HandleFallThroughBlocks(branch.from_bb, branch.to_bb, path.sample_time);
      } else {
        handler_->VisitBlock(branch.to_bb->flat_bb_index, path.sample_time);
      }
    }
    last_to_bb = branch.to_bb;
  }
  if (path.returns_to.has_value()) handler_->HandleReturn(*path.returns_to);
}

namespace {
// Traces a single intra-function `BbHandleBranchPath` and maps it to
// `PathNode`s in a path tree.
class CloningPathTraceHandler : public PathTraceHandler {
 public:
  // Does not take any ownership, and all pointers must refer to valid objects
  // that outlive the one constructed.
  CloningPathTraceHandler(const PathProfileOptions* path_profile_options,
                          const ControlFlowGraph* cfg,
                          const absl::btree_set<int>* function_hot_join_bbs,
                          FunctionPathInfo* function_path_info,
                          FunctionPathProfile* function_path_profile)
      : path_profile_options_(path_profile_options),
        cfg_(cfg),
        function_hot_join_bbs_(function_hot_join_bbs),
        function_path_info_(function_path_info),
        function_path_profile_(function_path_profile),
        prev_node_bb_index_(-1) {}

  CloningPathTraceHandler(const CloningPathTraceHandler&) = delete;
  CloningPathTraceHandler& operator=(const CloningPathTraceHandler&) = delete;
  CloningPathTraceHandler(CloningPathTraceHandler&&) = delete;
  CloningPathTraceHandler& operator=(CloningPathTraceHandler&&) = delete;

  // Visits the block with index `bb_index` with sample time `sample_time`
  // and updates the current paths, by adding this block as a child. Also
  // creates a new path starting from this block if needed.
  void VisitBlock(int flat_bb_index, absl::Time sample_time) override {
    UpdateMissingPredPathNode(flat_bb_index);
    if (missing_pred_path_node_ != nullptr) {
      ++missing_pred_path_node_->mutable_path_pred_info()
            .missing_pred_entry.freq;
    }
    ++path_length_;
    std::vector<BasePathProbe> new_path_probes;
    // Extends `path_probe` with `bb_index` and returns if we should continue
    // tracing the extended path. Tracing stops when either we find a cycle in
    // the path or if we reach a block with indirect branch (which we can't
    // clone).
    auto extend_path_and_return_if_should_trace = [&](PathProbe& path_probe) {
      // Stop tracing if the path is looping.
      if (!path_probe.AddToNodesInPath(flat_bb_index)) return false;

      // Insert a child path node associated with this block.
      PathNode& child_path_node =
          *path_probe.path_node()
               ->mutable_children()
               .lazy_emplace(flat_bb_index,
                             [&](const auto& ctor) {
                               ctor(flat_bb_index,
                                    std::make_unique<PathNode>(
                                        flat_bb_index, path_probe.path_node()));
                             })
               ->second;

      // Increment the frequency associated with the child path node.
      ++child_path_node.mutable_path_pred_info()
            .GetOrInsertEntry(path_probe.pred_node_bb_index())
            .freq;

      // Stop tracing if the previous block has an indirect branch. Indirect
      // branches cannot be rewired. Therefore, they can only exist in the last
      // block of the cloning path. Note we still need to update the frequencies
      // of the successors of the indirect-branch block.
      CHECK_NE(prev_node_bb_index_, -1);
      if (cfg_->nodes().at(prev_node_bb_index_)->has_indirect_branch())
        return false;
      // Stop tracing when the path reaches the length threshold.
      if (path_probe.GetPathLength() >=
          path_profile_options_->max_path_length())
        return false;
      // Make this path probe point to the child node (and keep tracing it).
      path_probe.set_path_node(&child_path_node);
      new_path_probes.push_back(path_probe.base_path_probe());
      return true;
    };

    // Extends the current paths with the current block and removes them once
    // they have a cycle or a block with an indirect branch.
    current_path_probes_.erase(
        std::remove_if(current_path_probes_.begin(), current_path_probes_.end(),
                       std::not_fn(extend_path_and_return_if_should_trace)),
        current_path_probes_.end());

    // Create a new path starting from this block if it is a hot join block.
    // We only account for paths with predecessors.
    // Note we do trace a path when the path predecessor has an indirect branch
    // even though the path with that predecessor is not cloneable. This is to
    // ensure that we have all the path frequencies for a join block in case
    // it has other path predecessors with no indirect branches.
    if (prev_node_bb_index_ != -1 &&
        function_hot_join_bbs_->contains(flat_bb_index)) {
      // Add the new path tree rooted at this node.
      PathNode& path_node =
          function_path_profile_->GetOrInsertPathTree(flat_bb_index);
      // Increment the frequency of the root (given the predecessor block).
      ++path_node.mutable_path_pred_info().entries[prev_node_bb_index_].freq;
      // Start tracking this path.
      current_path_probes_.emplace_back(&path_node, prev_node_bb_index_);
      new_path_probes.push_back(current_path_probes_.back().base_path_probe());
    }
    // Even if no path probes exist, we still need to update the cache pressure
    // for the block.
    function_path_info_->UpdateCachePressure(
        flat_bb_index, sample_time, std::move(new_path_probes), path_length_,
        absl::Milliseconds(
            path_profile_options_->max_icache_penalty_interval_millis()));
    prev_node_bb_index_ = flat_bb_index;
  }

  // Inserts `calls` into latest path nodes tracked by `current_path_probes_`.
  void HandleCalls(absl::Span<const CallRetInfo> call_rets) override {
    if (missing_pred_path_node_ != nullptr) {
      for (const auto& call_ret : call_rets) {
        // Skips call-returns from unknown code (library functions, etc.).
        if (!call_ret.callee.has_value() && !call_ret.return_bb.has_value())
          continue;
        ++missing_pred_path_node_->mutable_path_pred_info()
              .missing_pred_entry.call_freqs[call_ret];
      }
    }
    for (PathProbe& path_probe : current_path_probes_) {
      absl::flat_hash_map<CallRetInfo, int>& call_freqs_for_pred =
          path_probe.path_node()
              ->mutable_path_pred_info()
              .GetOrInsertEntry(path_probe.pred_node_bb_index())
              .call_freqs;
      for (const auto& call_ret : call_rets) {
        // Skips call-returns from unknown code (library functions, etc.).
        if (!call_ret.callee.has_value() && !call_ret.return_bb.has_value())
          continue;
        ++call_freqs_for_pred[call_ret];
      }
    }
  }

  void HandleReturn(const FlatBbHandle& bb_handle) override {
    if (missing_pred_path_node_ != nullptr) {
      ++missing_pred_path_node_->mutable_path_pred_info()
            .missing_pred_entry.return_to_freqs[bb_handle];
    }
    for (PathProbe& path_probe : current_path_probes_) {
      ++path_probe.path_node()
            ->mutable_path_pred_info()
            .entries[path_probe.pred_node_bb_index()]
            .return_to_freqs[{.function_index = bb_handle.function_index,
                              .flat_bb_index = bb_handle.flat_bb_index}];
    }
  }

  // Cuts and disconnects the current paths by resetting the internal states of
  // the path visitor, including `current_path_probes_`, `prev_node_bb_index`,
  // `path_length_`, and `missing_pred_path_node_`.
  void ResetPath() override {
    missing_pred_path_node_ = nullptr;
    current_path_probes_.clear();
    prev_node_bb_index_ = -1;
    path_length_ = 0;
  }

 private:
  // Updates `missing_pred_path_node_` upon visiting a new block with flat bb
  // index `flat_bb_index`.
  void UpdateMissingPredPathNode(int flat_bb_index) {
    // If `missing_pred_path_node_` is already set, we trace the path through
    // its child node with corresponding to `flat_bb_index`.
    if (missing_pred_path_node_ != nullptr) {
      // Stop tracking the missing predecessor path node once we reach the
      // cloning path length threshold.
      if (path_length_ >= path_profile_options_->max_path_length()) {
        missing_pred_path_node_ = nullptr;
      } else {
        // We don't need to check for loops here. Missing predecessor path
        // nodes for looping paths will be created, but they won't be considered
        // when applying the cloning since we only clone paths with no loops.
        missing_pred_path_node_ =
            missing_pred_path_node_->mutable_children()
                .lazy_emplace(
                    flat_bb_index,
                    [&](const auto& ctor) {
                      ctor(flat_bb_index,
                           std::make_unique<PathNode>(flat_bb_index,
                                                      missing_pred_path_node_));
                    })
                ->second.get();
      }
    } else if (path_length_ == 0 && flat_bb_index != 0) {
      // If the path length is 0, we are at the very first block of the path.
      // We create a new path tree rooted at this node unless this is the
      // function entry block, which means there is no path predecessor.
      missing_pred_path_node_ =
          &function_path_profile_->GetOrInsertPathTree(flat_bb_index);
    }
  }

  const PathProfileOptions* path_profile_options_;
  const ControlFlowGraph* cfg_;
  // At each point during the tracing of a path, we will potentially be tracking
  // multiple paths (all of which end at the visited block but start from
  // different hot join blocks).
  std::vector<PathProbe> current_path_probes_;
  // Hot join block (indices) of `cfg_`.
  const absl::btree_set<int>* function_hot_join_bbs_;
  FunctionPathInfo* function_path_info_;
  // Path tree corresponding to ‍`cfg_`, stored as a map from block indices to
  // their path tree root.
  FunctionPathProfile* function_path_profile_;
  // Previous node's bb_index when traversing the path (Should be -1 before path
  // traversal).
  int prev_node_bb_index_;
  // Length of the full visited path in terms of number of blocks. This is
  // incremented for each block visited during the path traversal.
  int path_length_ = 0;
  // The path node corresponding to the current path with missing path
  // predecessor which starts from the very first block of the path. This is
  // used to populate the missing path predecessor info for the paths, which
  // will later be used to drop edges weights for paths with missing path
  // predecessors.
  PathNode* missing_pred_path_node_ = nullptr;
};
}  // namespace

void ProgramCfgPathAnalyzer::AnalyzePaths(std::optional<int> paths_to_analyze) {
  int num_paths = paths_to_analyze.value_or(bb_branch_paths_.size());
  CHECK_LE(num_paths, bb_branch_paths_.size());
  absl::c_stable_sort(bb_branch_paths_, [](const FlatBbHandleBranchPath& lhs,
                                           const FlatBbHandleBranchPath& rhs) {
    return lhs.sample_time < rhs.sample_time;
  });
  for (int i = 0; i < num_paths; ++i) {
    const FlatBbHandleBranchPath& path = bb_branch_paths_[i];
    if (i != 0) CHECK_GE(path.sample_time, bb_branch_paths_[i - 1].sample_time);
    if (!IsFromFunctionWithHotJoinBbs(path)) continue;
    int path_function_index =
        path.branches.front().from_bb.has_value()
            ? path.branches.front().from_bb->function_index
            : path.branches.front().to_bb->function_index;
    const ControlFlowGraph* cfg =
        program_cfg_->GetCfgByIndex(path_function_index);
    CHECK_NE(cfg, nullptr);
    FunctionPathInfo& function_path_info =
        all_function_path_info_
            .try_emplace(path_function_index, cfg->nodes().size())
            .first->second;

    if (!path.branches.front().to_bb.has_value()) {
      CHECK_EQ(path.branches.size(), 1)
          << "Path with unknown block in the middle: " << path;
      function_path_info.UpdateCachePressure(
          path.branches.front().from_bb->flat_bb_index, path.sample_time, {},
          /*path_length=*/1,
          absl::Milliseconds(
              path_profile_options_->max_icache_penalty_interval_millis()));
      continue;
    }
    CloningPathTraceHandler handler(
        path_profile_options_, cfg, &hot_join_bbs_.at(path_function_index),
        &function_path_info,
        &program_path_profile_->GetProfileForFunctionIndex(
            path_function_index));
    PathTracer(cfg, &handler).TracePath(path);
  }
  bb_branch_paths_.erase(bb_branch_paths_.begin(),
                         bb_branch_paths_.begin() + num_paths);
}

void ProgramCfgPathAnalyzer::StoreAndAnalyzePaths(
    absl::Span<const FlatBbHandleBranchPath> bb_branch_paths) {
  absl::c_move(bb_branch_paths, std::back_inserter(bb_branch_paths_));
  if (!bb_branch_paths_.empty() &&
      bb_branch_paths_.back().sample_time -
              bb_branch_paths_.front().sample_time >
          absl::Milliseconds(
              path_profile_options_->max_time_diff_in_path_buffer_millis())) {
    AnalyzePaths(bb_branch_paths_.size() / 2);
  }
}
}  // namespace propeller
