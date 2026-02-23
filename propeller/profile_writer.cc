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

#include "propeller/profile_writer.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/bb_addr_map.h"
#include "propeller/bb_addr_map.pb.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"
#include "propeller/function_layout_info.h"
#include "propeller/function_prefetch_info.h"
#include "propeller/profile.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
namespace {

void DumpCfgs(const PropellerProfile& profile,
              const PropellerOptions& options) {
  BbAddrMapPb bb_addr_map_pb = GetBbAddrMap(options.binary_name());
  if (bb_addr_map_pb.module_bb_addr_maps().empty()) {
    LOG(ERROR) << "Failed to get BbAddrMap for " << options.binary_name();
    return;
  }
  absl::flat_hash_map<uint64_t, FunctionBbAddrMapPb*> addr_to_func_map;
  for (auto& module_map : *bb_addr_map_pb.mutable_module_bb_addr_maps()) {
    for (auto& func_map : *module_map.mutable_function_bb_addr_maps()) {
      addr_to_func_map[func_map.function_address()] = &func_map;
    }
  }

  for (auto& [section_name, cfgs] :
       profile.program_cfg->GetCfgsBySectionName()) {
    const auto section_it =
        profile.profile_infos_by_section_name.find(section_name);
    for (const ControlFlowGraph* cfg : cfgs) {
      auto func_map_it = addr_to_func_map.find(cfg->GetEntryNode()->addr());
      if (func_map_it == addr_to_func_map.end()) {
        LOG(WARNING) << "Function " << cfg->GetPrimaryName().str()
                     << " not found in BbAddrMap.";
        continue;
      }
      FunctionBbAddrMapPb* func_map = func_map_it->second;
      const FunctionProfileInfo* func_profile_info = nullptr;
      if (section_it != profile.profile_infos_by_section_name.end()) {
        auto func_it = section_it->second.profile_infos_by_function_index.find(
            cfg->function_index());
        if (func_it !=
            section_it->second.profile_infos_by_function_index.end()) {
          func_profile_info = &func_it->second;
        }
      }
      if (func_profile_info) {
        auto* optimization_info = func_map->mutable_optimization_info();
        optimization_info->set_n_clusters(
            static_cast<int>(func_profile_info->layout_info.bb_chains.size()));
        optimization_info->set_original_intra_ext_tsp_score(
            func_profile_info->layout_info.original_score.intra_score);
        optimization_info->set_optimized_intra_ext_tsp_score(
            func_profile_info->layout_info.optimized_score.intra_score);
        for (const auto& prefetch_hint :
             func_profile_info->prefetch_info.prefetch_hints) {
          auto* prefetch_hint_pb = optimization_info->add_prefetch_hints();
          prefetch_hint_pb->set_site_bb_id(prefetch_hint.site_bb_id);
          prefetch_hint_pb->set_site_callsite_index(
              prefetch_hint.site_callsite_index);
          prefetch_hint_pb->set_target_function_name(
              profile.program_cfg
                  ->GetCfgByIndex(prefetch_hint.target_function_index)
                  ->GetPrimaryName()
                  .str());
          prefetch_hint_pb->set_target_bb_id(prefetch_hint.target_bb_id);
          prefetch_hint_pb->set_target_callsite_index(
              prefetch_hint.target_callsite_index);
        }
        for (const auto& prefetch_target :
             func_profile_info->prefetch_info.prefetch_targets) {
          auto* prefetch_target_pb = optimization_info->add_prefetch_targets();
          prefetch_target_pb->set_bb_id(prefetch_target.bb_id);
          prefetch_target_pb->set_callsite_index(
              prefetch_target.callsite_index);
        }
      }
      absl::flat_hash_map<uint32_t, BbEntryPb*> id_to_bb_entry_pb;
      for (auto& bb_range : *func_map->mutable_bb_ranges()) {
        for (auto& bb_entry : *bb_range.mutable_bb_entries()) {
          id_to_bb_entry_pb[bb_entry.id()] = &bb_entry;
        }
      }
      cfg->ForEachNodeRef([&](const CFGNode& node) {
        auto bb_entry_it = id_to_bb_entry_pb.find(node.bb_id());
        if (bb_entry_it == id_to_bb_entry_pb.end()) return;
        bb_entry_it->second->set_post_link_frequency(node.CalculateFrequency());

        // Build a map for efficient lookup of existing edges.
        absl::flat_hash_map<uint32_t, EdgePb*> dest_bb_id_to_edge_pb;
        for (auto& edge_pb : *bb_entry_it->second->mutable_edges()) {
          dest_bb_id_to_edge_pb[edge_pb.dest_bb_id()] = &edge_pb;
        }

        node.ForEachOutEdgeRef([&](const CFGEdge& edge) {
          if (!edge.IsBranchOrFallthrough()) return;
          auto edge_pb_it = dest_bb_id_to_edge_pb.find(edge.sink()->bb_id());
          if (edge_pb_it != dest_bb_id_to_edge_pb.end()) {
            edge_pb_it->second->set_post_link_frequency(edge.weight());
          } else {
            auto* edge_pb = bb_entry_it->second->add_edges();
            edge_pb->set_dest_bb_id(edge.sink()->bb_id());
            edge_pb->set_post_link_frequency(edge.weight());
          }
        });
      });
    }
  }

  std::ofstream cfg_dump_os(std::string(options.cfg_dump_file_name()),
                            std::ofstream::out | std::ofstream::binary);
  CHECK(cfg_dump_os.good())
      << "Failed to open " << options.cfg_dump_file_name() << " for writing.";
  bb_addr_map_pb.SerializeToOstream(&cfg_dump_os);
}

// Writes the intra-function edge profile of `cfg` into `out` in a single line
// which starts with the "g" marker.
// For each CFGNode, it prints out the node and edge frequencies in the
// following format:
// "<bb>:<bb_freq>,<succ_bb_1>:<edge_freq_1>,<succ_bb_2>:<edge_freq_2>,..."
// which starts first with the full bb id and frequency of that node, followed
// by the successors and their edge frequencies. Please note that the edge
// weights may not precisely add up to the node frequency.
void WriteCfgProfile(const ControlFlowGraph& cfg, std::ofstream& out) {
  out << "g";
  cfg.ForEachNodeRef([&](const CFGNode& node) {
    int64_t node_frequency = node.CalculateFrequency();
    out << " " << node.full_intra_cfg_id().profile_bb_id() << ":"
        << node_frequency;
    node.ForEachOutEdgeInOrder([&](const CFGEdge& edge) {
      if (!edge.IsBranchOrFallthrough()) return;
      out << "," << edge.sink()->full_intra_cfg_id().profile_bb_id() << ":"
          << edge.weight();
    });
  });
  out << "\n";
}

// Writes the basic block hashes in a single line which starts with the "h"
// marker. For each CFGNode, it prints out the node and hash in the following
// format:
// "<bb>:<bb_hash>", the bb_hash is a hexadecimal string without the "0x"
// prefix.
void WriteBBHash(const ControlFlowGraph& cfg, std::ofstream& out) {
  out << "h";
  cfg.ForEachNodeRef([&](const CFGNode& node) {
    out << " " << node.full_intra_cfg_id().profile_bb_id() << ":"
        << absl::StrCat(absl::Hex(node.hash()));
  });
  out << "\n";
}

}  // namespace

