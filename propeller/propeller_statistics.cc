// Copyright 2026 The Propeller Authors.
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

#include "propeller/propeller_statistics.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/chain_merge_order.h"

namespace propeller {

std::string PropellerStats::CodeLayoutStats::DebugString() const {
  const double intra_score_percent_change =
      100 * (optimized_intra_score / original_intra_score - 1);

  const double inter_score_percent_change =
      100 * (optimized_inter_score / original_inter_score - 1);

  return llvm::join_items(
      "\n",
      llvm::formatv(
          "Merge order stats: {0}",
          llvm::join(
              llvm::map_range(n_assemblies_by_merge_order,
                              [](const std::pair<ChainMergeOrder, int>& entry) {
                                return (llvm::Twine("[") +
                                        GetMergeOrderName(entry.first) + ":" +
                                        llvm::Twine(entry.second) + "]")
                                    .str();
                              }),
              ", "))
          .str(),
      llvm::formatv("Initial chains stats: single-node chains: [{0}] "
                    "multi-node chains: [{1}]",
                    n_single_node_chains, n_multi_node_chains)
          .str(),
      llvm::formatv(
          "Changed inter-function (ext-tsp) score by {0}{1:F1}% from {2:F6} to "
          "{3:F6}.",
          inter_score_percent_change >= 0 ? "+" : "",
          inter_score_percent_change, original_inter_score,
          optimized_inter_score)
          .str(),
      llvm::formatv(
          "Changed intra-function (ext-tsp) score by {0}{1:F1}% from {2:F6} to "
          "{3:F6}",
          intra_score_percent_change >= 0 ? "+" : "",
          intra_score_percent_change, original_intra_score,
          optimized_intra_score)
          .str());
}

std::string PropellerStats::DisassemblyStats::Stat::DebugString() const {
  return llvm::formatv("absolute: {0} / weighted: {1}", absolute, weighted)
      .str();
}

std::string PropellerStats::DisassemblyStats::DebugString() const {
  return llvm::formatv(
             "Disassembly stats:\nCould not disassemble: {0}\nMay affect "
             "control flow: {1}\nCan not affect control flow: {2}",
             could_not_disassemble.DebugString(),
             may_affect_control_flow.DebugString(),
             cant_affect_control_flow.DebugString())
      .str();
}

std::string PropellerStats::ProfileStats::DebugString() const {
  return llvm::formatv(
             "Parsed {0} profiles.\nTotal {1} binary mmaps.\nTotal {2} br "
             "entries accumulated.",
             perf_file_parsed, binary_mmap_num, br_counters_accumulated)
      .str();
}

std::string PropellerStats::CfgStats::DebugString() const {
  int64_t edges_created = total_edges_created();
  int64_t total_edge_weight = total_edge_weight_created();

  std::vector<std::string> lines = {
      llvm::formatv("{0} hot basic blocks found in profiles.", hot_basic_blocks)
          .str(),
      llvm::formatv("Created {0} cfgs.", cfgs_created).str(),
      llvm::formatv("Created {0} nodes.", nodes_created).str(),
      llvm::formatv("{0} cfgs have hot landing pads.",
                    cfgs_with_hot_landing_pads)
          .str(),
      llvm::formatv("{0} hot blocks have zero size.", hot_empty_basic_blocks)
          .str(),
      llvm::formatv(
          "Created {0} edges: {{{1}}.", edges_created,
          llvm::join(llvm::map_range(
                         edges_created_by_kind,
                         [edges_created](
                             const std::pair<CFGEdgeKind, int64_t>& entry) {
                           return llvm::formatv(
                                      "{0}: {1:F2}%",
                                      GetCfgEdgeKindString(entry.first),
                                      entry.second * 100.0 / edges_created)
                               .str();
                         }),
                     ", "))
          .str(),
      llvm::formatv(
          "Profiled {0} total edge weight: {{{1}}.", total_edge_weight,
          llvm::join(llvm::map_range(
                         total_edge_weight_by_kind,
                         [total_edge_weight](
                             const std::pair<CFGEdgeKind, int64_t>& entry) {
                           return llvm::formatv(
                                      "{0}: {1:F2}%",
                                      GetCfgEdgeKindString(entry.first),
                                      entry.second * 100.0 / total_edge_weight)
                               .str();
                         }),
                     ", "))
          .str()};

  if (edges_with_same_src_sink_but_different_type) {
    lines.push_back(
        llvm::formatv(
            "Found edges with same source and sink but different type {0}",
            edges_with_same_src_sink_but_different_type)
            .str());
  }

  return llvm::join(lines, "\n");
}

std::string PropellerStats::BbAddrMapStats::DebugString() const {
  std::vector<std::string> lines = {
      llvm::formatv("{0} hot functions (alias included) found in profiles.",
                    hot_functions)
          .str()};
  if (duplicate_symbols) {
    lines.push_back(
        llvm::formatv("Duplicate symbols: {0} symbols.", duplicate_symbols)
            .str());
  }
  if (bbaddrmap_function_does_not_have_symtab_entry) {
    lines.push_back(
        llvm::formatv("Dropped {0} bbaddrmap entries, because they do not have "
                      "corresponding symbols in binary symtab.",
                      bbaddrmap_function_does_not_have_symtab_entry)
            .str());
  }
  return llvm::join(lines, "\n");
}

std::string PropellerStats::CloningStats::DebugString() const {
  return llvm::formatv(
             "Cloned {0} paths.\nAdded {1} cloned basic blocks.\nIncreased "
             "code size by {2} bytes with cloning.\nGained {3} in cloning "
             "score.",
             paths_cloned, bbs_cloned, bytes_cloned, score_gain)
      .str();
}

std::string PropellerStats::DebugString() const {
  std::vector<std::string> stat_lines = {
      profile_stats.DebugString(),     bbaddrmap_stats.DebugString(),
      cfg_stats.DebugString(),         code_layout_stats.DebugString(),
      disassembly_stats.DebugString(), cloning_stats.DebugString()};
  return llvm::join(stat_lines, "\n");
}
}  // namespace propeller
