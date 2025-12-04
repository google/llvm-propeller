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

#include "propeller/code_layout.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/types/span.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/cfg.h"
#include "propeller/cfg_node.h"
#include "propeller/chain_cluster_builder.h"
#include "propeller/function_layout_info.h"
#include "propeller/node_chain.h"
#include "propeller/node_chain_builder.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

absl::btree_map<llvm::StringRef, SectionLayoutInfo> GenerateLayoutBySection(
    const ProgramCfg& program_cfg,
    const PropellerCodeLayoutParameters& code_layout_params,
    PropellerStats::CodeLayoutStats& code_layout_stats) {
  absl::btree_map<llvm::StringRef, SectionLayoutInfo>
      layout_info_by_section_name;
  absl::flat_hash_map<llvm::StringRef, std::vector<const ControlFlowGraph*>>
      cfgs_by_section_name = program_cfg.GetCfgsBySectionName();
  for (const auto& [section_name, cfgs] : cfgs_by_section_name) {
    CodeLayout code_layout(code_layout_params, cfgs);
    layout_info_by_section_name.emplace(section_name,
                                        code_layout.GenerateLayout());
    code_layout_stats += code_layout.stats();
  }
  return layout_info_by_section_name;
}

// Returns the intra-procedural ext-tsp scores for the given CFGs given a
// function for getting the address of each CFG node.
// This is called by ComputeOrigLayoutScores and ComputeOptLayoutScores below.
absl::flat_hash_map<int, CFGScore> CodeLayout::ComputeCfgScores(
    absl::FunctionRef<uint64_t(const CFGNode*)> get_node_addr) {
  absl::flat_hash_map<int, CFGScore> score_map;
  for (const ControlFlowGraph* cfg : cfgs_) {
    double intra_score = 0;
    for (const auto& edge : cfg->intra_edges()) {
      if (edge->weight() == 0 || edge->IsReturn()) continue;
      // Compute the distance between the end of src and beginning of sink.
      int64_t distance = static_cast<int64_t>(get_node_addr(edge->sink())) -
                         get_node_addr(edge->src()) - edge->src()->size();
      intra_score += code_layout_scorer_.GetEdgeScore(*edge, distance);
    }
    double inter_out_score = 0;
    if (cfgs_.size() > 1) {
      for (const auto& edge : cfg->inter_edges()) {
        if (edge->weight() == 0 || edge->IsReturn() || edge->inter_section()) {
          continue;
        }
        int64_t distance = static_cast<int64_t>(get_node_addr(edge->sink())) -
                           get_node_addr(edge->src()) - edge->src()->size();
        inter_out_score += code_layout_scorer_.GetEdgeScore(*edge, distance);
      }
    }
    score_map.emplace(cfg->function_index(),
                      CFGScore({intra_score, inter_out_score}));
  }
  return score_map;
}

// Returns the intra-procedural ext-tsp scores for the given CFGs under the
// original layout.
absl::flat_hash_map<int, CFGScore> CodeLayout::ComputeOrigLayoutScores() {
  return ComputeCfgScores([](const CFGNode* n) { return n->addr(); });
}

// Returns the intra-procedural ext-tsp scores for the given CFGs under the new
// layout, which is described by the 'clusters' parameter.
absl::flat_hash_map<int, CFGScore> CodeLayout::ComputeOptLayoutScores(
    absl::Span<const std::unique_ptr<const ChainCluster>> clusters) {
  // First compute the address of each basic block under the given layout.
  uint64_t layout_addr = 0;
  absl::flat_hash_map<const CFGNode*, uint64_t> layout_address_map;
  for (auto& cluster : clusters) {
    cluster->VisitEachNodeRef([&](const CFGNode& node) {
      layout_address_map.emplace(&node, layout_addr);
      layout_addr += node.size();
    });
  }

  return ComputeCfgScores([&layout_address_map](const CFGNode* n) {
    return layout_address_map.at(n);
  });
}

