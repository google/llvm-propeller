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

#include "propeller/program_cfg.h"

#include <tuple>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/cfg.h"

namespace propeller {

std::vector<const ControlFlowGraph *> ProgramCfg::GetCfgs() const {
  std::vector<const ControlFlowGraph *> cfgs;
  cfgs.reserve(cfgs_.size());
  for (const auto &[function_index, cfg] : cfgs_) cfgs.push_back(cfg.get());
  absl::c_sort(cfgs, [](const ControlFlowGraph *a, const ControlFlowGraph *b) {
    return a->function_index() < b->function_index();
  });
  return cfgs;
}

absl::flat_hash_map<llvm::StringRef, std::vector<const ControlFlowGraph *>>
ProgramCfg::GetCfgsBySectionName() const {
  absl::flat_hash_map<llvm::StringRef, std::vector<const ControlFlowGraph *>>
      result;
  for (const auto &[function_index, cfg] : cfgs_) {
    result[cfg->section_name()].push_back(cfg.get());
  }
  return result;
}

int ProgramCfg::GetNodeFrequencyThreshold(
    int node_frequency_cutoff_percentile) const {
  CHECK_LE(node_frequency_cutoff_percentile, 100);
  CHECK_GE(node_frequency_cutoff_percentile, 0);
  struct NodeFrequencyInfo {
    int function_index;
    int node_index;
    int frequency;
  };
  absl::flat_hash_map<int, std::vector<int>> node_frequencies;
  for (const auto &[function_index, cfg] : cfgs_) {
    node_frequencies.emplace(function_index, cfg->GetNodeFrequencies());
  }
  std::vector<NodeFrequencyInfo> hot_nodes;
  for (const auto &[function_index, frequencies] : node_frequencies) {
    for (int i = 0; i < frequencies.size(); ++i) {
      if (frequencies[i] == 0) continue;
      hot_nodes.push_back({.function_index = function_index,
                           .node_index = i,
                           .frequency = frequencies[i]});
    }
  }
  if (hot_nodes.empty()) return 0;
  int cutoff_index =
      hot_nodes.size() * node_frequency_cutoff_percentile / 100 - 1;
  if (cutoff_index < 0) return 0;

  absl::c_nth_element(
      hot_nodes, hot_nodes.begin() + cutoff_index,
      [](const NodeFrequencyInfo &a, const NodeFrequencyInfo &b) {
        return std::forward_as_tuple(a.frequency, a.function_index,
                                     a.node_index) <
               std::forward_as_tuple(b.frequency, b.function_index,
                                     b.node_index);
      });
  return hot_nodes[cutoff_index].frequency;
}

absl::flat_hash_map<int, absl::btree_set<int>> ProgramCfg::GetHotJoinNodes(
    int hot_node_frequency_threshold, int hot_edge_frequency_threshold) const {
  absl::flat_hash_map<int, absl::btree_set<int>> hot_join_nodes;

  for (const auto &[function_index, cfg] : cfgs_) {
    std::vector<int> hot_join_bbs = cfg->GetHotJoinNodes(
        hot_node_frequency_threshold, hot_edge_frequency_threshold);
    if (hot_join_bbs.empty()) continue;
    hot_join_nodes.emplace(
        function_index,
        absl::btree_set<int>(hot_join_bbs.begin(), hot_join_bbs.end()));
  }
  return hot_join_nodes;
}
}  // namespace propeller
