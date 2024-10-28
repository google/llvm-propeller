#include "propeller/binary_content.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace propeller {
namespace {
using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::SizeIs;
using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;

TEST(BinaryContentTest, BuildId) {
  const std::string binary =
      absl::StrCat(::testing::SrcDir(),
                   "_main/propeller/testdata/"
                   "llvm_function_samples.binary");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  EXPECT_EQ(binary_content->build_id,
            "a56e4274b3adf7c87d165ca6deb66db002e72e1e");
}

TEST(BinaryContentTest, PieAndNoBuildId) {
  const std::string binary =
      absl::StrCat(::testing::SrcDir(),
                   "_main/propeller/testdata/"
                   "propeller_barebone_pie_nobuildid.bin");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  EXPECT_TRUE(binary_content->is_pie);
  EXPECT_THAT(binary_content->build_id, IsEmpty());
}

TEST(GetSymbolAddressTest, SymbolFound) {
  const std::string binary = absl::StrCat(
      ::testing::SrcDir(),
      "_main/propeller/testdata/propeller_sample_1.bin");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  EXPECT_THAT(GetSymbolAddress(*binary_content->object_file, "main"),
              IsOkAndHolds(6432));
}

TEST(GetSymbolAddressTest, SymbolNotFound) {
  const std::string binary = absl::StrCat(
      ::testing::SrcDir(),
      "_main/propeller/testdata/propeller_sample_1.bin");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  EXPECT_THAT(
      GetSymbolAddress(*binary_content->object_file, "not_found_symbol"),
      Not(IsOk()));
}

}  // namespace
}  // namespace propeller
