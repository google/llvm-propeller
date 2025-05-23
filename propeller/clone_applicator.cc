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

#include "propeller/clone_applicator.h"

#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_node.h"
#include "propeller/code_layout.h"
#include "propeller/function_chain_info.h"
#include "propeller/path_clone_evaluator.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
namespace {
// Sorts `nodes` in descending order of their frequencies, breaking ties by
// their `intra_cfg_id`s. `nodes` should be from the same CFG.
void SortNodesByFrequency(std::vector<CFGNode *> &nodes) {
  absl::c_sort(nodes, [](const CFGNode *a, const CFGNode *b) {
    return std::forward_as_tuple(-a->CalculateFrequency(), a->intra_cfg_id()) <
           std::forward_as_tuple(-b->CalculateFrequency(), b->intra_cfg_id());
  });
}

// Creates inter-function edges for `clone_cfgs_by_index` based on
// inter-function edges from `program_cfg` and the inter-function edge changes
// in `cfg_changes_by_function_index`.
void CreateInterFunctionEdges(
    const ProgramCfg &program_cfg,
    const absl::flat_hash_map<int, std::vector<CfgChangeFromPathCloning>>
        &cfg_changes_by_function_index,
    absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>>
        &clone_cfgs_by_index) {
  // Mirror original inter-function edges in `program_cfg` onto
  // `clone_cfgs_by_index`.
  for (auto &[function_index, cfg] : program_cfg.cfgs_by_index()) {
    ControlFlowGraph &src_clone_cfg = *clone_cfgs_by_index.at(function_index);
    for (const std::unique_ptr<CFGEdge> &edge : cfg->inter_edges()) {
      ControlFlowGraph &sink_clone_cfg =
          *clone_cfgs_by_index.at(edge->sink()->function_index());
      src_clone_cfg.CreateEdge(
          &src_clone_cfg.GetNodeById(edge->src()->intra_cfg_id()),
          &sink_clone_cfg.GetNodeById(edge->sink()->intra_cfg_id()),
          edge->weight(), edge->kind(), edge->inter_section());
    }
  }

  // Apply inter-function edge changes.
  for (const auto &[function_index, function_cfg_changes] :
       cfg_changes_by_function_index) {
    // `function_cfg_changes` includes the cfg changes from clonings in the
    // same order as those clonings have been applied.
    // We use a vector to keep track of the current clone_number of the cloned
    // blocks (mapped by their bb_index).
    std::vector<int> current_clone_numbers(
        program_cfg.cfgs_by_index().at(function_index)->nodes().size(), 0);
    for (const auto &cfg_change : function_cfg_changes) {
      for (const auto &inter_edge_reroute : cfg_change.inter_edge_reroutes) {
        ControlFlowGraph &src_cfg =
            *clone_cfgs_by_index.at(inter_edge_reroute.src_function_index);
        ControlFlowGraph &sink_cfg =
            *clone_cfgs_by_index.at(inter_edge_reroute.sink_function_index);
        int weight_remainder = inter_edge_reroute.weight;
        if (inter_edge_reroute.src_is_cloned) {
          CHECK_EQ(inter_edge_reroute.src_function_index, function_index);
          // This is a call or return edge from this function. We first reduce
          // the edge weight for all edges from the original src node to all
          // clone instances of the sink node.
          CFGNode &orig_src_node =
              *src_cfg.nodes().at(inter_edge_reroute.src_bb_index);
          std::vector<CFGNode *> all_sink_nodes =
              sink_cfg.GetAllClonesForBbIndex(inter_edge_reroute.sink_bb_index);
          // If we have multiple clones for the sink node, the edge weight may
          // have already been distributed among edges to the clones. Therefore,
          // we consider all corresponding edges in decreasing order of their
          // sink node's frequency.
          SortNodesByFrequency(all_sink_nodes);
          for (CFGNode *sink_node : all_sink_nodes) {
            CFGEdge *edge =
                orig_src_node.GetEdgeTo(*sink_node, inter_edge_reroute.kind);
            if (edge == nullptr) continue;
            weight_remainder -= edge->DecrementWeight(weight_remainder);
            if (weight_remainder <= 0) break;
          }
          // Now create or update the edge.
          int clone_number =
              current_clone_numbers[inter_edge_reroute.src_bb_index] + 1;
          CFGNode &clone_src_node =
              src_cfg.GetNodeById({.bb_index = inter_edge_reroute.src_bb_index,
                                   .clone_number = clone_number});
          src_cfg.CreateOrUpdateEdge(
              &clone_src_node, all_sink_nodes.front(),
              inter_edge_reroute.weight, inter_edge_reroute.kind,
              src_cfg.section_name() != sink_cfg.section_name());
        } else {
          // This must be a return edge from another function. We first reduce
          // the edge weight for all edges from the all clone instances of the
          // src node to the original sink node.
          CHECK(inter_edge_reroute.sink_is_cloned);
          CHECK_EQ(inter_edge_reroute.sink_function_index, function_index);
          CHECK_EQ(inter_edge_reroute.kind, CFGEdgeKind::kRet);
          CFGNode &orig_sink_node =
              *sink_cfg.nodes().at(inter_edge_reroute.sink_bb_index);
          std::vector<CFGNode *> all_src_nodes =
              src_cfg.GetAllClonesForBbIndex(inter_edge_reroute.src_bb_index);
          // If we have multiple clones for the src node, the edge weight may
          // have already been distributed among edges from the clones.
          // Therefore, we consider all corresponding edges in decreasing order
          // of their src node's frequency.
          SortNodesByFrequency(all_src_nodes);
          for (CFGNode *clone_src_node : all_src_nodes) {
            CFGEdge *edge = clone_src_node->GetEdgeTo(orig_sink_node,
                                                      inter_edge_reroute.kind);
            if (edge == nullptr) continue;
            weight_remainder -= edge->DecrementWeight(weight_remainder);
            if (weight_remainder <= 0) break;
          }
          // Now create or update the edge.
          int clone_number =
              current_clone_numbers[inter_edge_reroute.sink_bb_index] + 1;
          CFGNode &clone_sink_node = sink_cfg.GetNodeById(
              {.bb_index = inter_edge_reroute.sink_bb_index,
               .clone_number = clone_number});
          src_cfg.CreateOrUpdateEdge(
              all_src_nodes.front(), &clone_sink_node,
              inter_edge_reroute.weight, inter_edge_reroute.kind,
              src_cfg.section_name() != sink_cfg.section_name());
        }
      }
      for (int bb_index : cfg_change.path_to_clone)
        ++current_clone_numbers[bb_index];
    }
  }

  auto drop_inter_function_edges = [&](const PathNode &path_node,
                                       ControlFlowGraph &src_cfg) {
    CFGNode &src_node = *src_cfg.nodes().at(path_node.node_bb_index());
    for (const auto &[call_ret, freq] :
         path_node.path_pred_info().missing_pred_entry.call_freqs) {
      if (!call_ret.callee.has_value()) continue;
      ControlFlowGraph &callee_cfg = *clone_cfgs_by_index.at(*call_ret.callee);
      CFGNode &callee_node = *callee_cfg.nodes().at(0);
      CFGEdge *call_edge = src_node.GetEdgeTo(callee_node, CFGEdgeKind::kCall);
      if (call_edge == nullptr) {
        LOG(WARNING) << "No call edge from block "
                     << src_cfg.GetPrimaryName().str() << "#"
                     << src_node.bb_id() << " to function "
                     << callee_cfg.GetPrimaryName().str();
        continue;
      } else {
        call_edge->DecrementWeight(freq);
      }
      if (call_ret.return_bb.has_value()) {
        ControlFlowGraph &return_from_cfg =
            *clone_cfgs_by_index.at(call_ret.return_bb->function_index);
        CFGNode &return_from_node =
            *return_from_cfg.nodes().at(call_ret.return_bb->flat_bb_index);
        CFGEdge *return_edge =
            return_from_node.GetEdgeTo(src_node, CFGEdgeKind::kRet);
        if (return_edge == nullptr) {
          LOG(WARNING) << "No return edge from block "
                       << return_from_cfg.GetPrimaryName().str() << "#"
                       << return_from_node.bb_id() << " to block "
                       << src_cfg.GetPrimaryName().str() << "#"
                       << src_node.bb_id();
        } else {
          return_edge->DecrementWeight(freq);
        }
      }
    }
    for (const auto &[bb_handle, freq] :
         path_node.path_pred_info().missing_pred_entry.return_to_freqs) {
      ControlFlowGraph &return_to_cfg =
          *clone_cfgs_by_index.at(bb_handle.function_index);
      CFGNode &return_to_node =
          *return_to_cfg.nodes().at(bb_handle.flat_bb_index);
      CFGEdge *return_edge =
          src_node.GetEdgeTo(return_to_node, CFGEdgeKind::kRet);
      if (return_edge == nullptr) {
        LOG(WARNING) << "No return edge from block "
                     << src_cfg.GetPrimaryName().str() << "#"
                     << src_node.bb_id() << " to block "
                     << return_to_cfg.GetPrimaryName().str() << "#"
                     << return_to_node.bb_id();
      } else {
        return_edge->DecrementWeight(freq);
      }
    }
  };

  for (const auto &[function_index, function_cfg_changes] :
       cfg_changes_by_function_index) {
    ControlFlowGraph &cfg = *clone_cfgs_by_index.at(function_index);
    for (const auto &function_cfg_change : function_cfg_changes) {
      for (const PathNode *path_node : function_cfg_change.paths_to_drop) {
        drop_inter_function_edges(*path_node, cfg);
      }
    }
  }
}
}  // namespace

