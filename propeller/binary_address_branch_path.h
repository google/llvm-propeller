#ifndef PROPELLER_BINARY_ADDRESS_BRANCH_PATH_H_
#define PROPELLER_BINARY_ADDRESS_BRANCH_PATH_H_

#include <cstdint>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "propeller/binary_address_branch.h"

namespace propeller {

struct BinaryAddressBranchPath {
  int64_t pid;
  absl::Time sample_time;
  std::vector<BinaryAddressBranch> branches;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const BinaryAddressBranchPath &path) {
    absl::Format(&sink, "BinaryAddressBranchPath[pid:%lld, branches:%s]",
                 path.pid, absl::StrJoin(path.branches, ", "));
  }
};
}  // namespace propeller

#endif  // PROPELLER_BINARY_ADDRESS_BRANCH_PATH_H_
