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

#ifndef PROPELLER_BRANCH_AGGREGATOR_H_
#define PROPELLER_BRANCH_AGGREGATOR_H_

#include <cstdint>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/branch_aggregation.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// `BranchAggregator` is an abstraction around producing a `BranchAggregation`,
// making the source of the branch data (SPE, LBR) and profile (memtrace, perf)
// opaque to the user.
class BranchAggregator {
 public:
  virtual ~BranchAggregator() = default;

  // Gets the set of branch endpoint addresses (i.e. the set of addresses which
  // are either the source or target of a branch or fallthrough).
  virtual absl::StatusOr<absl::flat_hash_set<uint64_t>>
  GetBranchEndpointAddresses() = 0;

  // Returns a `BranchAggregation` for the binary mapped by
  // `binary_address_mapper`, or an `absl::Status` if a valid aggregation can't
  // be produced. Updates relevant Propeller statistics if aggregation succeeds;
  // otherwise, leaves `stats` in an undefined state.
  virtual absl::StatusOr<BranchAggregation> Aggregate(
      const BinaryAddressMapper& binary_address_mapper,
      PropellerStats& stats) = 0;
};
}  // namespace propeller

#endif  // PROPELLER_BRANCH_AGGREGATOR_H_
