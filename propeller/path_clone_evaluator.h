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

#ifndef PROPELLER_PATH_CLONE_EVALUATOR_H_
#define PROPELLER_PATH_CLONE_EVALUATOR_H_

#include <optional>
#include <tuple>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "propeller/cfg.h"
#include "propeller/function_chain_info.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {

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

// Returns the `CfgChange` (including intra- and inter-procedural changes)
// resulting from applying `cloning` to the cfg, or
// absl::Status on error when applying `cloning` is found to be infeasible
// because of conflict with `conflict_edges`.
absl::StatusOr<CfgChangeFromPathCloning> GetCfgChangeForPathCloning(
    const PathCloning &cloning, const ConflictEdges &conflict_edges);

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
// or if its score gain is lower than `min_score`.
absl::StatusOr<EvaluatedPathCloning> EvaluateCloning(
    const CfgBuilder &cfg_builder, const PathCloning &path_cloning,
    const PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options, double min_score,
    const FunctionChainInfo &optimal_chain_info);

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
  // when evaluating path clonings.
  void EvaluateCloningsForSubtree(
      const PathNode &path_tree, int path_length,
      const absl::flat_hash_set<int> &path_preds_in_path,
      std::vector<EvaluatedPathCloning> &clonings);

  // Evaluates all clonings associated with `path_node` which includes paths
  // corresponding to `path_node` with every possible path predecessor and adds
  // the profitable clonings to `clonings`. `path_preds_in_path` is the subset
  // of path predecessor bb indices of the root which have been encountered in
  // the path to `path_tree` (excluding `path_tree` itself). These are filtered
  // out from the predecessor blocks when evaluating path clonings.
  void EvaluateCloningsForPath(
      const PathNode &path_node,
      const absl::flat_hash_set<int> &path_preds_in_path,
      std::vector<EvaluatedPathCloning> &clonings);

 private:
  const ControlFlowGraph &cfg_;
  const PathProfileOptions &path_profile_options_;
  const PropellerCodeLayoutParameters &code_layout_params_;
  const FunctionChainInfo &optimal_chain_info_;
};
}  // namespace propeller
#endif  // PROPELLER_PATH_CLONE_EVALUATOR_H_
