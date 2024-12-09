#include "propeller/profile_generator.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "propeller/file_helpers.h"

#include "propeller/parse_text_proto.h"
#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
namespace {

using ::propeller_testing::ParseTextProtoOrDie;

std::string GetPropellerTestDataDirectoryPath() {
  return absl::StrCat(::testing::SrcDir(),
                      "_main/propeller/testdata/");
}

// Returns the content of the file stored in `file_name` as a string.
std::string GetFileContent(absl::string_view file_name) {
  std::string content;
  CHECK_OK(file::GetContents(file_name, &content, true));
  return content;
}

using ::testing::HasSubstr;
using ::testing::Not;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

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
};

TEST(GeneratePropellerProfiles, UsesPassedProvider) {
  PropellerOptions options;
  options.set_binary_name(
      absl::StrCat(::testing::SrcDir(),
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
