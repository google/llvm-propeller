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

#include "propeller/code_prefetch_parser.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

namespace propeller {

namespace {
// Parses a string address, handling both hexadecimal (with "0x" prefix) and
// decimal formats. Returns an error if the address is invalid.
absl::StatusOr<uint64_t> ParseAddressToUint64(llvm::StringRef address_str) {
  uint64_t address;
  if (address_str.starts_with("0x")) {
    if (address_str.drop_front(2).getAsInteger(16, address)) {
      return absl::InvalidArgumentError(
          (llvm::Twine("Invalid hexadecimal address format: \"") + address_str +
           "\"")
              .str());
    }
  } else {
    if (address_str.getAsInteger(10, address)) {
      return absl::InvalidArgumentError(
          (llvm::Twine("Invalid decimal address format: \"") + address_str +
           "\"")
              .str());
    }
  }
  return address;
}
}  // namespace

absl::StatusOr<std::vector<CodePrefetchDirective>> ReadCodePrefetchDirectives(
    llvm::StringRef prefetch_directives_path) {
  if (prefetch_directives_path.empty()) {
    return std::vector<CodePrefetchDirective>();
  }

  std::vector<CodePrefetchDirective> code_prefetch_directives;
  std::ifstream infile(prefetch_directives_path.str());
  if (!infile.is_open()) {
    return absl::NotFoundError(
        (llvm::Twine("Could not open file: ") + prefetch_directives_path)
            .str());
  }

  std::string line;
  int line_number = 0;
  while (std::getline(infile, line)) {
    ++line_number;
    llvm::StringRef line_ref = llvm::StringRef(line).trim();
    // Skip comments and empty lines.
    if (line_ref.empty() || line_ref.front() == '#') continue;

    llvm::SmallVector<llvm::StringRef, 2> addresses;
    line_ref.split(addresses, ',');
    if (addresses.size() != 2) {
      return absl::InvalidArgumentError(
          (llvm::Twine("Invalid format in prefetch directives file at line ") +
           llvm::Twine(line_number) +
           ": Expected two comma-separated addresses, but got \"" + line_ref +
           "\"")
              .str());
    }

    absl::StatusOr<uint64_t> prefetch_site =
        ParseAddressToUint64(addresses[0].trim());
    if (!prefetch_site.ok()) {
      return absl::InvalidArgumentError(
          (llvm::Twine("Invalid prefetch site address format in prefetch "
                       "directives file at line ") +
           llvm::Twine(line_number) + ": " + prefetch_site.status().message() +
           " in \"" + line_ref + "\"")
              .str());
    }

    absl::StatusOr<uint64_t> prefetch_target =
        ParseAddressToUint64(addresses[1].trim());
    if (!prefetch_target.ok()) {
      return absl::InvalidArgumentError(
          (llvm::Twine("Invalid prefetch target address format in prefetch "
                       "directives file at line ") +
           llvm::Twine(line_number) + ": " +
           prefetch_target.status().message() + " in \"" + line_ref + "\"")
              .str());
    }

    code_prefetch_directives.push_back(
        {.prefetch_site = *prefetch_site, .prefetch_target = *prefetch_target});
  }
  return code_prefetch_directives;
}

}  // namespace propeller
