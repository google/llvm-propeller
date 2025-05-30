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

#include "propeller/file_perf_data_provider.h"

#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>

#include "absl/log/check.h"
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
using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;

MATCHER_P(BufferIs, contents_matcher,
          absl::StrCat("an llvm::MemoryBuffer that ",
                       testing::DescribeMatcher<absl::string_view>(
                           contents_matcher, negation))) {
  return testing::ExplainMatchResult(
      contents_matcher, absl::string_view(std::string_view(arg->getBuffer())),
      result_listener);
}

// Writes `contents` to file named `file_name`.
void WriteFile(absl::string_view file_name, absl::string_view contents) {
  std::ofstream stream(std::string{file_name}, std::ios::binary);
  stream << contents;
  CHECK(!stream.fail());
}

template <typename T>
class FilePerfDataProviderTest : public testing::Test {
 public:
  using FilePerfDataProviderType = T;
};

using FilePerfDataProviderTypes = ::testing::Types<GenericFilePerfDataProvider>;
TYPED_TEST_SUITE(FilePerfDataProviderTest, FilePerfDataProviderTypes);

TYPED_TEST(FilePerfDataProviderTest, GetNextReadsFilesCorrectly) {
  std::string file1 =
      absl::StrCat(::testing::TempDir(),
                   "/FilePerfDataProvider_ReadsFilesCorrectly_file1.perf");
  std::string file2 =
      absl::StrCat(::testing::TempDir(),
                   "/FilePerfDataProvider_ReadsFilesCorrectly_file2.perf");
  WriteFile(file1, "Hello world");
  WriteFile(file2, "Test data");

  typename TestFixture::FilePerfDataProviderType provider({file1, file2});
  EXPECT_THAT(provider.GetNext(),
              IsOkAndHolds(Optional(FieldsAre(absl::StrCat("[1/2] ", file1),
                                              BufferIs("Hello world")))));
  EXPECT_THAT(provider.GetNext(),
              IsOkAndHolds(Optional(FieldsAre(absl::StrCat("[2/2] ", file2),
                                              BufferIs("Test data")))));
  EXPECT_THAT(provider.GetNext(), IsOkAndHolds(Eq(std::nullopt)));
}

TYPED_TEST(FilePerfDataProviderTest, GetAllAvailableOrNextReadsFilesCorrectly) {
  std::string file1 =
      absl::StrCat(::testing::TempDir(),
                   "/FilePerfDataProvider_ReadsFilesCorrectly_file1.perf");
  std::string file2 =
      absl::StrCat(::testing::TempDir(),
                   "/FilePerfDataProvider_ReadsFilesCorrectly_file2.perf");
  WriteFile(file1, "Hello world");
  WriteFile(file2, "Test data");

  typename TestFixture::FilePerfDataProviderType provider({file1, file2});
  EXPECT_THAT(
      provider.GetAllAvailableOrNext(),
      IsOkAndHolds(ElementsAre(
          FieldsAre(absl::StrCat("[1/2] ", file1), BufferIs("Hello world")),
          FieldsAre(absl::StrCat("[2/2] ", file2), BufferIs("Test data")))));
  EXPECT_THAT(provider.GetAllAvailableOrNext(), IsOkAndHolds(IsEmpty()));
}

TYPED_TEST(FilePerfDataProviderTest, GetNextPropagatesErrors) {
  auto file_name =
      absl::StrCat(::testing::TempDir(),
                   "/FilePerfDataProvider_PropagatesErrors_does_not_exist");
  typename TestFixture::FilePerfDataProviderType provider({file_name});
  EXPECT_THAT(
      provider.GetNext(),
      StatusIs(Not(absl::StatusCode::kOk),
               HasSubstr(absl::StrCat("When reading file ", file_name))));
}

TYPED_TEST(FilePerfDataProviderTest, GetAllAvailableOrNextPropagatesErrors) {
  auto file_name =
      absl::StrCat(::testing::TempDir(),
                   "/FilePerfDataProvider_PropagatesErrors_does_not_exist");
  typename TestFixture::FilePerfDataProviderType provider({file_name});
  EXPECT_THAT(
      provider.GetAllAvailableOrNext(),
      StatusIs(Not(absl::StatusCode::kOk),
               HasSubstr(absl::StrCat("When reading file ", file_name))));
}
}  // namespace
}  // namespace propeller
