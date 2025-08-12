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

#include "propeller/branch_aggregation.h"

#include <cstdint>

#include "absl/container/flat_hash_set.h"

namespace propeller {
absl::flat_hash_set<uint64_t> BranchAggregation::GetUniqueAddresses() const {
  absl::flat_hash_set<uint64_t> unique_addresses;
  for (const auto& [branch, _] : branch_counters) {
    unique_addresses.insert(branch.from);
    unique_addresses.insert(branch.to);
  }
  for (const auto& [fallthrough, _] : fallthrough_counters) {
    unique_addresses.insert(fallthrough.from);
    unique_addresses.insert(fallthrough.to);
  }
  return unique_addresses;
}

}  // namespace propeller
