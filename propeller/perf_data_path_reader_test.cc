#include "propeller/perf_data_path_reader.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/internal_file_perf_data_provider.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perfdata_reader.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
namespace {
using ::testing::_;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::status::IsOkAndHolds;

static std::string GetPropellerTestDataFilePath(absl::string_view filename) {
  return absl::StrCat(::testing::SrcDir(),
                      "_main/propeller/testdata/",
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

  std::vector<BbHandleBranchPath> all_paths;
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

  std::vector<BbHandleBranchPath> all_paths;
  PerfDataPathReader(&perf_data_reader, binary_address_mapper.get())
      .ReadPathsAndApplyCallBack([&](auto paths) {
        all_paths.insert(all_paths.end(), paths.begin(), paths.end());
      });
  EXPECT_THAT(all_paths, SizeIs(70139));
}
}  // namespace
}  // namespace propeller
