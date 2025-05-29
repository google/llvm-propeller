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

#ifndef PROPELLER_PATH_CLONE_EVALUATOR_H_
#define PROPELLER_PATH_CLONE_EVALUATOR_H_

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "propeller/cfg.h"
#include "propeller/function_chain_info.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {

// Helper class for constructing a `CfgChangeFromPathCloning` for a given
// `PathCloning`. This class should be used as:
//
// CfgChangeBuilder cfg_change_builder(cloning, conflict_edges,
//                                      function_path_profile);
// cfg_change = std::move(cfg_change_builder).Build();
//
// where `cloning` is the `PathCloning` to apply, `conflict_edges` is the
// `ConflictEdges` from the previously applied clonings, and
// `function_path_profile` is the path profile of the corresponding function.

class CfgChangeBuilder {
 public:
  // Does not take ownership of any of its arguments which should all point to
  // valid objects which will outlive the constructed object. `path_cloning` is
  // the `PathCloning` to apply. `conflict_edges` is the `ConflictEdges` from
  // the previously applied clonings. `function_path_profile` is the path
  // profile of the corresponding function.
  CfgChangeBuilder(const PathCloning &cloning ABSL_ATTRIBUTE_LIFETIME_BOUND,
                   const ConflictEdges &conflict_edges
                       ABSL_ATTRIBUTE_LIFETIME_BOUND,
                   const FunctionPathProfile &function_path_profile
                       ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : cloning_(cloning),
        conflict_edges_(conflict_edges),
        function_path_profile_(function_path_profile),
        path_from_root_(cloning.path_node->path_from_root()),
        cfg_change_({.path_pred_bb_index = cloning.path_pred_bb_index}) {}

  CfgChangeBuilder(const CfgChangeBuilder &) = delete;
  CfgChangeBuilder &operator=(const CfgChangeBuilder &) = delete;
  CfgChangeBuilder(CfgChangeBuilder &&) = delete;
  CfgChangeBuilder &operator=(CfgChangeBuilder &&) = delete;

  // Returns the `CfgChange` (including intra- and inter-procedural changes)
  // resulting from applying `cloning_` to the cfg, or
  // absl::Status on error when applying `cloning` is found to be infeasible
  // due to conflict with `conflict_edges`.
  absl::StatusOr<CfgChangeFromPathCloning> Build() &&;

 private:
  // The status of the block currently being visited in the cloning path.
  enum class PathVisitStatus {
    kPred,      // Visiting the path predecessor block.
    kMiddle,    // Visiting a middle block in the cloning path (after the path
                // predecessor and before the last block).
    kLast,      // Visiting the last block in the cloning path.
    kFinished,  // Finished visiting the cloning path.
  };

  PathVisitStatus CurrentPathVisitStatus() const {
    if (current_index_in_path_ == -1) return PathVisitStatus::kPred;
    if (current_index_in_path_ == path_from_root_.size() - 1)
      return PathVisitStatus::kLast;
    if (current_index_in_path_ >= path_from_root_.size())
      return PathVisitStatus::kFinished;
    return PathVisitStatus::kMiddle;
  }

  // Visits the current block in the cloning path and updates `cfg_change_` with
  // the changes. Returns `absl::FailedPreconditionError` if the cloning is
  // found to be invalid based on `conflict_edges_`. Finally, moves to the next
  // block in the cloning path by incrementing `current_index_in_path_`.
  absl::Status VisitNext();

  // Adds an intra-function edge reroute to `cfg_change_`. Returns
  // `absl::FailedPreconditionError` if the reroute conflicts with a previously
  // applied cloning.
  absl::Status AddEdgeReroute(
      CfgChangeFromPathCloning::IntraEdgeReroute edge_reroute);

  // Adds an inter-function edge reroute to `cfg_change_`.
  void AddEdgeReroute(CfgChangeFromPathCloning::InterEdgeReroute edge_reroute) {
    cfg_change_.inter_edge_reroutes.push_back(std::move(edge_reroute));
  }

  // Updates `current_paths_with_missing_pred_` with paths with missing
  // predecessor at `next_bb_index`, and adds them to
  // `cfg_change_.paths_to_drop`.
  void UpdatePathsWithMissingPred(int next_bb_index);

  const PathCloning &cloning_;
  const ConflictEdges &conflict_edges_;
  const FunctionPathProfile &function_path_profile_;
  // The path associated with `cloning_` (excluding
  // `cloning_.path_pred_bb_index`).
  const std::vector<const PathNode *absl_nonnull> path_from_root_;
  // Index of the current block to be visited in `path_from_root_`. -1 means the
  // path predecessor block.
  int current_index_in_path_ = -1;
  // Tracks paths with missing path predecessor at the currently visited block
  // in the cloning path. These paths start from different blocks in the cloning
  // path and end at the currently visited block. The outgoing edge weights from
  // these paths must be dropped when applying the cloning.
  std::vector<const PathNode *absl_nonnull> current_paths_with_missing_pred_;
  // The CFG change which will be constructed and returned by `Build()`.
  CfgChangeFromPathCloning cfg_change_;
};

// Extracts and returns a vector of initial chains for `cfg` for applying
// `cfg_change` based on layout information in `chain_info`. Every two adjacent
// blocks A and B are placed consecutively in the same chain/bundle iff
//   1. they form a fallthrough in `chain_info`. Which means they are placed
//      consecutively in the layout and there is `kBranchOrFallthrough` edge
//      between the two blocks in the direction of the layout, and
//   2. `cfg_change.intra_edge_reroutes` contains neither A nor B.
std::vector<FunctionChainInfo::BbChain> GetInitialChains(
    const ControlFlowGraph &cfg, const FunctionChainInfo &chain_info,
    const CfgChangeFromPathCloning &cfg_change);

// Represents a potentially evaluated path cloning.
struct EvaluatedPathCloning {
  PathCloning path_cloning;
  // The layout score achieved from applying `path_cloning`. nullopt if the
  // cloning has not been evaluated yet (only used in tests).
  std::optional<double> score;
  // The CFG change resulting from applying `path_cloning`.
  CfgChangeFromPathCloning cfg_change;

