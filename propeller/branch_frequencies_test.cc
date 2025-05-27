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

#include "propeller/branch_frequencies.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/parse_text_proto.h"
#include "propeller/protocol_buffer_matchers.h"
#include "propeller/status_testing_macros.h"

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
