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

#include "propeller/spe_tid_pid_provider.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gtest/gtest.h"
#include "propeller/parse_text_proto.h"
#include "propeller/status_testing_macros.h"
#include "src/quipper/perf_data.pb.h"
#include "src/quipper/perf_parser.h"

namespace propeller {
namespace {
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::propeller_testing::ParseTextProtoOrDie;
using ::testing::Eq;

google::protobuf::RepeatedPtrField<quipper::PerfDataProto_PerfEvent>
ToRepeatedPtrField(std::vector<quipper::PerfDataProto_PerfEvent> span) {
  google::protobuf::RepeatedPtrField<quipper::PerfDataProto_PerfEvent> result;
  for (quipper::PerfDataProto_PerfEvent& element : span) {
    result.Add(std::move(element));
  }
  return result;
}

TEST(SpeTidPidProvider, GetPidReturnsTidIfNoTimestamps) {
  EXPECT_THAT(SpeTidPidProvider(ToRepeatedPtrField({ParseTextProtoOrDie(R"pb(
                time_conv_event { time_mult: 1 time_shift: 0 time_zero: 10 }
              )pb")}))
                  .GetPid({.context = {.id = 1, .el1 = true}}),
              IsOkAndHolds(Eq(1)));
}

TEST(SpeTidPidProvider, GetPidReturnsTidIfEarlierThanTimestamps) {
  EXPECT_THAT(SpeTidPidProvider(ToRepeatedPtrField(
                                    {ParseTextProtoOrDie(R"pb(time_conv_event {
                                                                time_mult: 1
                                                                time_shift: 0
                                                                time_zero: 0
                                                              })pb"),
                                     ParseTextProtoOrDie(R"pb(fork_event {
                                                                pid: 123
                                                                tid: 1
                                                                fork_time_ns: 10
                                                              })pb")}))
                  .GetPid({.timestamp = 1, .context = {.id = 1, .el1 = true}}),
              IsOkAndHolds(Eq(1)));
}

TEST(SpeTidPidProvider, GetPidReturnsPidForForked) {
  EXPECT_THAT(
      SpeTidPidProvider(
          ToRepeatedPtrField(
              {ParseTextProtoOrDie(R"pb(
                 time_conv_event { time_mult: 1 time_shift: 0 time_zero: 10 }
               )pb"),
               ParseTextProtoOrDie(R"pb(
                 fork_event { pid: 123 tid: 456 fork_time_ns: 10 }
               )pb")}))
          .GetPid({.timestamp = 10, .context = {.id = 456, .el1 = true}}),
      IsOkAndHolds(123));
}

TEST(SpeTidPidProvider, GetPidReturnsPidForSampled) {
  EXPECT_THAT(
      SpeTidPidProvider(
          ToRepeatedPtrField(
              {ParseTextProtoOrDie(R"pb(
                 time_conv_event { time_mult: 1 time_shift: 0 time_zero: 10 }
               )pb"),
               ParseTextProtoOrDie(R"pb(
                 sample_event: { pid: 42 tid: 100 sample_time_ns: 100 }
               )pb")}))
          .GetPid({.timestamp = 100, .context = {.id = 100, .el1 = true}}),
      IsOkAndHolds(42));
}

TEST(SpeTidPidProvider, GetPidReturnsPidForSwitched) {
  EXPECT_THAT(
      SpeTidPidProvider(
          ToRepeatedPtrField(
              {ParseTextProtoOrDie(R"pb(
                 time_conv_event { time_mult: 1 time_shift: 0 time_zero: 10 }
               )pb"),
               ParseTextProtoOrDie(R"pb(
                 sample_event: { pid: 42 tid: 100 sample_time_ns: 100 }
               )pb"),
               ParseTextProtoOrDie(R"pb(
                 header: { type: 15 }
                 context_switch_event: {
                   sample_info: { pid: 50 tid: 100 sample_time_ns: 104 }
                 }
               )pb")}))
          .GetPid({.timestamp = 94, .context = {.id = 100, .el1 = true}}),
      IsOkAndHolds(50));
}

TEST(SpeTidPidProvider, GetPidReturnsErrorForInvalidContext) {
  EXPECT_THAT(SpeTidPidProvider({}).GetPid({.timestamp = 94, .context = {}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}
}  // namespace
}  // namespace propeller
