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

#include "propeller/file_perf_data_provider.h"

#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/Twine.h"
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
          (llvm::Twine("an llvm::MemoryBuffer that ") +
           testing::DescribeMatcher<absl::string_view>(contents_matcher,
                                                       negation))
              .str()) {
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
  std::string file1 = (llvm::Twine(::testing::TempDir()) +
                       "/FilePerfDataProvider_ReadsFilesCorrectly_file1.perf")
                          .str();
  std::string file2 = (llvm::Twine(::testing::TempDir()) +
                       "/FilePerfDataProvider_ReadsFilesCorrectly_file2.perf")
                          .str();
  WriteFile(file1, "Hello world");
  WriteFile(file2, "Test data");

  typename TestFixture::FilePerfDataProviderType provider({file1, file2});
  EXPECT_THAT(
      provider.GetNext(),
      IsOkAndHolds(Optional(FieldsAre((llvm::Twine("[1/2] ") + file1).str(),
                                      BufferIs("Hello world")))));
  EXPECT_THAT(
      provider.GetNext(),
      IsOkAndHolds(Optional(FieldsAre((llvm::Twine("[2/2] ") + file2).str(),
                                      BufferIs("Test data")))));
  EXPECT_THAT(provider.GetNext(), IsOkAndHolds(Eq(std::nullopt)));
}

TYPED_TEST(FilePerfDataProviderTest, GetAllAvailableOrNextReadsFilesCorrectly) {
  std::string file1 = (llvm::Twine(::testing::TempDir()) +
                       "/FilePerfDataProvider_ReadsFilesCorrectly_file1.perf")
                          .str();
  std::string file2 = (llvm::Twine(::testing::TempDir()) +
                       "/FilePerfDataProvider_ReadsFilesCorrectly_file2.perf")
                          .str();
  WriteFile(file1, "Hello world");
  WriteFile(file2, "Test data");

  typename TestFixture::FilePerfDataProviderType provider({file1, file2});
  EXPECT_THAT(
      provider.GetAllAvailableOrNext(),
      IsOkAndHolds(ElementsAre(FieldsAre((llvm::Twine("[1/2] ") + file1).str(),
                                         BufferIs("Hello world")),
                               FieldsAre((llvm::Twine("[2/2] ") + file2).str(),
                                         BufferIs("Test data")))));
  EXPECT_THAT(provider.GetAllAvailableOrNext(), IsOkAndHolds(IsEmpty()));
}

TYPED_TEST(FilePerfDataProviderTest, GetNextPropagatesErrors) {
  auto file_name = (llvm::Twine(::testing::TempDir()) +
                    "/FilePerfDataProvider_PropagatesErrors_does_not_exist")
                       .str();
  typename TestFixture::FilePerfDataProviderType provider({file_name});
  EXPECT_THAT(
      provider.GetNext(),
      StatusIs(
          Not(absl::StatusCode::kOk),
          HasSubstr((llvm::Twine("When reading file ") + file_name).str())));
}

TYPED_TEST(FilePerfDataProviderTest, GetAllAvailableOrNextPropagatesErrors) {
  auto file_name = (llvm::Twine(::testing::TempDir()) +
                    "/FilePerfDataProvider_PropagatesErrors_does_not_exist")
                       .str();
  typename TestFixture::FilePerfDataProviderType provider({file_name});
  EXPECT_THAT(
      provider.GetAllAvailableOrNext(),
      StatusIs(
          Not(absl::StatusCode::kOk),
          HasSubstr((llvm::Twine("When reading file ") + file_name).str())));
}
}  // namespace
}  // namespace propeller
