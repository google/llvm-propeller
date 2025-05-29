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
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/addr2cu.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregation.h"
#include "propeller/branch_aggregator.h"
#include "propeller/clone_applicator.h"
#include "propeller/code_layout.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/function_chain_info.h"
#include "propeller/lbr_branch_aggregator.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_aggregator.h"
#include "propeller/perf_data_path_profile_aggregator.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perf_lbr_aggregator.h"
#include "propeller/profile.h"
#include "propeller/program_cfg_builder.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_macros.h"  // Included for macros.

namespace propeller {

using ::propeller::ApplyClonings;
using ::propeller::ProgramPathProfile;

namespace {
// Evaluates if Propeller options contain profiles that are specified as
// non-LBR.
bool ContainsNonLbrProfile(const PropellerOptions &options) {
  return absl::c_any_of(options.input_profiles(),
                        [](const InputProfile &profile) {
                          return profile.type() != PERF_LBR &&
                                 profile.type() != PROFILE_TYPE_UNSPECIFIED;
                        });
}

// Extracts the input profile names from Propeller options.
std::vector<std::string> ExtractProfileNames(const PropellerOptions &options) {
  std::vector<std::string> profile_names;
  profile_names.reserve(options.input_profiles_size());

  absl::c_transform(options.input_profiles(), std::back_inserter(profile_names),
                    [](const InputProfile &profile) { return profile.name(); });

  return profile_names;
}
}  // namespace

absl::StatusOr<PropellerProfile> PropellerProfileComputer::ComputeProfile() && {
  CHECK_NE(program_cfg_, nullptr) << "ProgramCfg is not initialized.";
  if (program_path_profile_.has_value()) {
    program_cfg_ = ApplyClonings(
        options_.code_layout_params(), options_.path_profile_options(),
        *program_path_profile_, std::move(program_cfg_), stats_.cloning_stats);
  }

  absl::btree_map<llvm::StringRef, std::vector<FunctionChainInfo>>
      chain_info_by_section_name =
          GenerateLayoutBySection(*program_cfg_, options_.code_layout_params(),
                                  stats_.code_layout_stats);

  return PropellerProfile({.program_cfg = std::move(program_cfg_),
                           .functions_chain_info_by_section_name =
                               std::move(chain_info_by_section_name),
                           .stats = std::move(stats_)});
}

absl::StatusOr<std::unique_ptr<PropellerProfileComputer>>
PropellerProfileComputer::Create(
    const PropellerOptions &options,
    ABSL_ATTRIBUTE_LIFETIME_BOUND const BinaryContent
        *absl_nonnull binary_content) {
  if (ContainsNonLbrProfile(options))
    return absl::InvalidArgumentError("non-LBR profile type");

  return Create(options, binary_content,
                std::make_unique<GenericFilePerfDataProvider>(
                    /*file_names=*/ExtractProfileNames(options)));
}

absl::StatusOr<std::unique_ptr<PropellerProfileComputer>>
PropellerProfileComputer::Create(
    const PropellerOptions &options,
    ABSL_ATTRIBUTE_LIFETIME_BOUND const BinaryContent
        *absl_nonnull binary_content,
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
    const PropellerOptions &options,
    ABSL_ATTRIBUTE_LIFETIME_BOUND const BinaryContent
        *absl_nonnull binary_content,
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
  ASSIGN_OR_RETURN(absl::flat_hash_set<uint64_t> unique_addresses,
                   branch_aggregator_->GetBranchEndpointAddresses());

  ASSIGN_OR_RETURN(binary_address_mapper_,
                   BuildBinaryAddressMapper(options_, *binary_content_, stats_,
                                            &unique_addresses));

  ASSIGN_OR_RETURN(
      BranchAggregation branch_aggregation,
      branch_aggregator_->Aggregate(*binary_address_mapper_, stats_));

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
