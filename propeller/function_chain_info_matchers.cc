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

#include "propeller/function_chain_info_matchers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/function_chain_info.h"
#include "propeller/status_testing_macros.h"

namespace propeller {

testing::Matcher<CFGScore> CfgScoreIsNear(double intra_score,
                                          double inter_out_score,
                                          double epsilon) {
  return AllOf(testing::Field("intra_score", &CFGScore::intra_score,
                              testing::DoubleNear(intra_score, epsilon)),
               testing::Field("inter_out_score", &CFGScore::inter_out_score,
                              testing::DoubleNear(inter_out_score, epsilon)));
}

}  // namespace propeller
