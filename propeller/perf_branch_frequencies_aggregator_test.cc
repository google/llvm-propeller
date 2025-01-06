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

#include "propeller/perf_branch_frequencies_aggregator.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/perf_data_provider.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Field;
using ::testing::SizeIs;

class MockPerfDataProvider : public PerfDataProvider {
 public:
  MOCK_METHOD(absl::StatusOr<std::optional<PerfDataProvider::BufferHandle>>,
              GetNext, ());
  MOCK_METHOD(absl::StatusOr<std::vector<PerfDataProvider::BufferHandle>>,
              GetAllAvailableOrNext, ());
};

static constexpr std::string_view kTestDataDir = "propeller/testdata/";

TEST(PerfBranchFrequenciesAggregator, FailsIfNoPerfData) {
  auto mock_perf_data_provider = std::make_unique<MockPerfDataProvider>();

  // PerfDataProvider::BufferHandle is not copyable, so we can't use
  // `testing::Return` here.
  EXPECT_CALL(*mock_perf_data_provider, GetNext()).WillRepeatedly([]() {
    return absl::InvalidArgumentError("No perf data");
  });
  EXPECT_CALL(*mock_perf_data_provider, GetAllAvailableOrNext())
      .WillRepeatedly(
          []() { return absl::InvalidArgumentError("No perf data"); });

  PropellerStats stats;
  EXPECT_THAT(
      PerfBranchFrequenciesAggregator(std::move(mock_perf_data_provider))
          .AggregateBranchFrequencies(PropellerOptions(), BinaryContent(),
                                      stats),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace propeller
