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

#include "propeller/bb_addr_map.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/addr2cu.h"
#include "propeller/bb_addr_map.pb.h"
#include "propeller/binary_content.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

namespace {

std::optional<absl::string_view> GetModuleName(
    const propeller::Addr2Cu* addr2cu, uint64_t function_address) {
  if (addr2cu == nullptr) return std::nullopt;
  auto cu_info =
      addr2cu->GetCompileUnitFileNameForCodeAddress(function_address);
  if (!cu_info.ok()) {
    LOG(ERROR) << "Failed to get module name for function address: "
               << function_address << ": " << cu_info.status().message();
    return std::nullopt;
  }
  return *cu_info;
}
}  // namespace

BbAddrMapPb GetBbAddrMap(const std::string& binary_path) {
  propeller::PropellerOptions options;
  options.set_binary_name(binary_path);
  propeller::PropellerStats stats;
  absl::StatusOr<std::unique_ptr<propeller::BinaryContent>> binary_content =
      propeller::GetBinaryContent(binary_path);
  if (!binary_content.ok()) {
    LOG(ERROR) << "Failed to get binary content: ",
        binary_content.status().message();
    return {};
  }
  auto symbol_info_map = propeller::GetSymbolInfoMap(**binary_content);
  std::unique_ptr<propeller::Addr2Cu> addr2cu = nullptr;
  if ((*binary_content)->dwarf_context != nullptr) {
    addr2cu =
        std::make_unique<propeller::Addr2Cu>(*(*binary_content)->dwarf_context);
  } else {
    LOG(WARNING) << "No DWARFContext is available for '" << binary_path
                 << "'. Either because it does not "
                    "have debuginfo, or '"
                 << binary_path << ".dwp' does not exist.";
  }
  absl::StatusOr<propeller::BbAddrMapData> bb_addr_map =
      propeller::ReadBbAddrMap(**binary_content, {.read_pgo_analyses = true});
  if (!bb_addr_map.ok()) {
    LOG(ERROR) << "Failed to read BBAddrMap: ", bb_addr_map.status().message();
    return {};
  }

  absl::flat_hash_map<std::optional<std::string>,
                      std::unique_ptr<ModuleBbAddrMapPb>>
      module_bb_addr_maps_by_name;

  for (int bb_addr_map_index = 0;
       bb_addr_map_index < bb_addr_map->bb_addr_maps.size();
       ++bb_addr_map_index) {
    const auto& function_bb_addr_map =
        bb_addr_map->bb_addr_maps[bb_addr_map_index];
    auto module_name =
        GetModuleName(addr2cu.get(), function_bb_addr_map.getFunctionAddress());

    auto [module_it, inserted] = module_bb_addr_maps_by_name.emplace(
        module_name, std::make_unique<ModuleBbAddrMapPb>());
    if (inserted && module_name.has_value()) {
      module_it->second->set_module_name(module_name.value());
    }

    FunctionBbAddrMapPb* function_bb_addr_map_pb =
        module_it->second->add_function_bb_addr_maps();
    function_bb_addr_map_pb->set_function_address(
        function_bb_addr_map.getFunctionAddress());

    auto symbol_it =
        symbol_info_map.find(function_bb_addr_map.getFunctionAddress());
    if (symbol_it == symbol_info_map.end()) {
      LOG(ERROR) << "Failed to find symbol for function address: "
                 << function_bb_addr_map.getFunctionAddress();
    } else {
      for (const auto& alias : symbol_it->second.aliases) {
        function_bb_addr_map_pb->add_function_names(alias.str());
      }
      function_bb_addr_map_pb->set_section_name(symbol_it->second.section_name);
    }

    const llvm::object::PGOAnalysisMap* pgo_analysis_map = nullptr;
    if (bb_addr_map->pgo_analyses.has_value()) {
      pgo_analysis_map = &(*bb_addr_map->pgo_analyses)[bb_addr_map_index];
    }
    int bb_index = 0;
    // Scaling factor for the frequencies of the basic blocks. PGO analysis map
    // stores the block frequencies relative to the function entry count. This
    // is used to get the absolute frequencies.
    float frequency_scale = 0.0;
    for (const auto& bb_range : function_bb_addr_map.getBBRanges()) {
      BbRangePb* bb_range_pb = function_bb_addr_map_pb->add_bb_ranges();
      bb_range_pb->set_base_address(bb_range.BaseAddress);

      for (int i = 0; i < bb_range.BBEntries.size(); ++i) {
        const auto& bb_entry = bb_range.BBEntries[i];
        BbEntryPb* bb_entry_pb = bb_range_pb->add_bb_entries();
        bb_entry_pb->set_index(i);
        bb_entry_pb->set_id(bb_entry.ID);
        bb_entry_pb->set_offset(bb_entry.Offset);
        bb_entry_pb->set_size(bb_entry.Size);
        bb_entry_pb->set_has_return(bb_entry.hasReturn());
        bb_entry_pb->set_has_indirect_branch(bb_entry.hasIndirectBranch());
        bb_entry_pb->set_is_eh_pad(bb_entry.isEHPad());
        bb_entry_pb->set_can_fall_through(bb_entry.canFallThrough());
        bb_entry_pb->set_has_tail_call(bb_entry.hasTailCall());
        for (uint32_t callsite : bb_entry.CallsiteEndOffsets) {
          CallsitePb* callsite_pb = bb_entry_pb->add_callsites();
          callsite_pb->set_offset(callsite);
        }
        if (pgo_analysis_map != nullptr &&
            !pgo_analysis_map->BBEntries.empty()) {
          bb_entry_pb->set_post_link_frequency(static_cast<int64_t>(
              pgo_analysis_map->BBEntries.at(bb_index).PostLinkBlockFreq));
          if (bb_index == 0) {
            bb_entry_pb->set_frequency(pgo_analysis_map->FuncEntryCount);
            // Capture the frequency scale based on the first basic block.
            frequency_scale =
                static_cast<float>(pgo_analysis_map->BBEntries.at(bb_index)
                                       .BlockFreq.getFrequency()) /
                std::max(1.0f, bb_entry_pb->frequency());
          } else {
            bb_entry_pb->set_frequency(pgo_analysis_map->BBEntries.at(bb_index)
                                           .BlockFreq.getFrequency() /
                                       frequency_scale);
          }
          for (const auto& successor :
               pgo_analysis_map->BBEntries.at(bb_index).Successors) {
            EdgePb* edge_pb = bb_entry_pb->add_edges();
            edge_pb->set_dest_bb_id(successor.ID);
            // Compute the raw edge frequency as the product of the block
            // frequency and the edge probability.
            edge_pb->set_frequency(bb_entry_pb->frequency() *
                                   successor.Prob.getNumerator() /
                                   successor.Prob.getDenominator());
            edge_pb->set_post_link_frequency(
                static_cast<int64_t>(successor.PostLinkFreq));
          }
        }
        ++bb_index;
      }
    }
  }
  BbAddrMapPb bb_addr_map_pb;
  for (auto& [module_name, module_bb_addr_map] : module_bb_addr_maps_by_name) {
    bb_addr_map_pb.mutable_module_bb_addr_maps()->AddAllocated(
        module_bb_addr_map.release());
  }
  return bb_addr_map_pb;
}
}  // namespace propeller
