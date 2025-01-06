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

#include "propeller/proto_branch_frequencies_aggregator.h"

#include <utility>

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

ProtoBranchFrequenciesAggregator ProtoBranchFrequenciesAggregator::Create(
    BranchFrequenciesProto proto) {
  return ProtoBranchFrequenciesAggregator(std::move(proto));
}

absl::StatusOr<BranchFrequencies>
ProtoBranchFrequenciesAggregator::AggregateBranchFrequencies(
    const PropellerOptions& options, const BinaryContent& binary_content,
    PropellerStats& stats) {
  return BranchFrequencies::Create(proto_);
}

}  // namespace propeller
