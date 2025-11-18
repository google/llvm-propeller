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

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

MATCHER_P2(CodePrefetchDirectiveEq, site, target, "") {
  return arg.prefetch_site == site && arg.prefetch_target == target;
}

std::string GetTestDataPath(absl::string_view filename) {
  return absl::StrCat(::testing::SrcDir(),
                      "_main/propeller/testdata/prefetch_parsing/", filename);
}

TEST(CodePrefetchParserTest, EmptyPath) {
  EXPECT_THAT(ReadCodePrefetchDirectives(""), IsOkAndHolds(IsEmpty()));
}

TEST(CodePrefetchParserTest, NonExistentFile) {
  EXPECT_THAT(ReadCodePrefetchDirectives("non_existent_file.txt"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(CodePrefetchParserTest, ValidDecimalAddresses) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_decimal.txt")),
      IsOkAndHolds(ElementsAre(CodePrefetchDirectiveEq(10, 20),
                               CodePrefetchDirectiveEq(30, 40))));
}

TEST(CodePrefetchParserTest, ValidHexadecimalAddresses) {
  EXPECT_THAT(ReadCodePrefetchDirectives(GetTestDataPath("prefetch_hex.txt")),
              IsOkAndHolds(ElementsAre(CodePrefetchDirectiveEq(0x1a, 0x2b),
                                       CodePrefetchDirectiveEq(0x3c, 0x4d))));
}

TEST(CodePrefetchParserTest, MixedDecimalAndHexadecimal) {
  EXPECT_THAT(ReadCodePrefetchDirectives(GetTestDataPath("prefetch_mixed.txt")),
              IsOkAndHolds(ElementsAre(CodePrefetchDirectiveEq(10, 0x2b),
                                       CodePrefetchDirectiveEq(0x3c, 40))));
}

TEST(CodePrefetchParserTest, WithCommentsAndEmptyLines) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_comments.txt")),
      IsOkAndHolds(ElementsAre(CodePrefetchDirectiveEq(10, 20),
                               CodePrefetchDirectiveEq(0x3c, 40))));
}

TEST(CodePrefetchParserTest, InvalidFormat_TooFewAddresses) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_invalid1.txt")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CodePrefetchParserTest, InvalidFormat_TooManyAddresses) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_invalid2.txt")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CodePrefetchParserTest, InvalidAddress_NonNumeric) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_invalid3.txt")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CodePrefetchParserTest, InvalidAddress_OutOfRange) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_invalid4.txt")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CodePrefetchParserTest, InvalidAddress_HexOutOfRange) {
  EXPECT_THAT(
      ReadCodePrefetchDirectives(GetTestDataPath("prefetch_invalid5.txt")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace propeller
