#include "propeller/proto_branch_frequencies_aggregator.h"

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/parse_text_proto.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::absl_testing::IsOkAndHolds;
using ::propeller_testing::ParseTextProtoOrDie;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::Pair;

TEST(ProtoBranchFrequenciesAggregator, AggregateBranchFrequencies) {
  BranchFrequenciesProto proto = ParseTextProtoOrDie(R"pb(
    taken_counts: { source: 1 dest: 2 count: 3 }
    not_taken_counts: { address: 1 count: 2 }
  )pb");

  PropellerStats ignored;

  EXPECT_THAT(
      ProtoBranchFrequenciesAggregator::Create(ParseTextProtoOrDie(R"pb(
        taken_counts: { source: 1 dest: 2 count: 3 }
        not_taken_counts: { address: 1 count: 2 }
      )pb"))
          .AggregateBranchFrequencies(PropellerOptions{}, BinaryContent{},
                                      ignored),
      IsOkAndHolds(
          AllOf(Field("taken_branch_counters",
                      &BranchFrequencies::taken_branch_counters,
                      ElementsAre(Pair(FieldsAre(/*.from=*/1, /*.to=*/2), 3))),
                Field("not_taken_branch_counters",
                      &BranchFrequencies::not_taken_branch_counters,
                      ElementsAre(Pair(FieldsAre(/*.address=*/1), 2))))));
}

}  // namespace
}  // namespace propeller