CloneApplicatorStats ApplyClonings(
    const propeller::PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options,
    absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>>
        clonings_by_function_index,
    const propeller::ProgramCfg &program_cfg,
    const absl::flat_hash_map<int, FunctionPathProfile>
        &path_profiles_by_function_index) {
  double total_score_gain = 0;

  LOG(INFO) << "Applying clonings...";
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>>
      clone_cfgs_by_function_index;
  absl::flat_hash_map<int, std::vector<CfgChangeFromPathCloning>>
      cfg_changes_by_function_index;

  for (auto &[function_index, clonings] : clonings_by_function_index) {
    // Apply clonings in reverse order of their scores.
    absl::c_sort(clonings, std::greater<EvaluatedPathCloning>());
    const auto &function_path_profile =
        path_profiles_by_function_index.at(function_index);

    const ControlFlowGraph *cfg = program_cfg.GetCfgByIndex(function_index);
    CfgBuilder cfg_builder(cfg);
    auto compute_optimal_chain_info = [&]() {
      std::unique_ptr<ControlFlowGraph> clone_cfg = cfg_builder.Clone().Build();
      std::vector<FunctionChainInfo> code_layout_result =
          CodeLayout(code_layout_params, {clone_cfg.get()},
                     /*initial_chains=*/{})
              .OrderAll();
      CHECK_EQ(code_layout_result.size(), 1);
      return code_layout_result.front();
    };
    std::optional<propeller::FunctionChainInfo> optimal_chain_info;
    auto &current_cfg_changes = cfg_changes_by_function_index[function_index];

    for (EvaluatedPathCloning &cloning : clonings) {
      auto register_cloning = [&](EvaluatedPathCloning cloning) {
        total_score_gain += *cloning.score;
        cfg_builder.AddCfgChange(cloning.cfg_change);
        current_cfg_changes.push_back(std::move(cloning.cfg_change));
        // Reset `optimal_chain_info` as the CFG has changed and it must be
        // recomputed.
        optimal_chain_info = std::nullopt;
      };
      // Evaluate clonings again if any clonings have been applied as the score
      // may have been changed.
      if (!cfg_builder.cfg_changes().empty() || !cloning.score.has_value()) {
        if (!optimal_chain_info.has_value())
          optimal_chain_info = compute_optimal_chain_info();
        absl::StatusOr<EvaluatedPathCloning> evaluated_cloning =
            EvaluateCloning(cfg_builder.Clone(), cloning.path_cloning,
                            code_layout_params, path_profile_options,
                            path_profile_options.min_final_cloning_score(),
                            optimal_chain_info.value(), function_path_profile);
        if (!evaluated_cloning.ok()) continue;
        register_cloning(std::move(*std::move(evaluated_cloning)));
      } else if (cloning.score <
                 path_profile_options.min_final_cloning_score()) {
        // We can skip the rest of the clonings since they will have lower
        // scores.
        break;
      } else {
        register_cloning(std::move(cloning));
      }
    }
    if (cfg_builder.cfg_changes().empty()) continue;
    CHECK(clone_cfgs_by_function_index
              .insert({function_index, std::move(cfg_builder).Build()})
              .second);
  }

  // Clone the remaining CFGs (those without any clonings applied) into the
  // clone_cfgs_by_function_index map, so we can recreate the inter-function
  // edges.
  for (const auto &[function_index, cfg] : program_cfg.cfgs_by_index()) {
    clone_cfgs_by_function_index.lazy_emplace(
        function_index,
        [&](const auto &ctor) { ctor(function_index, CloneCfg(*cfg)); });
  }
  CreateInterFunctionEdges(program_cfg, cfg_changes_by_function_index,
                           clone_cfgs_by_function_index);
  return {
      .clone_cfgs_by_function_index = std::move(clone_cfgs_by_function_index),
      .total_score_gain = total_score_gain};
}

