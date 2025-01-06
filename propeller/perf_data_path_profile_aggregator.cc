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

#include "propeller/perf_data_path_profile_aggregator.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/statusor.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/path_node.h"
#include "propeller/perf_data_path_reader.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perfdata_reader.h"
#include "propeller/program_cfg.h"
#include "propeller/program_cfg_path_analyzer.h"
#include "propeller/resolve_mmap_name.h"
#include "propeller/status_macros.h"

namespace propeller {
using ::propeller::ProgramCfgPathAnalyzer;
using ::propeller::ProgramPathProfile;

absl::StatusOr<ProgramPathProfile> PerfDataPathProfileAggregator::Aggregate(
    const BinaryContent &binary_content,
    const BinaryAddressMapper &binary_address_mapper,
    const ProgramCfg &program_cfg) {
  ProgramPathProfile program_path_profile;
  ProgramCfgPathAnalyzer path_analyzer(
      &propeller_options_.path_profile_options(), &program_cfg,
      &program_path_profile);
  while (true) {
    ASSIGN_OR_RETURN(std::optional<PerfDataProvider::BufferHandle> perf_data,
                     perf_data_provider_->GetNext());
    if (!perf_data.has_value()) break;
    std::string description = perf_data->description;
    LOG(INFO) << "Parsing " << description << " ...";
    absl::StatusOr<PerfDataReader> perf_data_reader =
        BuildPerfDataReader(*std::move(perf_data), &binary_content,
                            ResolveMmapName(propeller_options_));
    if (!perf_data_reader.ok()) {
      LOG(WARNING) << "Skipped profile " << description << ": "
                   << perf_data_reader.status();
      continue;
    }

    PerfDataPathReader(&*perf_data_reader, &binary_address_mapper)
        .ReadPathsAndApplyCallBack(absl::bind_front(
            &ProgramCfgPathAnalyzer::StoreAndAnalyzePaths, &path_analyzer));
    // Analyze the remaining paths.
    path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  }
  if (VLOG_IS_ON(1)) {
    for (const auto &[function_index, function_path_profile] :
         program_path_profile.path_profiles_by_function_index()) {
      LOG(INFO) << "Path tree for function: " << function_index << ":\n";
      for (const auto &[bb_index, path_tree] :
           function_path_profile.path_trees_by_root_bb_index())
        LOG(INFO) << *path_tree << "\n";
    }
  }
  return program_path_profile;
}
}  // namespace propeller
