#include "propeller/branch_frequencies.h"

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/parse_text_proto.h"
#include "propeller/protocol_buffer_matchers.h"

namespace propeller {
namespace {

using ::propeller_testing::EqualsProto;
using ::propeller_testing::ParseTextProtoOrDie;
using ::testing::AllOf;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(BranchFrequencies, GetNumberOfTakenBranchCounters) {
  const BranchFrequencies kFrequencies = {
      .taken_branch_counters = {{{.from = 0, .to = 1}, 2},
                                {{.from = 3, .to = 4}, 5}},
      .not_taken_branch_counters = {{{.address = 6}, 7}},
  };

  EXPECT_EQ(kFrequencies.GetNumberOfTakenBranchCounters(), 7);
}

TEST(BranchFrequencies, Create) {
  EXPECT_THAT(
      BranchFrequencies::Create(ParseTextProtoOrDie(R"pb(
        taken_counts: { source: 0, dest: 1, count: 2 }
        not_taken_counts: { address: 6, count: 7 }
      )pb")),
      AllOf(Field(&BranchFrequencies::taken_branch_counters,
                  UnorderedElementsAre(
                      Pair(FieldsAre(/*.from=*/0, /*.to=*/1), 2))),
            Field(&BranchFrequencies::not_taken_branch_counters,
                  UnorderedElementsAre(Pair(FieldsAre(/*.address=*/6), 7)))));
}

TEST(BranchFrequencies, CreateMergesCounts) {
  EXPECT_THAT(
      BranchFrequencies::Create(ParseTextProtoOrDie(R"pb(
        taken_counts:
        [ { source: 1, dest: 2, count: 3 }
          , { source: 1, dest: 2, count: 3 }])pb")),
      Field(&BranchFrequencies::taken_branch_counters,
            UnorderedElementsAre(Pair(FieldsAre(/*.from=*/1, /*.to=*/2), 6))));
}

TEST(BranchFrequencies, ToProto) {
  BranchFrequencies frequencies = {
      .taken_branch_counters = {{{.from = 0, .to = 1}, 2}},
      .not_taken_branch_counters = {
          {{.address = 6}, 7},
      }};

  EXPECT_THAT(frequencies.ToProto(), EqualsProto(R"pb(
                taken_counts: { source: 0, dest: 1, count: 2 }
                not_taken_counts: { address: 6, count: 7 }
              )pb"));
}
}  // namespace
}  // namespace propeller
