
#include "propeller/frequencies_branch_aggregator.h"

#include <memory>
#include <utility>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregation.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies_aggregator.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
namespace {
using ::llvm::object::BBAddrMap;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::UnorderedElementsAre;
using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

class MockFrequenciesAggregator : public BranchFrequenciesAggregator {
 public:
  MOCK_METHOD(absl::StatusOr<BranchFrequencies>, AggregateBranchFrequencies,
              (const PropellerOptions& options,
               const BinaryContent& binary_content, PropellerStats& stats));
};

TEST(FrequenciesBranchAggregator, GetBranchEndpointAddressesPropagatesErrors) {
  const PropellerOptions options;
  const BinaryContent binary_content;
  PropellerStats stats;
  auto mock_aggregator = std::make_unique<MockFrequenciesAggregator>();
  EXPECT_CALL(*mock_aggregator, AggregateBranchFrequencies)
      .WillOnce(Return(absl::InternalError("")));

  EXPECT_THAT(FrequenciesBranchAggregator(std::move(mock_aggregator), options,
                                          binary_content)
                  .GetBranchEndpointAddresses(),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(FrequenciesBranchAggregator, GetBranchEndpointAddresses) {
  const PropellerOptions options;
  const BinaryContent binary_content;
  PropellerStats stats;

  EXPECT_THAT(FrequenciesBranchAggregator(
                  {.taken_branch_counters = {{{.from = 1, .to = 2}, 1},
                                             {{.from = 3, .to = 3}, 1}},
                   .not_taken_branch_counters = {{{.address = 3}, 1},
                                                 {{.address = 4}, 1},
                                                 {{.address = 4}, 1},
                                                 {{.address = 5}, 1}}})
                  .GetBranchEndpointAddresses(),
              IsOkAndHolds(UnorderedElementsAre(1, 2, 3, 4, 5)));
}

TEST(FrequenciesBranchAggregator, AggregatePropagatesErrors) {
  const PropellerOptions options;
  const BinaryContent binary_content;
  PropellerStats stats;
  BinaryAddressMapper binary_address_mapper(
      /*selected_functions=*/{}, /*bb_addr_map=*/{}, /*bb_handles=*/{},
      /*symbol_info_map=*/{});
  auto mock_aggregator = std::make_unique<MockFrequenciesAggregator>();
  EXPECT_CALL(*mock_aggregator, AggregateBranchFrequencies)
      .WillOnce(Return(absl::InternalError("")));

  EXPECT_THAT(FrequenciesBranchAggregator(std::move(mock_aggregator), options,
                                          binary_content)
                  .Aggregate(binary_address_mapper, stats),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(FrequenciesBranchAggregator, AggregatePropagatesStats) {
  const PropellerOptions options;
  const BinaryContent binary_content;
  PropellerStats stats;
  BinaryAddressMapper binary_address_mapper(
      /*selected_functions=*/{}, /*bb_addr_map=*/{}, /*bb_handles=*/{},
      /*symbol_info_map=*/{});
  auto mock_aggregator = std::make_unique<MockFrequenciesAggregator>();
  EXPECT_CALL(*mock_aggregator, AggregateBranchFrequencies)
      .WillOnce(DoAll(SetArgReferee<2>(PropellerStats{
                          .profile_stats = {.binary_mmap_num = 1,
                                            .perf_file_parsed = 2,
                                            .br_counters_accumulated = 3}}),

                      Return(BranchFrequencies{})));

  FrequenciesBranchAggregator aggregator(std::move(mock_aggregator), options,
                                         binary_content);

  EXPECT_THAT(aggregator.Aggregate(binary_address_mapper, stats), IsOk());

  EXPECT_THAT(stats,
              AllOf(Field("profile_stats", &PropellerStats::profile_stats,
                          FieldsAre(1, 2, 3))));
}

TEST(FrequenciesBranchAggregator, AggregateInfersUnconditionalFallthroughs) {
  PropellerStats stats;
  EXPECT_THAT(
      FrequenciesBranchAggregator(
          {.taken_branch_counters = {{{.from = 0x1000, .to = 0x1008}, 7},
                                     {{.from = 0x1010, .to = 0x1008}, 10}}})
          .Aggregate(BinaryAddressMapper(
                         /*selected_functions=*/{1}, /*bb_addr_map=*/
                         {{{{.BaseAddress = 0x1000,
                             .BBEntries =
                                 {
                                     BBAddrMap::BBEntry(
                                         /*ID=*/0, /*Offset=*/0, /*Size=*/4,
                                         /*Metadata=*/{}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/1, /*Offset=*/8, /*Size=*/8,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/2, /*Offset=*/16,
                                         /*Size=*/4,
                                         /*Metadata=*/{}),
                                 }}}}},
                         /*bb_handles=*/
                         {{.function_index = 0, .bb_index = 0},
                          {.function_index = 0, .bb_index = 1},
                          {.function_index = 0, .bb_index = 2}},
                         /*symbol_info_map=*/{}),
                     stats),
      IsOkAndHolds(AllOf(
          Field("branch_counters", &BranchAggregation::branch_counters,
                UnorderedElementsAre(Pair(FieldsAre(0x1000, 0x1008), Eq(7)),
                                     Pair(FieldsAre(0x1010, 0x1008), Eq(10)))),
          Field(
              "fallthrough_counters", &BranchAggregation::fallthrough_counters,
              UnorderedElementsAre(Pair(FieldsAre(0x1008, 0x1010), Eq(17)))))));
}

TEST(FrequenciesBranchAggregator, AggregatePropagatesFallthroughs) {
  PropellerStats stats;
  EXPECT_THAT(
      FrequenciesBranchAggregator(
          {.taken_branch_counters = {{{.from = 0x1014, .to = 0x1000}, 50},
                                     {{.from = 0x1014, .to = 0x1004}, 50},
                                     {{.from = 0x1014, .to = 0x1010}, 50}}})
          .Aggregate(BinaryAddressMapper(
                         /*selected_functions=*/{1}, /*bb_addr_map=*/
                         {{{{.BaseAddress = 0x1000,
                             .BBEntries =
                                 {
                                     BBAddrMap::BBEntry(
                                         /*ID=*/0, /*Offset=*/0x0, /*Size=*/4,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/1, /*Offset=*/0x4, /*Size=*/8,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/2, /*Offset=*/0x10, /*Size=*/4,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/3, /*Offset=*/0x14, /*Size=*/4,
                                         /*Metadata=*/{}),
                                 }}}}},
                         /*bb_handles=*/
                         {{.function_index = 0, .bb_index = 0},
                          {.function_index = 0, .bb_index = 1},
                          {.function_index = 0, .bb_index = 2},
                          {.function_index = 0, .bb_index = 3}},
                         /*symbol_info_map=*/{}),
                     stats),
      IsOkAndHolds(Field(
          "fallthrough_counters", &BranchAggregation::fallthrough_counters,
          UnorderedElementsAre(Pair(FieldsAre(0x1000, 0x1004), Eq(50)),
                               Pair(FieldsAre(0x1004, 0x1010), Eq(100)),
                               Pair(FieldsAre(0x1010, 0x1014), Eq(150))))));
}

TEST(FrequenciesBranchAggregator, AggregateRespectsNotTakenBranches) {
  PropellerStats stats;
  EXPECT_THAT(
      FrequenciesBranchAggregator(
          {.not_taken_branch_counters = {{{.address = 0x1000}, 19}}},
          /*stats=*/{}, /*instruction_size=*/4)
          .Aggregate(BinaryAddressMapper(
                         /*selected_functions=*/{1}, /*bb_addr_map=*/
                         {{{{.BaseAddress = 0x1000,
                             .BBEntries =
                                 {
                                     BBAddrMap::BBEntry(
                                         /*ID=*/0, /*Offset=*/0, /*Size=*/4,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/1, /*Offset=*/8, /*Size=*/8,
                                         /*Metadata=*/{}),
                                 }}}}},
                         /*bb_handles=*/
                         {{.function_index = 0, .bb_index = 0},
                          {.function_index = 0, .bb_index = 1}},
                         /*symbol_info_map=*/{}),
                     stats),
      IsOkAndHolds(Field(
          "fallthrough_counters", &BranchAggregation::fallthrough_counters,
          UnorderedElementsAre(Pair(FieldsAre(0x1000, 0x1008), Eq(19))))));
}

TEST(FrequenciesBranchAggregator, AggregateIgnoresMidFunctionNotTakenBranches) {
  PropellerStats stats;
  EXPECT_THAT(
      FrequenciesBranchAggregator(
          {.taken_branch_counters = {{{.from = 0x1000, .to = 0x1008}, 7},
                                     {{.from = 0x1010, .to = 0x1008}, 10}},
           .not_taken_branch_counters = {{{.address = 0x1008}, 19}}},
          /*stats=*/{}, /*instruction_size=*/4)
          .Aggregate(BinaryAddressMapper(
                         /*selected_functions=*/{1}, /*bb_addr_map=*/
                         {{{{.BaseAddress = 0x1000,
                             .BBEntries =
                                 {
                                     BBAddrMap::BBEntry(
                                         /*ID=*/0, /*Offset=*/0, /*Size=*/4,
                                         /*Metadata=*/{}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/1, /*Offset=*/8, /*Size=*/8,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/2, /*Offset=*/16, /*Size=*/4,
                                         /*Metadata=*/{}),
                                 }}}}},
                         /*bb_handles=*/
                         {{.function_index = 0, .bb_index = 0},
                          {.function_index = 0, .bb_index = 1},
                          {.function_index = 0, .bb_index = 2}},
                         /*symbol_info_map=*/{}),
                     stats),
      IsOkAndHolds(AllOf(
          Field("branch_counters", &BranchAggregation::branch_counters,
                UnorderedElementsAre(Pair(FieldsAre(0x1000, 0x1008), Eq(7)),
                                     Pair(FieldsAre(0x1010, 0x1008), Eq(10)))),
          Field(
              "fallthrough_counters", &BranchAggregation::fallthrough_counters,
              UnorderedElementsAre(Pair(FieldsAre(0x1008, 0x1010), Eq(17)))))));
}

TEST(FrequenciesBranchAggregator,
     AggregateIgnoresNonFallthroughNotTakenBranches) {
  PropellerStats stats;
  EXPECT_THAT(
      FrequenciesBranchAggregator(
          {.not_taken_branch_counters = {{{.address = 0x1000}, 100}}},
          /*stats=*/{}, /*instruction_size=*/4)
          .Aggregate(
              BinaryAddressMapper(
                  /*selected_functions=*/{1}, /*bb_addr_map=*/
                  {{{{.BaseAddress = 0x1000,
                      .BBEntries =
                          {
                              BBAddrMap::BBEntry(
                                  /*ID=*/0, /*Offset=*/0, /*Size=*/4,
                                  /*Metadata=*/{.CanFallThrough = false}),
                              BBAddrMap::BBEntry(
                                  /*ID=*/1, /*Offset=*/8, /*Size=*/8,
                                  /*Metadata=*/{}),
                          }}}}},
                  /*bb_handles=*/
                  {{.function_index = 0, .bb_index = 0},
                   {.function_index = 0, .bb_index = 1}},
                  /*symbol_info_map=*/{}),
              stats),
      IsOkAndHolds(Field("fallthrough_counters",
                         &BranchAggregation::fallthrough_counters, IsEmpty())));
}

TEST(FrequenciesBranchAggregator, AggregatesBlocksEndingInBranches) {
  PropellerStats stats;
  EXPECT_THAT(
      FrequenciesBranchAggregator(
          {.taken_branch_counters = {{{.from = 0x1014, .to = 0x1000}, 50},
                                     {{.from = 0x1000, .to = 0x1014}, 49}}},
          /*stats=*/{}, /*instruction_size=*/4)
          .Aggregate(BinaryAddressMapper(
                         /*selected_functions=*/{1}, /*bb_addr_map=*/
                         {{{{.BaseAddress = 0x1000,
                             .BBEntries =
                                 {
                                     BBAddrMap::BBEntry(
                                         /*ID=*/0, /*Offset=*/0x0, /*Size=*/4,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/1, /*Offset=*/0x4, /*Size=*/8,
                                         /*Metadata=*/{.CanFallThrough = true}),
                                     BBAddrMap::BBEntry(
                                         /*ID=*/2, /*Offset=*/0x14, /*Size=*/4,
                                         /*Metadata=*/{}),
                                 }}}}},
                         /*bb_handles=*/
                         {{.function_index = 0, .bb_index = 0},
                          {.function_index = 0, .bb_index = 1},
                          {.function_index = 0, .bb_index = 2}},
                         /*symbol_info_map=*/{}),
                     stats),
      IsOkAndHolds(Field("fallthrough_counters",
                         &BranchAggregation::fallthrough_counters, IsEmpty())));
}
}  // namespace
}  // namespace propeller
