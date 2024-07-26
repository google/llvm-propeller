#include "propeller/branch_aggregation.h"

#include <cstdint>

#include "absl/container/flat_hash_set.h"

namespace propeller {
absl::flat_hash_set<uint64_t> BranchAggregation::GetUniqueAddresses() const {
  absl::flat_hash_set<uint64_t> unique_addresses;
  for (const auto &[branch, _] : branch_counters) {
    unique_addresses.insert(branch.from);
    unique_addresses.insert(branch.to);
  }
  for (const auto &[fallthrough, _] : fallthrough_counters) {
    unique_addresses.insert(fallthrough.from);
    unique_addresses.insert(fallthrough.to);
  }
  return unique_addresses;
}

}  // namespace propeller