void PropellerProfileWriter::WritePrefetchInfo(
    const FunctionPrefetchInfo& prefetch_info, const ProgramCfg& program_cfg,
    std::ofstream& out) const {
  for (const FunctionPrefetchInfo::PrefetchHint& prefetch_hint :
       prefetch_info.prefetch_hints) {
    out << profile_encoding_.prefetch_hint_specifier << prefetch_hint.site_bb_id
        << "," << prefetch_hint.site_callsite_index << " "
        << program_cfg.GetCfgByIndex(prefetch_hint.target_function_index)
               ->GetPrimaryName()
               .str()
        << "," << prefetch_hint.target_bb_id << ","
        << prefetch_hint.target_callsite_index << "\n";
  }
  for (const auto& prefetch_target : prefetch_info.prefetch_targets) {
    out << profile_encoding_.prefetch_target_specifier << prefetch_target.bb_id
        << "," << prefetch_target.callsite_index << "\n";
  }
}

absl::Status PropellerProfileWriter::Write(
    const PropellerProfile& profile) const {
  std::ofstream cc_profile_os(options_.cluster_out_name());
  std::ofstream ld_profile_os(options_.symbol_order_out_name());
  if (profile_encoding_.version != ClusterEncodingVersion::VERSION_0) {
    cc_profile_os << profile_encoding_.version_specifier << "\n";
  }
  cc_profile_os << "#Profiled binary build ID: " << profile.build_id << "\n";
  absl::flat_hash_set<int> functions_with_layout;
  // TODO(b/160339651): Remove this in favour of structured format in LLVM code.
  for (const auto& [section_name, section_profile_info] :
       profile.profile_infos_by_section_name) {
    if (options_.verbose_cluster_output())
      cc_profile_os << "#section " << section_name.str() << "\n";
    // Find total number of chains.
    unsigned total_chains = 0;
    unsigned total_hot_functions = 0;
    for (const auto& [function_index, func_profile_info] :
         section_profile_info.profile_infos_by_function_index) {
      if (func_profile_info.layout_info.bb_chains.empty()) continue;
      functions_with_layout.insert(function_index);
      ++total_hot_functions;
      total_chains += func_profile_info.layout_info.bb_chains.size();
    }

    // Allocate the symbol order vector
    std::vector<std::pair<llvm::SmallVector<llvm::StringRef, 3>,
                          std::optional<unsigned>>>
        symbol_order(total_chains);
    // Allocate the cold symbol order vector equally sized as
    // function_layout_info, as there is (at most) one cold cluster per
    // function.
    std::vector<int> cold_symbol_order(total_hot_functions);
    for (const auto& [function_index, func_profile_info] :
         section_profile_info.profile_infos_by_function_index) {
      const ControlFlowGraph* cfg =
          profile.program_cfg->GetCfgByIndex(function_index);
      CHECK_NE(cfg, nullptr);
      if (cfg->module_name().has_value() &&
          profile_encoding_.version == ClusterEncodingVersion::VERSION_1) {
        // For version 1, print the module name before the function name
        // specifier on a separate line.
        cc_profile_os << profile_encoding_.module_name_specifier
                      << cfg->module_name().value().str() << "\n";
      }
      // Print all alias names of the function.
      cc_profile_os << profile_encoding_.function_name_specifier
                    << llvm::join(cfg->names(),
                                  profile_encoding_.function_name_separator);
      if (cfg->module_name().has_value() &&
          profile_encoding_.version == ClusterEncodingVersion::VERSION_0) {
        // For version 0, print the module name after the function names and on
        // the same line.
        cc_profile_os << profile_encoding_.module_name_specifier
                      << cfg->module_name().value().str();
      }
      cc_profile_os << "\n";
      // Print cloning paths.
      if (!cfg->clone_paths().empty()) {
        CHECK_EQ(profile_encoding_.version, ClusterEncodingVersion::VERSION_1)
            << "cloning is not supported for version: "
            << profile_encoding_.version;
      }
      for (const std::vector<int>& clone_path : cfg->clone_paths()) {
        cc_profile_os << profile_encoding_.clone_path_specifier
                      << absl::StrJoin(
                             clone_path, " ",
                             [&](std::string* out, const int bb_index) {
                               absl::StrAppend(out,
                                               cfg->nodes()[bb_index]->bb_id());
                             })
                      << "\n";
      }
      if (options_.verbose_cluster_output()) {
        // Print the layout score for intra-function and inter-function edges
        // involving this function. This information allows us to study the
        // impact on layout score on each individual function.
        cc_profile_os << absl::StreamFormat(
            "#ext-tsp score: [intra: %f -> %f] [inter: %f -> %f]\n",
            func_profile_info.layout_info.original_score.intra_score,
            func_profile_info.layout_info.optimized_score.intra_score,
            func_profile_info.layout_info.original_score.inter_out_score,
            func_profile_info.layout_info.optimized_score.inter_out_score);
      }
      const std::vector<FunctionLayoutInfo::BbChain>& chains =
          func_profile_info.layout_info.bb_chains;
      if (!chains.empty()) {
        for (unsigned chain_id = 0; chain_id < chains.size(); ++chain_id) {
          auto& chain = chains[chain_id];
          std::vector<FullIntraCfgId> bb_ids_in_chain =
              chains[chain_id].GetAllBbs();
          // If a chain starts with zero BB index (function entry basic block),
          // the function name is sufficient for section ordering. Otherwise,
          // the chain number is required.
          symbol_order[chain.layout_index] =
              std::pair<llvm::SmallVector<llvm::StringRef, 3>,
                        std::optional<unsigned>>(
                  cfg->names(),
                  bb_ids_in_chain.front().intra_cfg_id.bb_index == 0
                      ? std::optional<unsigned>()
                      : chain_id);
          for (int bbi = 0; bbi < bb_ids_in_chain.size(); ++bbi) {
            const auto& full_bb_id = bb_ids_in_chain[bbi];
            cc_profile_os << (bbi != 0 ? " "
                                       : profile_encoding_.cluster_specifier)
                          << full_bb_id.profile_bb_id();
          }
          cc_profile_os << "\n";
        }
        cold_symbol_order[func_profile_info.layout_info
                              .cold_chain_layout_index] = function_index;
      }

      WritePrefetchInfo(func_profile_info.prefetch_info, *profile.program_cfg,
                        cc_profile_os);

      // Dump the edge profile for this CFG if requested.
      if (bool write_cfg_profile = options_.has_write_cfg_profile()
                                       ? options_.write_cfg_profile()
                                       : profile_encoding_.version !=
                                             ClusterEncodingVersion::VERSION_0;
          write_cfg_profile) {
        if (profile_encoding_.version == ClusterEncodingVersion::VERSION_0) {
          return absl::FailedPreconditionError(
              "cfg profile for version 0 is not supported");
        }
        WriteCfgProfile(*cfg, cc_profile_os);
      }

      // Dump the basic block hashes if requested.
      if (options_.write_bb_hash()) WriteBBHash(*cfg, cc_profile_os);
    }

    for (const auto& [func_names, chain_id] : symbol_order) {
      // Print the symbol names corresponding to every function name alias. This
      // guarantees we get the right order regardless of which function name is
      // picked by the compiler.
      for (auto& func_name : func_names) {
        ld_profile_os << func_name.str();
        if (chain_id.has_value())
          ld_profile_os << ".__part." << chain_id.value();
        ld_profile_os << "\n";
      }
    }

    // Insert the .cold symbols for cold parts of hot functions.
    for (int function_index : cold_symbol_order) {
      const ControlFlowGraph* cfg =
          profile.program_cfg->GetCfgByIndex(function_index);
      CHECK_NE(cfg, nullptr);
      const FunctionLayoutInfo& layout_info =
          section_profile_info.profile_infos_by_function_index
              .at(function_index)
              .layout_info;
      // The cold node should not be emitted if all basic blocks appear in the
      // chains.
      int num_bbs_in_chains = 0;
      for (const FunctionLayoutInfo::BbChain& chain : layout_info.bb_chains)
        num_bbs_in_chains += chain.GetNumBbs();
      if (num_bbs_in_chains == cfg->nodes().size()) continue;
      // Check if the function entry is in the chains. The entry node always
      // begins its chain. So this simply checks the first node in every
      // chain.
      bool entry_is_in_chains = absl::c_any_of(
          layout_info.bb_chains, [](const FunctionLayoutInfo::BbChain& chain) {
            return chain.GetFirstBb().intra_cfg_id.bb_index == 0;
          });
      for (auto& func_name : cfg->names()) {
        ld_profile_os << func_name.str();
        // If the entry node is not in chains, function name can serve as the
        // cold symbol name. So we don't need the ".cold" suffix.
        if (entry_is_in_chains) ld_profile_os << ".cold";
        ld_profile_os << "\n";
      }
    }
  }
  if (options_.has_cfg_dump_file_name()) {
    DumpCfgs(profile, options_);
  }
  return absl::OkStatus();
}
}  // namespace propeller
