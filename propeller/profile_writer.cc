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
#include "propeller/function_chain_info.h"
#include "propeller/profile.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
namespace {
void DumpCfgs(const PropellerProfile &profile,
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
                                 "Optimized.ExtTSP.Score"},
                                " ")
               << "\n";

  for (const auto &[section_name, section_function_chain_info] :
       profile.functions_chain_info_by_section_name) {
    for (const FunctionChainInfo &func_chain_info :
         section_function_chain_info) {
      const ControlFlowGraph *cfg =
          profile.program_cfg->GetCfgByIndex(func_chain_info.function_index);
      CHECK_NE(cfg, nullptr);
      // Dump hot cfgs into the given directory.
      auto func_addr_str =
          absl::StrCat("0x", absl::Hex(cfg->GetEntryNode()->addr()));
      cfg_index_os << cfg->GetPrimaryName().str() << " " << func_addr_str << " "
                   << cfg->nodes().size() << " "
                   << func_chain_info.bb_chains.size() << " "
                   << func_chain_info.original_score.intra_score << " "
                   << func_chain_info.optimized_score.intra_score << "\n";

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
      for (auto &bb_chain : func_chain_info.bb_chains) {
        int bbs = 0;
        for (auto &bb_bundle : bb_chain.bb_bundles) {
          for (int bbi = 0; bbi < bb_bundle.full_bb_ids.size(); ++bbi) {
            layout_index_map.insert({bb_bundle.full_bb_ids[bbi].intra_cfg_id,
                                     bb_chain.layout_index + bbs + bbi});
          }
          bbs += bb_bundle.full_bb_ids.size();
        }
      }

      cfg->WriteDotFormat(cfg_dump_os, layout_index_map);
    }
  }
}

// Writes the intra-function edge profile of `cfg` into `out` in a single line
// which starts with the "#cfg" marker.
// For each CFGNode with non-zero frequency, it prints out the node and edge
// frequencies in the following format:
// "<bb>:<bb_freq>,<succ_bb_1>:<edge_freq_1>,<succ_bb_2>:<edge_freq_2>,..."
// which starts first with the full bb id and frequency of that node, followed
// by the successors and their edge frequencies. Please note that the edge
// weights may not precisely add up to the node frequency.
void WriteCfgProfile(const ControlFlowGraph &cfg, std::ofstream &out) {
  out << "#cfg";
  cfg.ForEachNodeRef([&](const CFGNode &node) {
    int node_frequency = node.CalculateFrequency();
    if (node_frequency == 0) return;
    out << " " << node.full_intra_cfg_id().profile_bb_id() << ":"
        << node_frequency;
    node.ForEachOutEdgeInOrder([&](const CFGEdge &edge) {
      if (!edge.IsBranchOrFallthrough()) return;
      out << "," << edge.sink()->full_intra_cfg_id().profile_bb_id() << ":"
          << edge.weight();
    });
  });
  out << "\n";
}
}  // namespace

