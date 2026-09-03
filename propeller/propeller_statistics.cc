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

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "llvm/Support/FormatVariadic.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/chain_merge_order.h"

namespace propeller {

std::string PropellerStats::CodeLayoutStats::DebugString() const {
  const double intra_score_percent_change =
      100 * (optimized_intra_score / original_intra_score - 1);

  const double inter_score_percent_change =
      100 * (optimized_inter_score / original_inter_score - 1);

  return absl::StrJoin(
      {llvm::formatv(
           "Merge order stats: {0}",
           absl::StrJoin(n_assemblies_by_merge_order, ", ",
                         [](std::string* out,
                            const std::pair<ChainMergeOrder, int>& entry) {
                           *out += '[';
                           *out += GetMergeOrderName(entry.first);
                           *out += ':';
                           *out += std::to_string(entry.second);
                           *out += ']';
                         }))
           .str(),
       llvm::formatv("Initial chains stats: single-node chains: [{0}] "
                     "multi-node chains: [{1}]",
                     n_single_node_chains, n_multi_node_chains)
           .str(),
       absl::StrFormat(
           "Changed inter-function (ext-tsp) score by %+.1f%% from %f to %f.",
           inter_score_percent_change, original_inter_score,
           optimized_inter_score),
       absl::StrFormat(
           "Changed intra-function (ext-tsp) score by %+.1f%% from %f to %f",
           intra_score_percent_change, original_intra_score,
           optimized_intra_score)},
      "\n");
}

std::string PropellerStats::DisassemblyStats::Stat::DebugString() const {
  return absl::StrFormat("absolute: %d / weighted: %d", absolute, weighted);
}

std::string PropellerStats::DisassemblyStats::DebugString() const {
  return absl::StrFormat(
      "Disassembly stats:\nCould not disassemble: %s\nMay affect control flow: "
      "%s\nCan not affect control flow: %s",
      could_not_disassemble.DebugString(),
      may_affect_control_flow.DebugString(),
      cant_affect_control_flow.DebugString());
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
          absl::StrJoin(
              edges_created_by_kind, ", ",
              [edges_created](std::string* out,
                              const std::pair<CFGEdgeKind, int64_t>& entry) {
                *out += absl::StrFormat("%s: %.2f%%",
                                        GetCfgEdgeKindString(entry.first),
                                        entry.second * 100.0 / edges_created);
              }))
          .str(),
      llvm::formatv(
          "Profiled {0} total edge weight: {{{1}}.", total_edge_weight,
          absl::StrJoin(total_edge_weight_by_kind, ", ",
                        [total_edge_weight](
                            std::string* out,
                            const std::pair<CFGEdgeKind, int64_t>& entry) {
                          *out += absl::StrFormat(
                              "%s: %.2f%%", GetCfgEdgeKindString(entry.first),
                              entry.second * 100.0 / total_edge_weight);
                        }))
          .str()};

  if (edges_with_same_src_sink_but_different_type) {
    lines.push_back(
        llvm::formatv(
            "Found edges with same source and sink but different type {0}",
            edges_with_same_src_sink_but_different_type)
            .str());
  }

  return absl::StrJoin(lines, "\n");
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
  return absl::StrJoin(lines, "\n");
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
  return absl::StrJoin(stat_lines, "\n");
}
}  // namespace propeller
