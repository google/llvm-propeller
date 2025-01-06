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

#ifndef PROPELLER_PERF_BRANCH_FREQUENCIES_AGGREGATOR_H_
#define PROPELLER_PERF_BRANCH_FREQUENCIES_AGGREGATOR_H_

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies_aggregator.h"
#include "propeller/perf_data_provider.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// `PerfBranchFrequenciesAggregator` is an implementation of
// `BranchFrequenciesAggregator` that builds `BranchFrequencies` from perf data
// containing SPE entries. The perf data can come from any `PerfDataProvider`,
// such as from a file or mock.
class PerfBranchFrequenciesAggregator : public BranchFrequenciesAggregator {
 public:
  explicit PerfBranchFrequenciesAggregator(
      std::unique_ptr<PerfDataProvider> perf_data_provider)
      : perf_data_provider_(std::move(perf_data_provider)) {}

  // PerfBranchFrequenciesAggregator is move-only.
  PerfBranchFrequenciesAggregator(PerfBranchFrequenciesAggregator&&) = default;
  PerfBranchFrequenciesAggregator& operator=(
      PerfBranchFrequenciesAggregator&&) = default;
  PerfBranchFrequenciesAggregator(const PerfBranchFrequenciesAggregator&) =
      delete;
  PerfBranchFrequenciesAggregator& operator=(
      const PerfBranchFrequenciesAggregator&) = delete;

  // Aggregates branch frequencies from perf data, may return an `absl::Status`
  // if the perf data can't be successfully parsed and aggregated (it doesn't
  // exist, is malformed, etc.).
  absl::StatusOr<BranchFrequencies> AggregateBranchFrequencies(
      const PropellerOptions& options, const BinaryContent& binary_content,
      PropellerStats& stats) override;

 private:
  std::unique_ptr<PerfDataProvider> perf_data_provider_;
};

}  // namespace propeller

#endif  // PROPELLER_PERF_BRANCH_FREQUENCIES_AGGREGATOR_H_
