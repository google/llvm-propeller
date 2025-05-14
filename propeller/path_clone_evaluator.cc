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

#include "propeller/path_clone_evaluator.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

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

absl::StatusOr<CfgChangeFromPathCloning> GetCfgChangeForPathCloning(
    const PathCloning &cloning, const ConflictEdges &conflict_edges) {
  CfgChangeFromPathCloning cfg_change{.path_pred_bb_index =
                                          cloning.path_pred_bb_index};
  const std::vector<const PathNode *> path_from_root =
      cloning.path_node->path_from_root();
  // We can't confidently apply a cloning if its path predecessor edge has been
  // affected by the clonings applied so far.
  if (conflict_edges.affected_edges.contains(
          {.from_bb_index = cloning.path_pred_bb_index,
           .to_bb_index = path_from_root.front()->node_bb_index()})) {
    return absl::FailedPreconditionError(
        "path predecessor edge has been affected by the currently applied "
        "clonings.");
  }

  // Construct the CfgChange by tracing the cloning path.
  int prev_bb_index = cloning.path_pred_bb_index;
  for (int i = 0; i < path_from_root.size(); ++i) {
    const PathNode *path_node = path_from_root[i];
    const PathPredInfoEntry *entry =
        path_node->path_pred_info().GetEntry(cloning.path_pred_bb_index);
    CHECK_NE(entry, nullptr)
        << "Path is unreachable via the predecessor block: "
        << cloning.path_pred_bb_index;
    cfg_change.path_to_clone.push_back(path_node->node_bb_index());

    // Record that the control flow from the previous block in the path must be
    // reroute via the clone.
    cfg_change.intra_edge_reroutes.push_back(
        {.src_bb_index = prev_bb_index,
         .sink_bb_index = path_node->node_bb_index(),
         .src_is_cloned = i != 0,
         .sink_is_cloned = true,
         .kind = CFGEdgeKind::kBranchOrFallthough,
         .weight = entry->freq});

    //  Record inter-function edge changes.
    for (const auto &[call_ret, freq] : entry->call_freqs) {
      if (call_ret.callee.has_value()) {
        cfg_change.inter_edge_reroutes.push_back(
            {.src_function_index = cloning.function_index,
             .sink_function_index = *call_ret.callee,
             .src_bb_index = path_node->node_bb_index(),
             .sink_bb_index = 0,
             .src_is_cloned = true,
             .sink_is_cloned = false,
             .kind = CFGEdgeKind::kCall,
             .weight = freq});
      }
      if (call_ret.return_bb.has_value()) {
        cfg_change.inter_edge_reroutes.push_back(
            {.src_function_index = call_ret.return_bb->function_index,
             .sink_function_index = cloning.function_index,
             .src_bb_index = call_ret.return_bb->flat_bb_index,
             .sink_bb_index = path_node->node_bb_index(),
             .src_is_cloned = false,
             .sink_is_cloned = true,
             .kind = CFGEdgeKind::kRet,
             .weight = freq});
      }
    }
    // Visit the child edges from this clone to record changes in their
    // weights.
    for (const auto &[child_bb_id, child_path_node] : path_node->children()) {
      const PathPredInfoEntry *child_entry =
          child_path_node->path_pred_info().GetEntry(
              cloning.path_pred_bb_index);
      if (child_entry == nullptr) continue;
      // If any of these affected edges were found to have been the path
      // predecessor edge of some cloning previously applied, it would
      // conflict with applying that cloning. So we fail in such cases.
      if (conflict_edges.path_pred_edges.contains(
              {.from_bb_index = path_node->node_bb_index(),
               .to_bb_index = child_bb_id}))
        return absl::FailedPreconditionError(
            "Edge is the path predecessor of some cloning previously "
            "applied.");

      // Rerouting the in-path edge will be done in next iteration. Here, we
      // reroute other outgoing edges from the path.
      if (i + 1 != path_from_root.size() &&
          path_from_root[i + 1]->node_bb_index() == child_bb_id)
        continue;
      // Record that the outgoing control flow of the path to the original
      // nodes must be rerouted via the clone nodes.
      cfg_change.intra_edge_reroutes.push_back(
          {.src_bb_index = path_node->node_bb_index(),
           .sink_bb_index = child_bb_id,
           .src_is_cloned = true,
           .sink_is_cloned = false,
           .kind = CFGEdgeKind::kBranchOrFallthough,
           .weight = child_entry->freq});
    }
    prev_bb_index = path_node->node_bb_index();
  }
  // Record return edge changes.
  const auto &return_to_freqs = cloning.path_node->path_pred_info()
                                    .entries.at(cloning.path_pred_bb_index)
                                    .return_to_freqs;
  for (const auto &[bb_handle, freq] : return_to_freqs) {
    cfg_change.inter_edge_reroutes.push_back(
        {.src_function_index = cloning.function_index,
         .sink_function_index = bb_handle.function_index,
         .src_bb_index = cloning.path_node->node_bb_index(),
         .sink_bb_index = bb_handle.flat_bb_index,
         .src_is_cloned = true,
         .sink_is_cloned = false,
         .kind = CFGEdgeKind::kRet,
         .weight = freq});
  }
  return cfg_change;
}

