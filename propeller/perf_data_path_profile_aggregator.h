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

#ifndef PROPELLER_PERF_DATA_PATH_PROFILE_AGGREGATOR_H_
#define PROPELLER_PERF_DATA_PATH_PROFILE_AGGREGATOR_H_

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_aggregator.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/perf_data_provider.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
namespace propeller {

// Aggregates path profiles from perf data.
class PerfDataPathProfileAggregator : public PathProfileAggregator {
 public:
  PerfDataPathProfileAggregator(
      const PropellerOptions &propeller_options,
      std::unique_ptr<PerfDataProvider> perf_data_provider)
      : propeller_options_(propeller_options),
        perf_data_provider_(std::move(perf_data_provider)) {}

  PerfDataPathProfileAggregator(const PerfDataPathProfileAggregator &) = delete;
  PerfDataPathProfileAggregator &operator=(
      const PerfDataPathProfileAggregator &) = delete;
  PerfDataPathProfileAggregator(PerfDataPathProfileAggregator &&) noexcept =
      delete;
  PerfDataPathProfileAggregator &operator=(
      PerfDataPathProfileAggregator &&) noexcept = delete;

  absl::StatusOr<propeller::ProgramPathProfile> Aggregate(
      const BinaryContent &binary_content,
      const BinaryAddressMapper &binary_address_mapper,
      const ProgramCfg &program_cfg) override;

 private:
  const PropellerOptions &propeller_options_;
  std::unique_ptr<PerfDataProvider> perf_data_provider_;
};

}  // namespace propeller
#endif  // PROPELLER_PERF_DATA_PATH_PROFILE_AGGREGATOR_H_
