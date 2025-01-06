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

#include "propeller/branch_frequencies.h"

#include "propeller/binary_address_branch.h"
#include "propeller/branch_frequencies.pb.h"

namespace propeller {

BranchFrequencies BranchFrequencies::Create(
    const BranchFrequenciesProto& proto) {
  BranchFrequencies frequencies;
  for (const TakenBranchCount& taken : proto.taken_counts()) {
    frequencies
        .taken_branch_counters[{.from = taken.source(), .to = taken.dest()}] +=
        taken.count();
  }
  for (const NotTakenBranchCount& not_taken : proto.not_taken_counts()) {
    BinaryAddressNotTakenBranch branch = {.address = not_taken.address()};
    frequencies.not_taken_branch_counters[branch] += not_taken.count();
  }
  return frequencies;
}

BranchFrequenciesProto BranchFrequencies::ToProto() const {
  BranchFrequenciesProto proto;
  for (const auto& [taken_branch, count] : taken_branch_counters) {
    TakenBranchCount* added = proto.add_taken_counts();
    added->set_source(taken_branch.from);
    added->set_dest(taken_branch.to);
    added->set_count(count);
  }
  for (const auto& [not_taken_branch, count] : not_taken_branch_counters) {
    NotTakenBranchCount* added = proto.add_not_taken_counts();
    added->set_address(not_taken_branch.address);
    added->set_count(count);
  }
  return proto;
}
}  // namespace propeller
