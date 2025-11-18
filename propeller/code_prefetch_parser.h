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

#ifndef PROPELLER_CODE_PREFETCH_PARSER_H_
#define PROPELLER_CODE_PREFETCH_PARSER_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace propeller {

// Represents a prefetch hint from the prefetch profile.
struct CodePrefetchDirective {
  // The binary address of the prefetch site, where the prefetch instruction is
  // to be inserted.
  uint64_t prefetch_site;
  // The binary address of the target of the prefetch instruction.
  uint64_t prefetch_target;
};

// Reads code prefetch directives from the given file path.
// Each line in the file is expected to contain two comma-separated hexadecimal
// or decimal addresses. Lines starting with '#' are ignored as comments.
absl::StatusOr<std::vector<CodePrefetchDirective>> ReadCodePrefetchDirectives(
    absl::string_view prefetch_directives_path);

}  // namespace propeller

#endif  // PROPELLER_CODE_PREFETCH_PARSER_H_
