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

#include <filesystem>  // NOLINT: open source.
#include <string>
#include <tuple>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "propeller/code_prefetch_parser.h"
#include "propeller/file_helpers.h"
#include "testing/fuzzing/fuzztest.h"

namespace propeller {
namespace {

// Returns fuzzing seeds for the code prefetch parser, by getting everything
// under testdata/prefetch_parsing/.
std::vector<std::tuple<std::string>> GetFuzzingSeeds() {
  std::vector<std::tuple<std::string>> seeds;
  for (const auto& entry : std::filesystem::directory_iterator(
           absl::StrCat(::testing::SrcDir(),
                        "_main/propeller/testdata/prefetch_parsing/"))) {
    if (entry.path().extension() != ".txt") continue;
    absl::StatusOr<std::string> contents =
        propeller_file::GetContents(entry.path().string());
    CHECK_OK(contents.status());
    seeds.emplace_back(*contents);
  }
  CHECK(!seeds.empty()) << "No fuzzing seeds found.";
  return seeds;
}

void DoesNotCrash(absl::string_view contents) {
  std::string path = absl::StrCat(testing::TempDir(), "/test_file.txt");
  CHECK_OK(propeller_file::SetContents(path, contents));
  ReadCodePrefetchDirectives(path).IgnoreError();
}
FUZZ_TEST(CodePrefetchParserFuzzTest, DoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(&GetFuzzingSeeds);

}  // namespace
}  // namespace propeller
