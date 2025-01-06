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

#include "propeller/propeller_statistics.h"

#include "gtest/gtest.h"
#include "propeller/cfg_edge_kind.h"

namespace propeller {
namespace {

TEST(PropellerStatisticsTest, TotalEdgeWeightCreatedDoesntOverflow) {
  PropellerStats statistics = {
      .cfg_stats = {.total_edge_weight_by_kind =
                        {{CFGEdgeKind::kBranchOrFallthough, 147121896},
                         {CFGEdgeKind::kCall, 152487202},
                         {CFGEdgeKind::kRet, 1902652788}}},
  };
  EXPECT_EQ(statistics.cfg_stats.total_edge_weight_created(), 2202261886);
}

}  // namespace
}  // namespace  propeller