void PropellerProfileWriter::Write(const PropellerProfile &profile) const {
  std::ofstream cc_profile_os(options_.cluster_out_name());
  std::ofstream ld_profile_os(options_.symbol_order_out_name());
  if (profile_encoding_.version != ClusterEncodingVersion::VERSION_0) {
    cc_profile_os << profile_encoding_.version_specifier << "\n";
  }
  // TODO(b/160339651): Remove this in favour of structured format in LLVM code.
  for (const auto &[section_name, section_function_chain_info] :
       profile.functions_chain_info_by_section_name) {
    if (options_.verbose_cluster_output())
      cc_profile_os << "#section " << section_name.str() << "\n";
    // Find total number of chains.
    unsigned total_chains = 0;
    for (const auto &func_chain_info : section_function_chain_info)
      total_chains += func_chain_info.bb_chains.size();

    // Allocate the symbol order vector
    std::vector<std::pair<llvm::SmallVector<llvm::StringRef, 3>,
                          std::optional<unsigned>>>
        symbol_order(total_chains);
    // Allocate the cold symbol order vector equally sized as
    // function_chain_info, as there is (at most) one cold cluster per
    // function.
    std::vector<const FunctionChainInfo *> cold_symbol_order(
        section_function_chain_info.size());
    for (const FunctionChainInfo &func_layout_info :
         section_function_chain_info) {
      const ControlFlowGraph *cfg =
          profile.program_cfg->GetCfgByIndex(func_layout_info.function_index);
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
      for (const std::vector<int> &clone_path : cfg->clone_paths()) {
        cc_profile_os << profile_encoding_.clone_path_specifier
                      << absl::StrJoin(
                             clone_path, " ",
                             [&](std::string *out, const int bb_index) {
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
            func_layout_info.original_score.intra_score,
            func_layout_info.optimized_score.intra_score,
            func_layout_info.original_score.inter_out_score,
            func_layout_info.optimized_score.inter_out_score);
      }
      const std::vector<FunctionChainInfo::BbChain> &chains =
          func_layout_info.bb_chains;
      for (unsigned chain_id = 0; chain_id < chains.size(); ++chain_id) {
        auto &chain = chains[chain_id];
        std::vector<FullIntraCfgId> bb_ids_in_chain =
            chains[chain_id].GetAllBbs();
        // If a chain starts with zero BB index (function entry basic block),
        // the function name is sufficient for section ordering. Otherwise,
        // the chain number is required.
        symbol_order[chain.layout_index] =
            std::pair<llvm::SmallVector<llvm::StringRef, 3>,
                      std::optional<unsigned>>(
                cfg->names(), bb_ids_in_chain.front().intra_cfg_id.bb_index == 0
                                  ? std::optional<unsigned>()
                                  : chain_id);
        for (int bbi = 0; bbi < bb_ids_in_chain.size(); ++bbi) {
          const auto &full_bb_id = bb_ids_in_chain[bbi];
          cc_profile_os << (bbi != 0 ? " "
                                     : profile_encoding_.cluster_specifier)
                        << full_bb_id.profile_bb_id();
        }
        cc_profile_os << "\n";
      }

      // Dump the edge profile for this CFG if requested.
      if (options_.write_cfg_profile()) WriteCfgProfile(*cfg, cc_profile_os);

      cold_symbol_order[func_layout_info.cold_chain_layout_index] =
          &func_layout_info;
    }

    for (const auto &[func_names, chain_id] : symbol_order) {
      // Print the symbol names corresponding to every function name alias. This
      // guarantees we get the right order regardless of which function name is
      // picked by the compiler.
      for (auto &func_name : func_names) {
        ld_profile_os << func_name.str();
        if (chain_id.has_value())
          ld_profile_os << ".__part." << chain_id.value();
        ld_profile_os << "\n";
      }
    }

    // Insert the .cold symbols for cold parts of hot functions.
    for (const FunctionChainInfo *chain_info : cold_symbol_order) {
      const ControlFlowGraph *cfg =
          profile.program_cfg->GetCfgByIndex(chain_info->function_index);
      CHECK_NE(cfg, nullptr);
      // The cold node should not be emitted if all basic blocks appear in the
      // chains.
      int num_bbs_in_chains = 0;
      for (const FunctionChainInfo::BbChain &chain : chain_info->bb_chains)
        num_bbs_in_chains += chain.GetNumBbs();
      if (num_bbs_in_chains == cfg->nodes().size()) continue;
      // Check if the function entry is in the chains. The entry node always
      // begins its chain. So this simply checks the first node in every
      // chain.
      bool entry_is_in_chains = absl::c_any_of(
          chain_info->bb_chains, [](const FunctionChainInfo::BbChain &chain) {
            return chain.GetFirstBb().intra_cfg_id.bb_index == 0;
          });
      for (auto &func_name : cfg->names()) {
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
