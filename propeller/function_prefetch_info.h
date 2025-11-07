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

#ifndef PROPELLER_FUNCTION_PREFETCH_INFO_H_
#define PROPELLER_FUNCTION_PREFETCH_INFO_H_

#include <cstdint>
#include <tuple>
#include <vector>

#include "absl/container/btree_set.h"

namespace propeller {

// Contains information about prefetch hints for a function.
struct FunctionPrefetchInfo {
  // Represents a prefetch hint, including the site and target of the prefetch.
  struct PrefetchHint {
    // ID of the basic block containing the prefetch instruction.
    uint32_t site_bb_id;
    // Callsite index within the site basic block. See
    // `BBEntry::CallsiteEndOffsets`.
    uint32_t site_callsite_index;
    // Index of the function containing the prefetch target basic block.
    int target_function_index;
    // ID of the prefetch target basic block.
    uint32_t target_bb_id;
    // Callsite index within the target basic block. See
    // `BBEntry::CallsiteEndOffsets`.
    uint32_t target_callsite_index;
  };

  // Represents the position of a prefetch target within a function (basic block
  // and callsite index within that basic block).
  struct TargetBBInfo {
    // ID of the basic block.
    uint32_t bb_id;
    // Callsite index within the basic block. See `BBEntry::CallsiteEndOffsets`.
    uint32_t callsite_index;
    bool operator==(const TargetBBInfo& other) const {
      return bb_id == other.bb_id && callsite_index == other.callsite_index;
    }
    bool operator<(const TargetBBInfo& other) const {
      return std::forward_as_tuple(bb_id, callsite_index) <
             std::forward_as_tuple(other.bb_id, other.callsite_index);
    }
  };

  // Prefetch hints whose site is in this function.
  std::vector<PrefetchHint> prefetch_hints;

  // Prefetch targets in this function.
  absl::btree_set<TargetBBInfo> prefetch_targets;
};

}  // namespace propeller

#endif  // PROPELLER_FUNCTION_PREFETCH_INFO_H_