  bool operator==(const EvaluatedPathCloning &other) const {
    return score == other.score && path_cloning == other.path_cloning;
  }
  bool operator!=(const EvaluatedPathCloning &other) const {
    return !(*this == other);
  }

  bool operator<(const EvaluatedPathCloning &other) const {
    return std::forward_as_tuple(score, path_cloning) <
           std::forward_as_tuple(other.score, other.path_cloning);
  }

  bool operator>(const EvaluatedPathCloning &other) const {
    return other < *this;
  }

  // Implementation of the `AbslStringify` interface for logging scored path
  // clonings.
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const EvaluatedPathCloning &e);
};

template <typename Sink>
void AbslStringify(Sink &sink, const EvaluatedPathCloning &e) {
  absl::Format(&sink, "[cloning: %v, score: %s]", e.path_cloning,
               e.score.has_value() ? absl::StrCat(*e.score) : "nullopt");
}

// Evaluates `path_cloning` for `cfg` and returns the evaluated path cloning.
// Returns `absl::kFailedPrecondition` if `path_cloning` is infeasible to apply
// or if its score gain is lower than `min_score`. `function_path_profile` is
// the path profile of the corresponding function, and its missing path
// predecessor info is used to drop the edge weights which cannot be confidently
// rerouted.
absl::StatusOr<EvaluatedPathCloning> EvaluateCloning(
    const CfgBuilder &cfg_builder, const PathCloning &path_cloning,
    const PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options, double min_score,
    const FunctionChainInfo &optimal_chain_info,
    const FunctionPathProfile &function_path_profile
        ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Evaluates and returns all applicable and profitable clonings in
// `program_path_profile` with `code_layout_params` and `path_profile_options`.
// Returns these clonings in a map keyed by the function index of the associated
// CFG.
absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>> EvaluateAllClonings(
    const ProgramCfg *program_cfg,
    const ProgramPathProfile *program_path_profile,
    const PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options);

// Evaluates all PathClonings in a path tree associated with a single CFG.
// Example usage:
//   std::vector<PathCloning> clonings;
//   PathTreeCloneEvaluator(cfg,
//                          optimal_chain_info,
//                          path_profile_options,
//                          code_layout_params).EvaluateCloningsForSubtree(
//                                                path_tree,
//                                                clonings);
class PathTreeCloneEvaluator {
 public:
  // Does not take ownership of any of its arguments which should all point
  // to valid objects which will outlive the constructed object.
  PathTreeCloneEvaluator(
      const ControlFlowGraph *absl_nonnull cfg,
      const FunctionChainInfo *absl_nonnull optimal_chain_info,
      const PathProfileOptions *absl_nonnull path_profile_options,
      const PropellerCodeLayoutParameters *absl_nonnull code_layout_params)
      : cfg_(*cfg),
        path_profile_options_(*path_profile_options),
        code_layout_params_(*code_layout_params),
        optimal_chain_info_(*optimal_chain_info) {}

  // Evaluates all clonings in `path_tree` and inserts the scored clonings in
  // `clonings`. `path_length` must be provided as the length of the path
  // to `path_tree` from its root (number of nodes in the path from root
  // including `path_tree` itself). This should be 1 for the root.
  // `path_preds_in_path` is the subset of path predecessor bb indices of the
  // root which have been encountered in the path to `path_tree` (excluding
  // `path_tree` itself). These are filtered out from the predecessor blocks
  // when evaluating path clonings. `function_path_profile` is the path profile
  // of the corresponding function.
  void EvaluateCloningsForSubtree(
      const PathNode &path_tree, int path_length,
      const absl::flat_hash_set<int> &path_preds_in_path,
      std::vector<EvaluatedPathCloning> &clonings,
      const FunctionPathProfile &function_path_profile);

  // Evaluates all clonings associated with `path_node` which includes paths
  // corresponding to `path_node` with every possible path predecessor and adds
  // the profitable clonings to `clonings`. `path_preds_in_path` is the subset
  // of path predecessor bb indices of the root which have been encountered in
  // the path to `path_tree` (excluding `path_tree` itself). These are filtered
  // out from the predecessor blocks when evaluating path clonings.
  // `function_path_profile` is the path profile of the corresponding function.
  void EvaluateCloningsForPath(
      const PathNode &path_node,
      const absl::flat_hash_set<int> &path_preds_in_path,
      std::vector<EvaluatedPathCloning> &clonings,
      const FunctionPathProfile &function_path_profile);

 private:
  const ControlFlowGraph &cfg_;
  const PathProfileOptions &path_profile_options_;
  const PropellerCodeLayoutParameters &code_layout_params_;
  const FunctionChainInfo &optimal_chain_info_;
};
}  // namespace propeller
#endif  // PROPELLER_PATH_CLONE_EVALUATOR_H_