std::unique_ptr<propeller::ProgramCfg> ApplyClonings(
    const propeller::PropellerCodeLayoutParameters &code_layout_params,
    const PathProfileOptions &path_profile_options,
    const ProgramPathProfile &program_path_profile,
    std::unique_ptr<propeller::ProgramCfg> program_cfg,
    propeller::PropellerStats::CloningStats &cloning_stats) {
  // Use a fast code layout parameter setting for evaluation of clonings by
  // disabling chain splitting.
  PropellerCodeLayoutParameters fast_code_layout_params(code_layout_params);
  fast_code_layout_params.set_call_chain_clustering(false);
  fast_code_layout_params.set_inter_function_reordering(false);
  fast_code_layout_params.set_chain_split(false);

  absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>>
      clonings_by_function_index =
          EvaluateAllClonings(program_cfg.get(), &program_path_profile,
                              fast_code_layout_params, path_profile_options);

  CloneApplicatorStats clone_applicator_stats =
      ApplyClonings(fast_code_layout_params, path_profile_options,
                    std::move(clonings_by_function_index), *program_cfg,
                    program_path_profile.path_profiles_by_function_index());

  cloning_stats.score_gain = clone_applicator_stats.total_score_gain;

  for (const auto &[function_index, clone_cfg] :
       clone_applicator_stats.clone_cfgs_by_function_index) {
    cloning_stats.paths_cloned += clone_cfg->clone_paths().size();
    for (const auto &[bb_index, clones] : clone_cfg->clones_by_bb_index()) {
      cloning_stats.bbs_cloned += clones.size();
      cloning_stats.bytes_cloned +=
          clone_cfg->nodes().at(bb_index)->size() * clones.size();
    }
  }
  return std::make_unique<ProgramCfg>(
      std::move(clone_applicator_stats.clone_cfgs_by_function_index));
}
}  // namespace propeller
