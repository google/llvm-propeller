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

PropellerOptions GetOptionsWithPrefetchPath(absl::string_view prefetch_path) {
  PropellerOptions options;
  options.set_prefetch_directives_path(prefetch_path);
  return options;
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

TEST(GeneratePropellerProfiles, GenerateProfileWithBBHash) {
  PropellerOptions options;
  options.set_binary_name(absl::StrCat(::testing::SrcDir(),
                                       "_main/propeller/testdata/",
                                       "sample_with_bb_hash.bin"));
  const std::string cc_profile_path = absl::StrCat(
      ::testing::TempDir(),
      "/LlvmPropellerProfileGeneratorTest_GenerateProfileWithBBHash_cc_profile.txt");
  options.set_cluster_out_name(cc_profile_path);
  options.set_symbol_order_out_name(absl::StrCat(
      ::testing::TempDir(),
      "/LlvmPropellerProfileGeneratorTest_GenerateProfileWithBBHash_ld_profile.txt"));
  options.set_write_bb_hash(true);

  absl::Status status = GeneratePropellerProfiles(
      options, std::make_unique<GenericFilePerfDataProvider>(
                    std::vector<std::string>{absl::StrCat(
                        ::testing::SrcDir(),
                        "_main/propeller/testdata/",
                        "sample_with_bb_hash.perfdata")}));

  // The provider will success, because the file exists.
  EXPECT_THAT(status, IsOk());
  absl::StatusOr<std::string> actual_cc_profile = GetContents(cc_profile_path);
  ASSERT_THAT(actual_cc_profile, IsOk());
  const std::string expected_cc_profile = 
    R"(v1)" "\n"
    R"(#Profiled binary build ID: 2a2a7bf9ebe9f34567b9f3275b6031662037b7d6)" "\n"
    R"(f compute_flag)" "\n"
    R"(c0 2 3 1)" "\n"
    R"(g 0:165611,1:52349,2:99716 1:53580,3:53580 2:99716,3:98738 3:155409)" "\n"
    R"(h 0:47f92c00f40b0000 1:e1b21381f8c2000c 2:5fc899cb7e4c0010 3:c5b8efa3dc9d0011)" "\n"
    R"(f main)" "\n"
    R"(c0 3 4 6 7 1 2)" "\n"
    R"(g 0:0 1:163830,2:162637 2:165611,3:45237,4:104694 3:45237,4:45237 4:156847,6:156847 5:0 6:159866,7:159866 7:163830,1:163830 8:0)" "\n"
    R"(h 0:8c7017b867530000 1:b19dc95813b9000c 2:be4ae80c1fa7000e 3:c685e7db68cd0016 4:7a0365c405a0020 5:3162f4400ecf0026 6:4692000000000031 7:adb353cef8c20032 8:f1b72b1bec3a0036)" "\n";
  EXPECT_EQ(*actual_cc_profile, expected_cc_profile);
}
}  // namespace
}  // namespace propeller
