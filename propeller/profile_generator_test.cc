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

#include "propeller/profile_generator.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/file_helpers.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/parse_text_proto.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::propeller_testing::ParseTextProtoOrDie;

std::string GetPropellerTestDataDirectoryPath() {
  return absl::StrCat(::testing::SrcDir(), "_main/propeller/testdata/");
}

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::propeller_file::GetContents;
using ::propeller_file::GetContentsIgnoringCommentLines;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::TestWithParam;

struct TestCaseInputProfile {
  std::string name;
  std::optional<ProfileType> type;
};

struct GeneratePropellerProfileTestCase {
  std::string test_name;
  std::string binary_name;
  std::vector<TestCaseInputProfile> input_profiles;
  PropellerOptions options;
  std::string expected_cc_profile_path;
  std::string expected_ld_profile_path;
  // If true, ignore lines starting with '#' in the actual cluster profile.
  bool ignore_comment_lines_in_cc_profile = true;
};

TEST(GeneratePropellerProfiles, UsesPassedProvider) {
  PropellerOptions options;
  options.set_binary_name(absl::StrCat(::testing::SrcDir(),
                                       "_main/propeller/testdata/"
                                       "sample.bin"));
  options.set_cluster_out_name(absl::StrCat(
      ::testing::TempDir(),
      "/LlvmPropellerProfileGeneratorTest_UsesPassedProvider_cc_profile.txt"));
  options.set_symbol_order_out_name(absl::StrCat(
      ::testing::TempDir(),
      "/LlvmPropellerProfileGeneratorTest_UsesPassedProvider_ld_profile.txt"));

  // The provider will fail, because the file does not exist, so
  // `GeneratePropellerProfiles` should fail.
  EXPECT_THAT(
      GeneratePropellerProfiles(
          options, std::make_unique<GenericFilePerfDataProvider>(
                       std::vector<std::string>{absl::StrCat(
                           ::testing::SrcDir(),
                           "/google3/devtools/crosstool/autofdo/testdata/"
                           "this_file_does_not_exist.perfdata")})),
      Not(IsOk()));
}
}  // namespace
}  // namespace propeller