SectionLayoutInfo CodeLayout::GenerateLayout() {
  // Build optimal node chains for each CFG.
  std::vector<std::unique_ptr<const NodeChain>> built_chains;
  if (code_layout_scorer_.code_layout_params().inter_function_reordering()) {
    absl::c_move(NodeChainBuilder::CreateNodeChainBuilder<
                     NodeChainAssemblyBalancedTreeQueue>(
                     code_layout_scorer_, cfgs_, initial_chains_, stats_)
                     .BuildChains(),
                 std::back_inserter(built_chains));
  } else {
    for (auto* cfg : cfgs_) {
      if (!cfg->is_hot()) continue;
      absl::c_move(NodeChainBuilder::CreateNodeChainBuilder<
                       NodeChainAssemblyIterativeQueue>(
                       code_layout_scorer_, {cfg}, initial_chains_, stats_)
                       .BuildChains(),
                   std::back_inserter(built_chains));
    }
  }

  // Further cluster the constructed chains to get the global order of all
  // nodes.
  const std::vector<std::unique_ptr<const ChainCluster>> clusters =
      ChainClusterBuilder(code_layout_scorer_.code_layout_params(),
                          std::move(built_chains))
          .BuildClusters();

  absl::flat_hash_map<int, CFGScore> orig_score_map = ComputeOrigLayoutScores();
  absl::flat_hash_map<int, CFGScore> opt_score_map =
      ComputeOptLayoutScores(clusters);

  SectionLayoutInfo section_layout_info;
  absl::btree_map<int, FunctionLayoutInfo>& function_layout_info_map =
      section_layout_info.layouts_by_function_index;

  int function_index = -1;
  unsigned layout_index = 0;

  // Cold chains are laid out consistently with how hot chains appear in the
  // layout. For two functions foo and bar, foo's cold chain is placed before
  // bar's cold chain iff (any) hot chain of foo appears before (all) hot
  // chains of bar.
  unsigned cold_chain_layout_index = 0;

  auto func_layout_info_it = function_layout_info_map.end();

  // Iterate over all CFG nodes in order and add them to the chain layout
  // information.
  for (auto& cluster : clusters) {
    for (const auto& chain : cluster->chains()) {
      for (const auto& node_bundle : chain->node_bundles()) {
        for (int i = 0; i < node_bundle->nodes().size(); ++i) {
          const CFGNode& node = *node_bundle->nodes()[i];
          if (function_index != node.function_index() || node.is_entry()) {
            // Switch to the right chain layout info when the function changes
            // or when an entry basic block is reached.
            function_index = node.function_index();
            bool inserted = false;
            std::tie(func_layout_info_it, inserted) =
                function_layout_info_map.emplace(
                    function_index,
                    FunctionLayoutInfo{
                        // We populate the clusters vector later.
                        .bb_chains = {},
                        .original_score = orig_score_map.at(function_index),
                        .optimized_score = opt_score_map.at(function_index),
                        .cold_chain_layout_index = cold_chain_layout_index});
            if (inserted) ++cold_chain_layout_index;
            // Start a new chain and increment the global layout index.
            func_layout_info_it->second.bb_chains.emplace_back(layout_index++);
          }
          // Start a new BB bundle if this either this is the first node in the
          // bundle, or if we have created a new chain.
          if (i == 0 ||
              func_layout_info_it->second.bb_chains.back().bb_bundles.empty()) {
            func_layout_info_it->second.bb_chains.back()
                .bb_bundles.emplace_back();
          }
          func_layout_info_it->second.bb_chains.back()
              .bb_bundles.back()
              .full_bb_ids.push_back(node.full_intra_cfg_id());
        }
      }
    }
  }
  for (auto& [unused, func_layout_info] : function_layout_info_map) {
    stats_.original_intra_score += func_layout_info.original_score.intra_score;
    stats_.optimized_intra_score +=
        func_layout_info.optimized_score.intra_score;
    stats_.original_inter_score +=
        func_layout_info.original_score.inter_out_score;
    stats_.optimized_inter_score +=
        func_layout_info.optimized_score.inter_out_score;
  }

  // For each function chain info, sort the BB chains in increasing order of
  // their first basic block id to make sure they appear in a fixed order in
  // the basic block sections list file which is independent from the global
  // chain ordering.
  // TODO(rahmanl): Test the chain order once we have interproc-reordering.
  for (auto& [unused, func_layout_info] : function_layout_info_map) {
    absl::c_sort(func_layout_info.bb_chains,
                 [](const FunctionLayoutInfo::BbChain& a,
                    const FunctionLayoutInfo::BbChain& b) {
                   return a.GetFirstBb().bb_id < b.GetFirstBb().bb_id;
                 });
  }

  return section_layout_info;
}
}  // namespace propeller
