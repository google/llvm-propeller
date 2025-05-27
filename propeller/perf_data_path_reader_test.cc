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

#include "propeller/perf_data_path_reader.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/internal_file_perf_data_provider.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perfdata_reader.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::absl_testing::IsOkAndHolds;
using ::testing::_;
using ::testing::Optional;
using ::testing::SizeIs;

static std::string GetPropellerTestDataFilePath(absl::string_view filename) {
  return absl::StrCat(::testing::SrcDir(), "_main/propeller/testdata/",
                      filename);
}

TEST(PerfDataPathReaderTest, ReadPaths) {
  const std::string binary = GetPropellerTestDataFilePath("bimodal_sample.bin");
  const std::string perfdata =
      GetPropellerTestDataFilePath("bimodal_sample.perfdata.1");

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));

  InternalFilePerfDataProvider provider({perfdata});
  absl::StatusOr<std::optional<PerfDataProvider::BufferHandle>> buffer =
      provider.GetNext();
  ASSERT_THAT(buffer, IsOkAndHolds(Optional(_)));
  ASSERT_OK_AND_ASSIGN(
      PerfDataReader perf_data_reader,
      BuildPerfDataReader(**std::move(buffer), binary_content.get(),
                          /*match_mmap_name=*/""));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  std::vector<FlatBbHandleBranchPath> all_paths;
  PerfDataPathReader(&perf_data_reader, binary_address_mapper.get())
      .ReadPathsAndApplyCallBack([&](auto paths) {
        all_paths.insert(all_paths.end(), paths.begin(), paths.end());
      });
  EXPECT_THAT(all_paths, SizeIs(23932));
}

TEST(PerfDataPathReaderTest, ReadPathsGetsPathsWithHotJoinBbs) {
  const std::string binary =
      GetPropellerTestDataFilePath("bimodal_sample.x.bin");
  const std::string perfdata =
      GetPropellerTestDataFilePath("bimodal_sample.x.perfdata.combined");

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(binary));

  InternalFilePerfDataProvider provider({perfdata});
  absl::StatusOr<std::optional<PerfDataProvider::BufferHandle>> buffer =
      provider.GetNext();
  ASSERT_THAT(buffer, IsOkAndHolds(Optional(_)));
  ASSERT_OK_AND_ASSIGN(
      PerfDataReader perf_data_reader,
      BuildPerfDataReader(**std::move(buffer), binary_content.get(),
                          /*match_mmap_name=*/""));

  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  std::vector<FlatBbHandleBranchPath> all_paths;
  PerfDataPathReader(&perf_data_reader, binary_address_mapper.get())
      .ReadPathsAndApplyCallBack([&](auto paths) {
        all_paths.insert(all_paths.end(), paths.begin(), paths.end());
      });
  EXPECT_THAT(all_paths, SizeIs(70139));
}
}  // namespace
}  // namespace propeller
