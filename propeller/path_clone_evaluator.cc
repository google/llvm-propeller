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

#include "propeller/path_clone_evaluator.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "propeller/bb_handle.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"
#include "propeller/code_layout.h"
#include "propeller/function_chain_info.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/status_macros.h"

namespace propeller {

namespace {
// Returns the penalty for cloning `path_cloning`. The total penalty is the
// base penalty (relative to the cloned size) plus the interval-based cache
// pressure penalty.
double GetClonePenalty(const ControlFlowGraph &cfg,
                       const PathProfileOptions &path_profile_options,
                       const PathCloning &path_cloning) {
  double total_icache_penalty = 0;
  double total_base_penalty = 0;
  for (const PathNode *pn = path_cloning.path_node; pn != nullptr;
       pn = pn->parent()) {
    int bb_size = cfg.nodes()[pn->node_bb_index()]->size();
    auto it =
        pn->path_pred_info().entries.find(path_cloning.path_pred_bb_index);
    if (it != pn->path_pred_info().entries.end())
      total_icache_penalty += it->second.cache_pressure * bb_size;
    total_base_penalty += bb_size;
  }
  return total_icache_penalty * path_profile_options.icache_penalty_factor() +
         total_base_penalty * path_profile_options.base_penalty_factor();
}
}  // namespace

absl::StatusOr<CfgChangeFromPathCloning> CfgChangeBuilder::Build() && {
  // Construct the CfgChangeFromPathCloning by tracing the cloning path.
  while (CurrentPathVisitStatus() != PathVisitStatus::kFinished) {
    RETURN_IF_ERROR(VisitNext());
  }
  // Record edge changes associated with returns from the last block in the
  // cloning path.
  const auto &return_to_freqs = path_from_root_.back()
                                    ->path_pred_info()
                                    .entries.at(cloning_.path_pred_bb_index)
                                    .return_to_freqs;
  for (const auto &[bb_handle, freq] : return_to_freqs) {
    AddEdgeReroute(CfgChangeFromPathCloning::InterEdgeReroute{
        .src_function_index = cloning_.function_index,
        .sink_function_index = bb_handle.function_index,
        .src_bb_index = path_from_root_.back()->node_bb_index(),
        .sink_bb_index = bb_handle.flat_bb_index,
        .src_is_cloned = true,
        .sink_is_cloned = false,
        .kind = CFGEdgeKind::kRet,
        .weight = freq});
  }
  cfg_change_.path_to_clone.reserve(path_from_root_.size());
  for (const PathNode *path_node : path_from_root_) {
    cfg_change_.path_to_clone.push_back(path_node->node_bb_index());
  }
  return std::move(cfg_change_);
}

absl::Status CfgChangeBuilder::AddEdgeReroute(
    CfgChangeFromPathCloning::IntraEdgeReroute edge_reroute) {
  if (edge_reroute.src_is_cloned) {
    if (conflict_edges_.path_pred_edges.contains(
            {.from_bb_index = edge_reroute.src_bb_index,
             .to_bb_index = edge_reroute.sink_bb_index})) {
      // If any of these affected edges were found to have been the path
      // predecessor edge of some cloning previously applied, it would
      // conflict with applying that cloning. So we fail in such cases.
      return absl::FailedPreconditionError(
          "Edge is the path predecessor of some cloning previously "
          "applied.");
    }
  } else {
    if (conflict_edges_.affected_edges.contains(
            {.from_bb_index = edge_reroute.src_bb_index,
             .to_bb_index = edge_reroute.sink_bb_index})) {
      // We can't confidently apply a cloning if its path predecessor edge has
      // been affected by the clonings applied so far.
      return absl::FailedPreconditionError(
          "path predecessor edge has been affected by the currently applied "
          "clonings.");
    }
  }
  cfg_change_.intra_edge_reroutes.push_back(std::move(edge_reroute));
  return absl::OkStatus();
}

void CfgChangeBuilder::UpdatePathsWithMissingPred(int next_bb_index) {
  std::vector<const PathNode *absl_nonnull> new_paths_with_missing_pred;
  new_paths_with_missing_pred.reserve(current_paths_with_missing_pred_.size());
  for (const PathNode *path_with_missing_pred :
       current_paths_with_missing_pred_) {
    const PathNode *next_path_with_missing_pred =
        path_with_missing_pred->GetChild(next_bb_index);
    if (next_path_with_missing_pred == nullptr ||
        !next_path_with_missing_pred->path_pred_info()
             .missing_pred_entry.freq) {
      continue;
    }
    new_paths_with_missing_pred.push_back(next_path_with_missing_pred);
  }
  // If there are any paths with missing path predecessor at this block, they
  // must be recorded.
  const PathNode *new_path_with_missing_pred =
      function_path_profile_.GetPathTree(next_bb_index);
  if (new_path_with_missing_pred != nullptr &&
      new_path_with_missing_pred->path_pred_info().missing_pred_entry.freq) {
    new_paths_with_missing_pred.push_back(new_path_with_missing_pred);
  }
  current_paths_with_missing_pred_ = std::move(new_paths_with_missing_pred);
  for (const PathNode *path_with_missing_pred :
       current_paths_with_missing_pred_) {
    cfg_change_.paths_to_drop.push_back(path_with_missing_pred);
  }
}

absl::Status CfgChangeBuilder::VisitNext() {
  auto current_path_visit_status = CurrentPathVisitStatus();
  CHECK(current_path_visit_status != PathVisitStatus::kFinished);
  int current_bb_index =
      current_path_visit_status == PathVisitStatus::kPred
          ? cloning_.path_pred_bb_index
          : path_from_root_[current_index_in_path_]->node_bb_index();

  std::optional<int> next_bb_index =
      current_path_visit_status == PathVisitStatus::kLast
          ? std::nullopt
          : std::optional<int>(
                path_from_root_[current_index_in_path_ + 1]->node_bb_index());

  if (current_path_visit_status != PathVisitStatus::kLast) {
    const PathNode &next_path_node =
        *path_from_root_[current_index_in_path_ + 1];
    const PathPredInfoEntry *next_path_pred_entry =
        next_path_node.path_pred_info().GetEntry(cloning_.path_pred_bb_index);
    CHECK_NE(next_path_pred_entry, nullptr)
        << "Path is unreachable via the predecessor block: "
        << cloning_.path_pred_bb_index
        << " at path: " << next_path_node.path_from_root();
    // Record that the control flow from the previous block in the path must be
    // reroute via the clone.
    RETURN_IF_ERROR(AddEdgeReroute(CfgChangeFromPathCloning::IntraEdgeReroute{
        .src_bb_index = current_bb_index,
        .sink_bb_index = next_path_node.node_bb_index(),
        .src_is_cloned = current_path_visit_status != PathVisitStatus::kPred,
        .sink_is_cloned = true,
        .kind = CFGEdgeKind::kBranchOrFallthough,
        .weight = next_path_pred_entry->freq}));
  }

  if (current_path_visit_status != PathVisitStatus::kPred) {
    UpdatePathsWithMissingPred(current_bb_index);
    const PathNode *current_path_node = path_from_root_[current_index_in_path_];
    const PathPredInfoEntry &current_path_pred_entry =
        current_path_node->path_pred_info().entries.at(
            cloning_.path_pred_bb_index);
    // Record inter-function edge changes.
    for (const auto &[call_ret, freq] : current_path_pred_entry.call_freqs) {
      if (call_ret.callee.has_value()) {
        AddEdgeReroute(CfgChangeFromPathCloning::InterEdgeReroute{
            .src_function_index = cloning_.function_index,
            .sink_function_index = *call_ret.callee,
            .src_bb_index = current_bb_index,
            .sink_bb_index = 0,
            .src_is_cloned = true,
            .sink_is_cloned = false,
            .kind = CFGEdgeKind::kCall,
            .weight = freq});
      }
      if (call_ret.return_bb.has_value()) {
        AddEdgeReroute(CfgChangeFromPathCloning::InterEdgeReroute{
            .src_function_index = call_ret.return_bb->function_index,
            .sink_function_index = cloning_.function_index,
            .src_bb_index = call_ret.return_bb->flat_bb_index,
            .sink_bb_index = current_bb_index,
            .src_is_cloned = false,
            .sink_is_cloned = true,
            .kind = CFGEdgeKind::kRet,
            .weight = freq});
      }
    }
    // Visit the child edges from this clone to record changes in their
    // weights.
    for (const auto &[child_bb_id, child_path_node] :
         current_path_node->children()) {
      // Rerouting the in-path edge is already done above. Here, we reroute
      // other outgoing edges from the path.
      if (next_bb_index.has_value() && child_bb_id == *next_bb_index) {
        continue;
      }
      const PathPredInfoEntry *child_entry =
          child_path_node->path_pred_info().GetEntry(
              cloning_.path_pred_bb_index);
      if (child_entry == nullptr) continue;

      // Record that the outgoing control flow of the path to the original
      // nodes must be rerouted via the clone nodes.
      RETURN_IF_ERROR(AddEdgeReroute(CfgChangeFromPathCloning::IntraEdgeReroute{
          .src_bb_index = current_bb_index,
          .sink_bb_index = child_bb_id,
          .src_is_cloned = true,
          .sink_is_cloned = false,
          .kind = CFGEdgeKind::kBranchOrFallthough,
          .weight = child_entry->freq}));
    }
  }
  ++current_index_in_path_;
  return absl::OkStatus();
}

absl::StatusOr<EvaluatedPathCloning> EvaluateCloning(
    const CfgBuilder &cfg_builder, const PathCloning &path_cloning,
    const PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options, double min_score,
    const FunctionChainInfo &optimal_chain_info,
    const FunctionPathProfile &function_path_profile) {
  CHECK(!code_layout_params.call_chain_clustering());
  CHECK(!code_layout_params.inter_function_reordering());
  CHECK_EQ(optimal_chain_info.function_index,
           cfg_builder.cfg().function_index());
  ASSIGN_OR_RETURN(CfgChangeFromPathCloning new_cfg_change,
                   CfgChangeBuilder(path_cloning, cfg_builder.conflict_edges(),
                                    function_path_profile)
                       .Build());
  // To make a fair evaluation, we need to drop the paths with missing
  // predecessors for both the original and cloned CFGs. So we first build a
  // CFG with only the paths with missing predecessors dropped.
  CfgBuilder cfg_builder_for_dropping_paths_with_missing_pred =
      cfg_builder.Clone();
  cfg_builder_for_dropping_paths_with_missing_pred.AddCfgChange(
      {.paths_to_drop = new_cfg_change.paths_to_drop});

  std::unique_ptr<ControlFlowGraph> cfg_with_paths_dropped =
      std::move(cfg_builder_for_dropping_paths_with_missing_pred).Build();
  FunctionChainInfo paths_dropped_chain_info =
      CodeLayout(code_layout_params, {cfg_with_paths_dropped.get()},
                 {{cfg_builder.cfg().function_index(),
                   GetInitialChains(*cfg_with_paths_dropped, optimal_chain_info,
                                    new_cfg_change)}})
          .OrderAll()
          .front();

  CfgBuilder cfg_builder_for_cloning = cfg_builder.Clone();
  cfg_builder_for_cloning.AddCfgChange(new_cfg_change);
  std::unique_ptr<ControlFlowGraph> cfg_with_cloning =
      std::move(cfg_builder_for_cloning).Build();
  FunctionChainInfo clone_chain_info =
      CodeLayout(code_layout_params, {cfg_with_cloning.get()},
                 {{cfg_builder.cfg().function_index(),
                   GetInitialChains(*cfg_with_cloning, optimal_chain_info,
                                    new_cfg_change)}})
          .OrderAll()
          .front();
  double score_gain =
      clone_chain_info.optimized_score.intra_score -
      paths_dropped_chain_info.optimized_score.intra_score -
      GetClonePenalty(cfg_builder.cfg(), path_profile_options, path_cloning);
  if (score_gain < min_score) {
    return absl::FailedPreconditionError(
        absl::StrCat("Cloning is not acceptable with score gain: ", score_gain,
                     " < ", min_score));
  }
  return EvaluatedPathCloning{.path_cloning = std::move(path_cloning),
                              .score = score_gain,
                              .cfg_change = std::move(new_cfg_change)};
}

void PathTreeCloneEvaluator::EvaluateCloningsForSubtree(
    const PathNode &path_tree, int path_length,
    const absl::flat_hash_set<int> &path_preds_in_path,
    std::vector<EvaluatedPathCloning> &clonings,
    const FunctionPathProfile &function_path_profile) {
  if (path_tree.parent() == nullptr)
    CHECK_EQ(path_length, 1) << "path_length must be 1 for root.";
  if (path_length > path_profile_options_.max_path_length()) return;
  // Cloning within this subtree won't be profitable if there is only one
  // possible path predecessor.
  if (path_tree.path_pred_info().entries.size() < 2) return;
  bool has_indirect_branch =
      cfg_.nodes().at(path_tree.node_bb_index())->has_indirect_branch();

  if (has_indirect_branch &&
      !path_profile_options_.clone_indirect_branch_blocks()) {
    return;
  }

  std::optional<absl::flat_hash_set<int>> updated_path_preds_in_path;
  if (path_tree.path_pred_info().entries.contains(path_tree.node_bb_index())) {
    updated_path_preds_in_path = path_preds_in_path;
    updated_path_preds_in_path->insert(path_tree.node_bb_index());
  }
  const absl::flat_hash_set<int> &new_path_preds_in_path =
      updated_path_preds_in_path.value_or(path_preds_in_path);
  // Skip evaluating the rest of the subtree if all possible path predecessors
  // are in the path.
  if (path_tree.path_pred_info().entries.size() ==
      new_path_preds_in_path.size())
    return;

  EvaluateCloningsForPath(path_tree, new_path_preds_in_path, clonings,
                          function_path_profile);

  // We can't clone a path if it has an intermediate blocks with indirect
  // branches as they can't be rewired.
  if (has_indirect_branch) return;

  for (auto &[child_bb_index, child_path_node] : path_tree.children()) {
    CHECK_NE(child_path_node, nullptr);
    EvaluateCloningsForSubtree(*child_path_node, path_length + 1,
                               new_path_preds_in_path, clonings,
                               function_path_profile);
  }
}

void PathTreeCloneEvaluator::EvaluateCloningsForPath(
    const PathNode &path_node,
    const absl::flat_hash_set<int> &path_preds_in_path,
    std::vector<EvaluatedPathCloning> &clonings,
    const FunctionPathProfile &function_path_profile) {
  bool is_return_block =
      cfg_.nodes().at(path_node.node_bb_index())->has_return();
  if (path_node.children().size() < 2 && !is_return_block) {
    return;
  }
  for (const auto &[pred_bb_index, path_pred_info_entry] :
       path_node.path_pred_info().entries) {
    // We can't clone a path when the path predecessor has an indirect branch as
    // it can't be rewired.
    if (cfg_.nodes().at(pred_bb_index)->has_indirect_branch()) continue;
    // We can't clone a path if its path predecessor is in the (cloned) path
    // as well as the path predecessor edge may be double counted.
    if (path_preds_in_path.contains(pred_bb_index)) continue;
    if (!is_return_block &&
        path_node.GetTotalChildrenFreqForPathPred(pred_bb_index) <
            path_profile_options_.min_flow_ratio() *
                path_pred_info_entry.freq) {
      continue;
    }
    PathCloning cloning = {.path_node = &path_node,
                           .function_index = cfg_.function_index(),
                           .path_pred_bb_index = pred_bb_index};
    absl::StatusOr<EvaluatedPathCloning> evaluated_cloning = EvaluateCloning(
        CfgBuilder(&cfg_), cloning, code_layout_params_, path_profile_options_,
        path_profile_options_.min_initial_cloning_score(), optimal_chain_info_,
        function_path_profile);
    if (!evaluated_cloning.ok()) continue;
    clonings.push_back(std::move(*std::move(evaluated_cloning)));
  }
}

absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>> EvaluateAllClonings(
    const ProgramCfg *program_cfg,
    const ProgramPathProfile *program_path_profile,
    const PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options) {
  CHECK(!code_layout_params.call_chain_clustering());
  CHECK(!code_layout_params.inter_function_reordering());
  LOG(INFO) << "Evaluating clonings...";
  absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>>
      cloning_scores_by_function_index;
  for (const auto &[function_index, function_path_profile] :
       program_path_profile->path_profiles_by_function_index()) {
    const ControlFlowGraph *cfg = program_cfg->GetCfgByIndex(function_index);
    CHECK_NE(cfg, nullptr);
    FunctionChainInfo fast_response_original_optimal_chain_info =
        CodeLayout(code_layout_params, {cfg},
                   /*initial_chains=*/{})
            .OrderAll()
            .front();
    auto &clonings = cloning_scores_by_function_index[function_index];
    for (const auto &[root_bb_index, path_tree] :
         function_path_profile.path_trees_by_root_bb_index()) {
      PathTreeCloneEvaluator(cfg, &fast_response_original_optimal_chain_info,
                             &path_profile_options, &code_layout_params)
          .EvaluateCloningsForSubtree(*path_tree, /*path_length=*/1, {},
                                      clonings, function_path_profile);
    }
  }
  return cloning_scores_by_function_index;
}

std::vector<FunctionChainInfo::BbChain> GetInitialChains(
    const ControlFlowGraph &cfg, const FunctionChainInfo &chain_info,
    const CfgChangeFromPathCloning &cfg_change) {
  CHECK_EQ(cfg.function_index(), chain_info.function_index);
  absl::flat_hash_set<int> bb_indices;
  for (const auto &intra_edge_reroute : cfg_change.intra_edge_reroutes) {
    bb_indices.insert(intra_edge_reroute.src_bb_index);
    bb_indices.insert(intra_edge_reroute.sink_bb_index);
  }
  std::vector<FunctionChainInfo::BbChain> all_chains;
  for (const FunctionChainInfo::BbChain &bb_chain : chain_info.bb_chains) {
    FunctionChainInfo::BbChain new_bb_chain(bb_chain.layout_index);
    for (const auto &bundle : bb_chain.bb_bundles) {
      new_bb_chain.bb_bundles.emplace_back();
      for (const FullIntraCfgId &full_bb_id : bundle.full_bb_ids) {
        CHECK(!new_bb_chain.bb_bundles.empty());
        // Commit the current chain and skip this block if it's in the path.
        if (bb_indices.contains(full_bb_id.intra_cfg_id.bb_index)) {
          all_chains.push_back(std::move(new_bb_chain));
          new_bb_chain = FunctionChainInfo::BbChain(bb_chain.layout_index);
          new_bb_chain.bb_bundles.emplace_back();
          continue;
        }
        // Simply insert the block in the chain if the chain is empty.
        if (new_bb_chain.bb_bundles.back().full_bb_ids.empty()) {
          new_bb_chain.bb_bundles.back().full_bb_ids.push_back(full_bb_id);
          continue;
        }
        // Extend the current chain only if the previous block of the chain
        // has an edge to this block.
        if (!cfg.GetNodeById(new_bb_chain.bb_bundles.back()
                                 .full_bb_ids.back()
                                 .intra_cfg_id)
                 .HasEdgeTo(cfg.GetNodeById(full_bb_id.intra_cfg_id),
                            CFGEdgeKind::kBranchOrFallthough)) {
          all_chains.push_back(std::move(new_bb_chain));
          new_bb_chain = FunctionChainInfo::BbChain(bb_chain.layout_index);
          new_bb_chain.bb_bundles.emplace_back();
          new_bb_chain.bb_bundles.back().full_bb_ids.push_back(full_bb_id);
          continue;
        }
        new_bb_chain.bb_bundles.back().full_bb_ids.push_back(full_bb_id);
      }
    }
    all_chains.push_back(std::move(new_bb_chain));
  }
  all_chains.erase(
      std::remove_if(all_chains.begin(), all_chains.end(),
                     [](FunctionChainInfo::BbChain &chain) {
                       chain.bb_bundles.erase(
                           std::remove_if(
                               chain.bb_bundles.begin(), chain.bb_bundles.end(),
                               [](const FunctionChainInfo::BbBundle &bundle) {
                                 return bundle.full_bb_ids.empty();
                               }),
                           chain.bb_bundles.end());
                       return chain.bb_bundles.empty();
                     }),
      all_chains.end());
  return all_chains;
}

}  // namespace propeller
