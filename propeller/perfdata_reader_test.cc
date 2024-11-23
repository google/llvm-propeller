#include "propeller/perfdata_reader.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/lbr_aggregation.h"
#include "propeller/perf_data_provider.h"

namespace propeller {
namespace {
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::testing::SizeIs;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

TEST(PerfDataReaderTest, IsKernel) {
  BinaryContent binary_content;

  EXPECT_FALSE(PerfDataReader(PerfDataProvider::BufferHandle{},
                              /*binary_mmaps=*/{{0, {}}},
                              /*binary_content=*/nullptr)
                   .IsKernelMode());

  EXPECT_TRUE(
      PerfDataReader(PerfDataProvider::BufferHandle{},
                     /*binary_mmaps=*/{{PerfDataReader::kKernelPid, {}}},
                     /*binary_content=*/nullptr)
          .IsKernelMode());
}
}  // namespace
}  // namespace propeller
