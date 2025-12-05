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

#ifndef PROPELLER_BRANCH_FREQUENCIES_H_
#define PROPELLER_BRANCH_FREQUENCIES_H_

#include <cstdint>

#include "absl/algorithm/container.h"
#include "llvm/ADT/DenseMap.h"
#include "propeller/binary_address_branch.h"
#include "propeller/branch_frequencies.pb.h"

namespace propeller {
// `BranchFrequencies` represents the taken/not-taken frequencies for
// the branches in a binary.
struct BranchFrequencies {
  // Deserializes a `BranchFrequenciesProto` into a `BranchFrequencies`.
  static BranchFrequencies Create(const BranchFrequenciesProto& proto);

  // Serializes a `BranchFrequencies` into a `BranchFrequenciesProto`.
  BranchFrequenciesProto ToProto() const;

  // Computes the sum of all taken branch counters.
  int64_t GetNumberOfTakenBranchCounters() const {
    return absl::c_accumulate(
        taken_branch_counters, 0,
        [](int64_t cnt, const auto& v) { return cnt + v.second; });
  }

  // The number of times each branch was taken, keyed by the binary address of
  // its source and destination.
  llvm::DenseMap<BinaryAddressBranch, int64_t> taken_branch_counters;
  // The number of times each branch was not taken, keyed by the binary address
  // of the instruction.
  llvm::DenseMap<BinaryAddressNotTakenBranch, int64_t>
      not_taken_branch_counters;
};
}  // namespace propeller
#endif  // PROPELLER_BRANCH_FREQUENCIES_H_
