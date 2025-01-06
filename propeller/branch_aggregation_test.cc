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

#include "propeller/branch_aggregation.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::testing::UnorderedElementsAre;

TEST(BranchAggregation, GetNumberOfBranchCounters) {
  EXPECT_EQ((BranchAggregation{.branch_counters = {{{.from = 1, .to = 2}, 3},
                                                   {{.from = 3, .to = 4}, 5}}}
                 .GetNumberOfBranchCounters()),
            8);
}

TEST(BranchAggregation, GetUniqueAddresses) {
  EXPECT_THAT(
      (BranchAggregation{.branch_counters = {{{.from = 1, .to = 2}, 1},
                                             {{.from = 3, .to = 3}, 1}},
                         .fallthrough_counters = {{{.from = 3, .to = 3}, 1},
                                                  {{.from = 4, .to = 5}, 1}}}
           .GetUniqueAddresses()),
      UnorderedElementsAre(1, 2, 3, 4, 5));
}

}  // namespace
}  // namespace propeller
