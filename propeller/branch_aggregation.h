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

#ifndef PROPELLER_BRANCH_AGGREGATION_H_
#define PROPELLER_BRANCH_AGGREGATION_H_

#include <cstdint>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "propeller/binary_address_branch.h"

namespace propeller {
// An aggregation of branch/fallthrough data, which can be obtained from LBR,
// SPE, or any other profiling source. `BranchAggregation` contains aggregated
// information about single branches and fallthroughs.
//
// `BranchAggregation`'s fallthrough counters indicate control flow transfer,
// but they do not directly encode how many times each instruction is executed.
// This property differs from `LbrAggregation`, whose fallthrough counts encode
// both control flow transfer and instruction execution count.
struct BranchAggregation {
  int64_t GetNumberOfBranchCounters() const {
    return absl::c_accumulate(
        branch_counters, int64_t{0},
        [](int64_t cnt, const auto& v) { return cnt + v.second; });
  }

  // Returns the set of unique addresses. An aggregation's addresses can come
  // from the `from` and `to` addresses of the keys in `branch_counters` and
  // `fallthrough_counters`.
  absl::flat_hash_set<uint64_t> GetUniqueAddresses() const;

  // A count of the number of times each branch was taken.
  absl::flat_hash_map<BinaryAddressBranch, int64_t> branch_counters;
  // A count of the number of times each fallthrough range (a fully-closed,
  // sequentially-executed interval) was taken.
  absl::flat_hash_map<BinaryAddressFallthrough, int64_t> fallthrough_counters;
};

}  // namespace propeller

#endif  // PROPELLER_BRANCH_AGGREGATION_H_
