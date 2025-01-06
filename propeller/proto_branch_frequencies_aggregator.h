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

#ifndef PROPELLER_PROTO_BRANCH_FREQUENCIES_AGGREGATOR_H_
#define PROPELLER_PROTO_BRANCH_FREQUENCIES_AGGREGATOR_H_

#include <utility>

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/branch_frequencies_aggregator.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// `ProtoBranchFrequenciesAggregator` is an implementation of
// `BranchFrequenciesAggregator` that builds `BranchFrequencies` from a
// `BranchFrequenciesProto`.
class ProtoBranchFrequenciesAggregator : public BranchFrequenciesAggregator {
 public:
  // Directly create a ProtoBranchFrequenciesAggregator from a
  // BranchFrequenciesProto.
  static ProtoBranchFrequenciesAggregator Create(BranchFrequenciesProto proto);

  // ProtoBranchFrequenciesAggregator is copyable and movable; explicitly define
  // both the move operations and copy operations.
  ProtoBranchFrequenciesAggregator(const ProtoBranchFrequenciesAggregator&) =
      default;
  ProtoBranchFrequenciesAggregator& operator=(
      const ProtoBranchFrequenciesAggregator&) = default;
  ProtoBranchFrequenciesAggregator(ProtoBranchFrequenciesAggregator&&) =
      default;
  ProtoBranchFrequenciesAggregator& operator=(
      ProtoBranchFrequenciesAggregator&&) = default;

  absl::StatusOr<BranchFrequencies> AggregateBranchFrequencies(
      const PropellerOptions& options, const BinaryContent& binary_content,
      PropellerStats& stats) override;

 private:
  explicit ProtoBranchFrequenciesAggregator(BranchFrequenciesProto proto)
      : proto_(std::move(proto)) {}

  BranchFrequenciesProto proto_;
};

}  // namespace propeller

#endif  // PROPELLER_PROTO_BRANCH_FREQUENCIES_AGGREGATOR_H_
