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

#include "propeller/binary_content.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/DenseMap.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::SizeIs;

TEST(BinaryContentTest, BuildId) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
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
      ::testing::SrcDir(), "_main/propeller/testdata/propeller_sample_1.bin");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  EXPECT_THAT(GetSymbolAddress(*binary_content->object_file, "main"),
              IsOkAndHolds(6096));
}

TEST(GetSymbolAddressTest, SymbolNotFound) {
  const std::string binary = absl::StrCat(
      ::testing::SrcDir(), "_main/propeller/testdata/propeller_sample_1.bin");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  EXPECT_THAT(
      GetSymbolAddress(*binary_content->object_file, "not_found_symbol"),
      Not(IsOk()));
}

TEST(ReadBbAddrMapTest, ReadBbAddrMap) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "sample_pgo_analysis_map.bin");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));
  absl::StatusOr<BbAddrMapData> bb_addr_map_data =
      ReadBbAddrMap(*binary_content, {.read_pgo_analyses = true});
  ASSERT_OK(bb_addr_map_data);
  EXPECT_THAT(bb_addr_map_data->bb_addr_maps, SizeIs(4));
  EXPECT_THAT(bb_addr_map_data->pgo_analyses, Optional(SizeIs(4)));
}

TEST(ThunkSymbolsTest, X86NoThunks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(absl::StrCat(::testing::SrcDir(),
                                    "_main/propeller/testdata/"
                                    "propeller_sample_1.bin")));
  EXPECT_THAT(ReadThunkSymbols(*binary_content), SizeIs(0));
}

}  // namespace
}  // namespace propeller
