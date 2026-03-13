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

#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "devtools/build/runtime/get_runfiles_dir.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/file_helpers.h"
#include "propeller/profile_generator.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/status_testing_macros.h"
#include "testing/base/public/googletest.h"

ABSL_FLAG(std::string, input_binary, "",
          "Path to the input binary, relative to runfiles.");
ABSL_FLAG(std::string, input_profile, "",
          "Path to the input profile, relative to runfiles.");

namespace {

using ::testing::IsEmpty;
using ::testing::Not;

// Joins two paths.
inline std::string JoinPath(absl::string_view p1, absl::string_view p2) {
  if (p1.empty()) return std::string(p2);
  if (p2.empty()) return std::string(p1);
  if (p1.back() == '/') {
    return absl::StrCat(p1, p2);
  }
  return absl::StrCat(p1, "/", p2);
}

// Calls propeller::GeneratePropellerProfiles on the given binary and profile,
// and stores the results in cc_profile and ld_profile.
void GenerateProfiles(absl::string_view binary, absl::string_view profile,
                      absl::string_view cc_profile,
                      absl::string_view ld_profile) {
  propeller::PropellerOptions options;
  options.set_binary_name(binary);
  propeller::InputProfile* input_profile = options.add_input_profiles();
  input_profile->set_name(profile);
  input_profile->set_type(propeller::PERF_LBR);
  options.set_cluster_out_name(cc_profile);
  options.set_symbol_order_out_name(ld_profile);
  ASSERT_OK(propeller::GeneratePropellerProfiles(options));
}

TEST(PropellerDeterminismTest, GeneratePropellerProfilesIsDeterministic) {
  const std::string binary = devtools_build::GetDataDependencyFilepath(
      absl::GetFlag(FLAGS_input_binary));
  const std::string profile = devtools_build::GetDataDependencyFilepath(
      absl::GetFlag(FLAGS_input_profile));
  const std::string cc_profile1 =
      JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "cc_profile1.txt");
  const std::string ld_profile1 =
      JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "ld_profile1.txt");
  GenerateProfiles(binary, profile, cc_profile1, ld_profile1);

  const std::string cc_profile2 =
      JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "cc_profile2.txt");
  const std::string ld_profile2 =
      JoinPath(absl::GetFlag(FLAGS_test_tmpdir), "ld_profile2.txt");
  GenerateProfiles(binary, profile, cc_profile2, ld_profile2);

  ASSERT_OK_AND_ASSIGN(std::string cc_profile1_contents,
                       propeller_file::GetContents(cc_profile1));
  ASSERT_OK_AND_ASSIGN(std::string ld_profile1_contents,
                       propeller_file::GetContents(ld_profile1));
  ASSERT_OK_AND_ASSIGN(std::string cc_profile2_contents,
                       propeller_file::GetContents(cc_profile2));
  ASSERT_OK_AND_ASSIGN(std::string ld_profile2_contents,
                       propeller_file::GetContents(ld_profile2));

  EXPECT_THAT(cc_profile1_contents, Not(IsEmpty()));
  EXPECT_THAT(ld_profile1_contents, Not(IsEmpty()));

  EXPECT_EQ(cc_profile1_contents, cc_profile2_contents);
  EXPECT_EQ(ld_profile1_contents, ld_profile2_contents);
}

}  // namespace
