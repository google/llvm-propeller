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

#include "propeller/code_prefetch_parser.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace propeller {

namespace {
// Parses a string address, handling both hexadecimal (with "0x" prefix) and
// decimal formats. Returns an error if the address is invalid.
absl::StatusOr<uint64_t> ParseAddressToUint64(absl::string_view address_str) {
  uint64_t address;
  if (address_str.starts_with("0x")) {
    if (!absl::SimpleHexAtoi(address_str, &address)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid hexadecimal address format: \"", address_str, "\""));
    }
  } else {
    if (!absl::SimpleAtoi(address_str, &address)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid decimal address format: \"", address_str, "\""));
    }
  }
  return address;
}
}  // namespace

absl::StatusOr<std::vector<CodePrefetchDirective>> ReadCodePrefetchDirectives(
    absl::string_view prefetch_directives_path) {
  if (prefetch_directives_path.empty()) {
    return std::vector<CodePrefetchDirective>();
  }

  std::vector<CodePrefetchDirective> code_prefetch_directives;
  std::ifstream infile((std::string(prefetch_directives_path)));
  if (!infile.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Could not open file: ", prefetch_directives_path));
  }

  std::string line;
  int line_number = 0;
  while (std::getline(infile, line)) {
    ++line_number;
    absl::StripAsciiWhitespace(&line);
    // Skip comments and empty lines.
    if (line.empty() || line[0] == '#') continue;

    std::vector<std::string> addresses = absl::StrSplit(line, ',');
    if (addresses.size() != 2) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid format in prefetch directives file at line ", line_number,
          ": Expected two comma-separated addresses, but got \"", line, "\""));
    }

    absl::StatusOr<uint64_t> prefetch_site = ParseAddressToUint64(addresses[0]);
    if (!prefetch_site.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid prefetch site address format in prefetch directives file at "
          "line ",
          line_number, ": ", prefetch_site.status().message(), " in \"", line,
          "\""));
    }

    absl::StatusOr<uint64_t> prefetch_target =
        ParseAddressToUint64(addresses[1]);
    if (!prefetch_target.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid prefetch target address format in prefetch directives file "
          "at line ",
          line_number, ": ", prefetch_target.status().message(), " in \"", line,
          "\""));
    }

    code_prefetch_directives.push_back(
        {.prefetch_site = *prefetch_site, .prefetch_target = *prefetch_target});
  }
  return code_prefetch_directives;
}

}  // namespace propeller
