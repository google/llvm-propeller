#ifndef PROPELLER_BRANCH_FREQUENCIES_H_
#define PROPELLER_BRANCH_FREQUENCIES_H_

#include <cstdint>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
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
  absl::flat_hash_map<BinaryAddressBranch, int64_t> taken_branch_counters;
  // The number of times each branch was not taken, keyed by the binary address
  // of the instruction.
  absl::flat_hash_map<BinaryAddressNotTakenBranch, int64_t>
      not_taken_branch_counters;
};
}  // namespace propeller
#endif  // PROPELLER_BRANCH_FREQUENCIES_H_
