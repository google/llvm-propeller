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

#include "propeller/profile_writer.h"

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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
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
              absl::string_view cfg_dump_dir_name) {
  // Create the cfg dump directory and the cfg index file.
  llvm::sys::fs::create_directory(cfg_dump_dir_name);
  llvm::SmallString<100> cfg_index_file_vec(cfg_dump_dir_name.begin(),
                                            cfg_dump_dir_name.end());
  llvm::sys::path::append(cfg_index_file_vec, "cfg-index.txt");
  std::string cfg_index_file(cfg_index_file_vec.str());
  std::ofstream cfg_index_os(cfg_index_file, std::ofstream::out);
  CHECK(cfg_index_os.good())
      << "Failed to open " << cfg_index_file << " for writing.";
  cfg_index_os << absl::StrJoin({"Function.Name", "Function.Address", "N_Nodes",
                                 "N_Clusters", "Original.ExtTSP.Score",
                                 "Optimized.ExtTSP.Score", "N_Prefetches"},
                                " ")
               << "\n";

  for (const auto& [section_name, section_profile_info] :
       profile.profile_infos_by_section_name) {
    for (const auto& [function_index, func_profile_info] :
         section_profile_info.profile_infos_by_function_index) {
      const ControlFlowGraph* cfg =
          profile.program_cfg->GetCfgByIndex(function_index);
      CHECK_NE(cfg, nullptr);
      // Dump hot cfgs into the given directory.
      auto func_addr_str =
          absl::StrCat("0x", absl::Hex(cfg->GetEntryNode()->addr()));
      cfg_index_os << cfg->GetPrimaryName().str() << " " << func_addr_str << " "
                   << cfg->nodes().size() << " "
                   << func_profile_info.layout_info.bb_chains.size() << " "
                   << func_profile_info.layout_info.original_score.intra_score
                   << " "
                   << func_profile_info.layout_info.optimized_score.intra_score
                   << " "
                   << func_profile_info.prefetch_info.prefetch_hints.size()
                   << "\n";

      // Use the address of the function as the CFG filename for uniqueness.
      llvm::SmallString<100> cfg_dump_file_vec(cfg_dump_dir_name.begin(),
                                               cfg_dump_dir_name.end());
      llvm::sys::path::append(cfg_dump_file_vec,
                              absl::StrCat(func_addr_str, ".dot"));
      std::string cfg_dump_file(cfg_dump_file_vec.str());
      std::ofstream cfg_dump_os(cfg_dump_file, std::ofstream::out);
      CHECK(cfg_dump_os.good())
          << "Failed to open " << cfg_dump_file << " for writing.";

      absl::flat_hash_map<IntraCfgId, int> layout_index_map;
      for (auto& bb_chain : func_profile_info.layout_info.bb_chains) {
        int bbs = 0;
        for (auto& bb_bundle : bb_chain.bb_bundles) {
          for (int bbi = 0; bbi < bb_bundle.full_bb_ids.size(); ++bbi) {
            layout_index_map.insert({bb_bundle.full_bb_ids[bbi].intra_cfg_id,
                                     bb_chain.layout_index + bbs + bbi});
          }
          bbs += bb_bundle.full_bb_ids.size();
        }
      }
      cfg->WriteDotFormat(cfg_dump_os, layout_index_map,
                          func_profile_info.prefetch_info.prefetch_hints);
    }
  }
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
    int node_frequency = node.CalculateFrequency();
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

// Writes the basic block hashes in a single line which starts with the "h" marker.
// For each CFGNode, it prints out the node and hash in the following format:
// "<bb>:<bb_hash>", the bb_hash is a hexadecimal string without the "0x" prefix.
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

void PropellerProfileWriter::Write(const PropellerProfile& profile) const {
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
      if (options_.write_cfg_profile()) WriteCfgProfile(*cfg, cc_profile_os);

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
  if (options_.has_cfg_dump_dir_name())
    DumpCfgs(profile, options_.cfg_dump_dir_name());
}
}  // namespace propeller
