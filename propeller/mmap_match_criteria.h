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

#ifndef PROPELLER_MMAP_MATCH_CRITERIA_H_
#define PROPELLER_MMAP_MATCH_CRITERIA_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
// A class that holds the criteria for adhoc matching of mmap events by either
// the binary name or the build id, but not both. If both are empty, then adhoc
// matching will not be performed.
class MMapMatchCriteria {
 public:
  MMapMatchCriteria() = default;
  MMapMatchCriteria(absl::Span<const absl::string_view> mmap_binary_names,
                    std::optional<const absl::string_view> mmap_build_id)
      : mmap_binary_names_(mmap_binary_names.begin(), mmap_binary_names.end()),
        mmap_build_id_(mmap_build_id) {
    CHECK(mmap_binary_names_.empty() || !mmap_build_id_.has_value());
  }

  explicit MMapMatchCriteria(const PropellerOptions &options);

  MMapMatchCriteria(const MMapMatchCriteria &) = default;
  MMapMatchCriteria &operator=(const MMapMatchCriteria &) = default;
  MMapMatchCriteria(MMapMatchCriteria &&) = default;
  MMapMatchCriteria &operator=(MMapMatchCriteria &&) = default;

  absl::Span<const std::string> mmap_binary_names() const {
    return mmap_binary_names_;
  }

  const std::optional<std::string> &mmap_build_id() const {
    return mmap_build_id_;
  }

  // Implementation of the `AbslStringify` interface.
  template <typename Sink>
  friend void AbslStringify(Sink &sink,
                            const MMapMatchCriteria &match_criteria) {
    if (!match_criteria.mmap_binary_names_.empty()) {
      absl::Format(&sink, "mmap_binary_names: %s",
                   absl::StrJoin(match_criteria.mmap_binary_names(), ", "));
    }
    if (match_criteria.mmap_build_id_.has_value()) {
      absl::Format(&sink, "and mmap_build_id: %s",
                   *match_criteria.mmap_build_id_);
    }
  }

 private:
  std::vector<std::string> mmap_binary_names_;
  std::optional<std::string> mmap_build_id_;
};
}  // namespace propeller
#endif  // PROPELLER_MMAP_MATCH_CRITERIA_H_
