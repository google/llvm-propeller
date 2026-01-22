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

#include "propeller/profile_computer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/addr2cu.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregation.h"
#include "propeller/branch_aggregator.h"
#include "propeller/cfg.h"
#include "propeller/clone_applicator.h"
#include "propeller/code_layout.h"
#include "propeller/code_prefetch_parser.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/function_layout_info.h"
#include "propeller/function_prefetch_info.h"
#include "propeller/lbr_branch_aggregator.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_aggregator.h"
#include "propeller/perf_data_path_profile_aggregator.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perf_lbr_aggregator.h"
#include "propeller/profile.h"
#include "propeller/program_cfg.h"
#include "propeller/program_cfg_builder.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_macros.h"  // Included for macros.

namespace propeller {

using ::propeller::ApplyClonings;
using ::propeller::ProgramPathProfile;

namespace {

// Assuming that the block is separated into subblocks by calls, this
// function returns the index of the subblock (0-based) that contains the given
// offset `bb_offset` in the basic block represented by `bb_entry`. Each
// subblock starts from the end of the previous call instruction (or the
// beginning of the basic block) to the end of the next call instruction (or the
// end of the basic block).
uint32_t GetSubblockIndex(const llvm::object::BBAddrMap::BBEntry& bb_entry,
                          uint32_t bb_offset) {
  return absl::c_upper_bound(bb_entry.CallsiteEndOffsets, bb_offset) -
         bb_entry.CallsiteEndOffsets.begin();
}

struct BbHandleAndSubblockIndex {
  BbHandle bb_handle;
  uint32_t subblock_index;
};

// Returns the BbHandle and subblock index for the given address, or nullopt if
// the address does not correspond to a basic block.
std::optional<BbHandleAndSubblockIndex> GetBbHandleAndSubblockIndex(
    const BinaryAddressMapper& binary_address_mapper, uint64_t address) {
  std::optional<BbHandle> bb_handle =
      binary_address_mapper.GetBbHandleUsingBinaryAddress(address,
                                                          BranchDirection::kTo);
  if (!bb_handle.has_value()) {
    return std::nullopt;
  }
  uint32_t bb_offset = address - binary_address_mapper.GetAddress(*bb_handle);
  const auto& bb_entry = binary_address_mapper.GetBBEntry(*bb_handle);
  return BbHandleAndSubblockIndex{
      .bb_handle = *bb_handle,
      .subblock_index = GetSubblockIndex(bb_entry, bb_offset)};
}

// Evaluates if Propeller options contain profiles that are specified as
// non-LBR.
bool ContainsNonLbrProfile(const PropellerOptions& options) {
  return absl::c_any_of(options.input_profiles(),
                        [](const InputProfile& profile) {
                          return profile.type() != PERF_LBR &&
                                 profile.type() != PROFILE_TYPE_UNSPECIFIED;
                        });
}

// Extracts the input profile names from Propeller options.
std::vector<std::string> ExtractProfileNames(const PropellerOptions& options) {
  std::vector<std::string> profile_names;
  profile_names.reserve(options.input_profiles_size());

  for (const InputProfile& profile : options.input_profiles()) {
    profile_names.push_back(profile.name());
  }
  return profile_names;
}

// Generates function profile infos containing prefetch hints and targets.
llvm::DenseMap<int, FunctionPrefetchInfo> GeneratePrefetchByFunctionIndex(
    const ProgramCfg& program_cfg,
    const BinaryAddressMapper& binary_address_mapper,
    absl::Span<const CodePrefetchDirective> code_prefetch_directives) {
  llvm::DenseMap<int, FunctionPrefetchInfo> function_prefetch_infos;

  for (const CodePrefetchDirective& code_prefetch_directive :
       code_prefetch_directives) {
    std::optional<BbHandleAndSubblockIndex> site_info =
        GetBbHandleAndSubblockIndex(binary_address_mapper,
                                    code_prefetch_directive.prefetch_site);
    std::optional<BbHandleAndSubblockIndex> target_info =
        GetBbHandleAndSubblockIndex(binary_address_mapper,
                                    code_prefetch_directive.prefetch_target);
    if (!site_info.has_value() || !target_info.has_value()) {
      continue;
    }
    function_prefetch_infos[site_info->bb_handle.function_index]
        .prefetch_hints.push_back(FunctionPrefetchInfo::PrefetchHint{
            .site_bb_id = static_cast<int32_t>(
                binary_address_mapper.GetBBEntry(site_info->bb_handle).ID),
            .site_callsite_index =
                static_cast<int32_t>(site_info->subblock_index),
            .target_function_index = target_info->bb_handle.function_index,
            .target_bb_id = static_cast<int32_t>(
                binary_address_mapper.GetBBEntry(target_info->bb_handle).ID),
            .target_callsite_index =
                static_cast<int32_t>(target_info->subblock_index)});
    function_prefetch_infos[target_info->bb_handle.function_index]
        .prefetch_targets.insert(FunctionPrefetchInfo::TargetBBInfo{
            .bb_id = static_cast<int32_t>(
                binary_address_mapper.GetBBEntry(target_info->bb_handle).ID),
            .callsite_index =
                static_cast<int32_t>(target_info->subblock_index)});
  }
  return function_prefetch_infos;
}
}  // namespace

absl::StatusOr<PropellerProfile> PropellerProfileComputer::ComputeProfile() && {
  CHECK_NE(program_cfg_, nullptr) << "ProgramCfg is not initialized.";
  if (program_path_profile_.has_value()) {
    program_cfg_ = ApplyClonings(
        options_.code_layout_params(), options_.path_profile_options(),
        *program_path_profile_, std::move(program_cfg_), stats_.cloning_stats);
  }

  absl::btree_map<llvm::StringRef, SectionLayoutInfo>
      layout_info_by_section_name =
          GenerateLayoutBySection(*program_cfg_, options_.code_layout_params(),
                                  stats_.code_layout_stats);

  llvm::DenseMap<int, FunctionPrefetchInfo> function_prefetch_infos =
      GeneratePrefetchByFunctionIndex(*program_cfg_, *binary_address_mapper_,
                                      code_prefetch_directives_);

  absl::btree_map<llvm::StringRef, SectionProfileInfo> section_profile_infos;

  for (auto& [section_name, section_layout_info] :
       layout_info_by_section_name) {
    for (auto& [function_index, layout_info] :
         section_layout_info.layouts_by_function_index) {
      section_profile_infos[section_name]
          .profile_infos_by_function_index[function_index]
          .layout_info = std::move(layout_info);
    }
  }

  for (auto& [function_index, prefetch_info] : function_prefetch_infos) {
    section_profile_infos[program_cfg_->GetCfgByIndex(function_index)
                              ->section_name()]
        .profile_infos_by_function_index[function_index]
        .prefetch_info = std::move(prefetch_info);
  }

  return PropellerProfile(
      {.program_cfg = std::move(program_cfg_),
       .profile_infos_by_section_name = std::move(section_profile_infos),
       .stats = std::move(stats_),
       .build_id = binary_content_->build_id});
}

absl::StatusOr<std::unique_ptr<PropellerProfileComputer>>
PropellerProfileComputer::Create(
    const PropellerOptions& options,
    ABSL_ATTRIBUTE_LIFETIME_BOUND const BinaryContent* absl_nonnull
        binary_content) {
  if (ContainsNonLbrProfile(options))
    return absl::InvalidArgumentError("non-LBR profile type");

  return Create(options, binary_content,
                std::make_unique<GenericFilePerfDataProvider>(
                    /*file_names=*/ExtractProfileNames(options)));
}

absl::StatusOr<std::unique_ptr<PropellerProfileComputer>>
PropellerProfileComputer::Create(
    const PropellerOptions& options,
    ABSL_ATTRIBUTE_LIFETIME_BOUND const BinaryContent* absl_nonnull
        binary_content,
    std::unique_ptr<PerfDataProvider> perf_data_provider) {
  if (ContainsNonLbrProfile(options))
    return absl::InvalidArgumentError("non-LBR profile type");

  auto branch_aggregator = std::make_unique<LbrBranchAggregator>(
      std::make_unique<PerfLbrAggregator>(std::move(perf_data_provider)),
      options, *binary_content);

  if (!options.path_profile_options().enable_cloning())
    return Create(options, binary_content, std::move(branch_aggregator));

  return Create(options, binary_content, std::move(branch_aggregator),
                std::make_unique<PerfDataPathProfileAggregator>(
                    options, std::make_unique<GenericFilePerfDataProvider>(
                                 /*file_names=*/ExtractProfileNames(options))));
}

absl::StatusOr<std::unique_ptr<PropellerProfileComputer>>
PropellerProfileComputer::Create(
    const PropellerOptions& options,
    ABSL_ATTRIBUTE_LIFETIME_BOUND const BinaryContent* absl_nonnull
        binary_content,
    std::unique_ptr<BranchAggregator> branch_aggregator,
    std::unique_ptr<PathProfileAggregator> path_profile_aggregator) {
  std::unique_ptr<PropellerProfileComputer> profile_computer =
      absl::WrapUnique(new PropellerProfileComputer(
          options, binary_content, std::move(branch_aggregator),
          std::move(path_profile_aggregator)));
  RETURN_IF_ERROR(profile_computer->InitializeProgramProfile());
  return profile_computer;
}

// "InitializeProgramProfile" steps:
//   1. Calls branch_aggregator_->GetBranchEndpointAddresses().
//   2. Initializes `binary_address_mapper_`.
//   3. Calls branch_aggregator_->Aggregate() to get `branch_aggregation`.
//   4. ProgramCfgBuilder::Build to initialize `program_cfg_`.
//   5. If cloning is enabled and we have LBR profiles, calls
//   ConvertPerfDataToPathProfile to
//      initialize `program_path_profile_`.
absl::Status PropellerProfileComputer::InitializeProgramProfile() {
  llvm::DenseSet<uint64_t> unique_addresses;
  if (branch_aggregator_ != nullptr) {
    ASSIGN_OR_RETURN(unique_addresses,
                     branch_aggregator_->GetBranchEndpointAddresses());
  }
  if (!options_.prefetch_directives_path().empty()) {
    ASSIGN_OR_RETURN(
        code_prefetch_directives_,
        ReadCodePrefetchDirectives(options_.prefetch_directives_path()));
    for (const auto& code_prefetch_directive : code_prefetch_directives_) {
      unique_addresses.insert(code_prefetch_directive.prefetch_site);
      unique_addresses.insert(code_prefetch_directive.prefetch_target);
    }
  }

  ASSIGN_OR_RETURN(binary_address_mapper_,
                   BuildBinaryAddressMapper(options_, *binary_content_, stats_,
                                            &unique_addresses));

  BranchAggregation branch_aggregation;
  if (branch_aggregator_ != nullptr) {
    ASSIGN_OR_RETURN(branch_aggregation, branch_aggregator_->Aggregate(
                                             *binary_address_mapper_, stats_));
  }

  std::unique_ptr<Addr2Cu> addr2cu;
  if (options_.output_module_name()) {
    if (binary_content_->dwarf_context != nullptr) {
      addr2cu = std::make_unique<Addr2Cu>(*binary_content_->dwarf_context);
    } else {
      return absl::FailedPreconditionError(absl::StrFormat(
          "no DWARFContext is available for '%s'. Either because it does not "
          "have debuginfo, or '%s.dwp' does not exist.",
          options_.binary_name().c_str(), options_.binary_name().c_str()));
    }
  }
  ASSIGN_OR_RETURN(program_cfg_,
                   ProgramCfgBuilder(binary_address_mapper_.get(), stats_)
                       .Build(branch_aggregation, addr2cu.get()));

  if (path_profile_aggregator_ != nullptr) {
    ASSIGN_OR_RETURN(
        program_path_profile_,
        path_profile_aggregator_->Aggregate(
            *binary_content_, *binary_address_mapper_, *program_cfg_));
  }
  return absl::OkStatus();
}
}  // namespace propeller
