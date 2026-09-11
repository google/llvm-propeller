// Copyright 2026 The Propeller Authors.
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

#ifndef PROPELLER_BINARY_ADDRESS_BRANCH_PATH_H_
#define PROPELLER_BINARY_ADDRESS_BRANCH_PATH_H_

#include <cstdint>
#include <vector>

#include "absl/time/time.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "propeller/binary_address_branch.h"

namespace propeller {

struct BinaryAddressBranchPath {
  int64_t pid;
  absl::Time sample_time;
  std::vector<BinaryAddressBranch> branches;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BinaryAddressBranchPath& path) {
    sink.Append(llvm::formatv(
                    "BinaryAddressBranchPath[pid:{0}, branches:{1}]", path.pid,
                    llvm::join(llvm::map_range(
                                   path.branches,
                                   [](const BinaryAddressBranch& b) {
                                     return llvm::formatv("{0:x16}->{1:x16}",
                                                          b.from, b.to)
                                         .str();
                                   }),
                               ", "))
                    .str());
  }
};
}  // namespace propeller

#endif  // PROPELLER_BINARY_ADDRESS_BRANCH_PATH_H_