absl::StatusOr<EvaluatedPathCloning> EvaluateCloning(
    const CfgBuilder &cfg_builder, const PathCloning &path_cloning,
    const PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options, double min_score,
    const FunctionChainInfo &optimal_chain_info) {
  CHECK(!code_layout_params.call_chain_clustering());
  CHECK(!code_layout_params.inter_function_reordering());
  CHECK_EQ(optimal_chain_info.function_index,
           cfg_builder.cfg().function_index());
  ASSIGN_OR_RETURN(
      CfgChangeFromPathCloning new_cfg_change,
      GetCfgChangeForPathCloning(path_cloning, cfg_builder.conflict_edges()));
  CfgBuilder cfg_builder_for_evaluation = cfg_builder.Clone();
  cfg_builder_for_evaluation.AddCfgChange(new_cfg_change);
  std::unique_ptr<ControlFlowGraph> cfg_with_cloning =
      std::move(cfg_builder_for_evaluation).Build();
  FunctionChainInfo clone_chain_info =
      CodeLayout(code_layout_params, {cfg_with_cloning.get()},
                 {{cfg_builder.cfg().function_index(),
                   GetInitialChains(*cfg_with_cloning, optimal_chain_info,
                                    new_cfg_change)}})
          .OrderAll()
          .front();
  double score_gain =
      clone_chain_info.optimized_score.intra_score -
      optimal_chain_info.optimized_score.intra_score -
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
    std::vector<EvaluatedPathCloning> &clonings) {
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

  EvaluateCloningsForPath(path_tree, new_path_preds_in_path, clonings);

  // We can't clone a path if it has an intermediate blocks with indirect
  // branches as they can't be rewired.
  if (has_indirect_branch) return;

  for (auto &[child_bb_index, child_path_node] : path_tree.children()) {
    CHECK_NE(child_path_node, nullptr);
    EvaluateCloningsForSubtree(*child_path_node, path_length + 1,
                               new_path_preds_in_path, clonings);
  }
}

void PathTreeCloneEvaluator::EvaluateCloningsForPath(
    const PathNode &path_node,
    const absl::flat_hash_set<int> &path_preds_in_path,
    std::vector<EvaluatedPathCloning> &clonings) {
  bool is_return_block =
      cfg_.nodes().at(path_node.node_bb_index())->has_return();
  if (path_node.children().size() < 2 && !is_return_block) {
    return;
  }
  for (const auto &[pred_bb_index, path_pred_info_entry] :
       path_node.path_pred_info().entries) {
    // We can't clone a path when the path predecessor has an indirect branch
    // as it can't be rewired.
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
        path_profile_options_.min_initial_cloning_score(), optimal_chain_info_);
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
                                      clonings);
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
        // Extend the current chain only if the previous block of the chain has
        // an edge to this block.
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
