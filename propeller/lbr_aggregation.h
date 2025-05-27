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

#ifndef PROPELLER_LBR_AGGREGATION_H_
#define PROPELLER_LBR_AGGREGATION_H_

#include <cstdint>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "propeller/binary_address_branch.h"

namespace propeller {
// An aggregation of LBR-like data, which encodes a sequence of consecutive
// branches. `LbrAggregation` contains aggregated information about single
// branches and resulting fallthroughs. For example, for the following LBR
// entry: [
//   { from: 0x10, to: 0x20 },
//   { from: 0x40, to: 0x20 },
//   { from: 0x40, to: 0x20 },
// ], the resulting `LbrAggregation` encodes that the branch from 0x10 to 0x20
// was taken once, the branch from 0x40 to 0x20 was taken twice, and the
// fallthrough range from 0x20 to 0x40 was serially executed twice.
struct LbrAggregation {
  int64_t GetNumberOfBranchCounters() const {
    return absl::c_accumulate(
        branch_counters, 0,
        [](int64_t cnt, const auto &v) { return cnt + v.second; });
  }

  // A count of the number of times each branch was taken.
  absl::flat_hash_map<BinaryAddressBranch, int64_t> branch_counters;
  // A count of the number of times each fallthrough range (a fully-closed
  // interval) was serially taken. Given an instruction at binary address
  // `addr`, we can infer that the number of times the instruction was executed
  // is equal to the sum of counts for every fallthrough range that contains
  // `addr`.
  absl::flat_hash_map<BinaryAddressFallthrough, int64_t> fallthrough_counters;
};

}  // namespace propeller
#endif  // PROPELLER_LBR_AGGREGATION_H_
