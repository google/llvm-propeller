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

#ifndef PROPELLER_FUNCTION_LAYOUT_INFO_MATCHERS_H_
#define PROPELLER_FUNCTION_LAYOUT_INFO_MATCHERS_H_

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/cfg_id.h"
#include "propeller/function_layout_info.h"
#include "propeller/status_testing_macros.h"

namespace propeller {

// Returns a matcher for `CFGScore` to match that its intra- and inter- scores
// are not more than `epsilon` away from `intra_score` and `inter_out_score`.
testing::Matcher<CFGScore> CfgScoreIsNear(double intra_score,
                                          double inter_out_score,
                                          double epsilon);

MATCHER_P(BbIdIs, bb_id_matcher,
          "has bb_id that " +
              testing::DescribeMatcher<int>(bb_id_matcher, negation)) {
  return testing::ExplainMatchResult(
      testing::Field("bb_id", &FullIntraCfgId::bb_id, bb_id_matcher), arg,
      result_listener);
}

MATCHER_P(HasFullBbIds, full_bb_ids_matcher,
          "has full_bb_ids that " +
              testing::DescribeMatcher<std::vector<FullIntraCfgId>>(
                  full_bb_ids_matcher, negation)) {
  return testing::ExplainMatchResult(
      testing::Property("full_bb_ids", &FunctionLayoutInfo::BbChain::GetAllBbs,
                        full_bb_ids_matcher),
      arg, result_listener);
}

MATCHER_P(BbBundleIs, full_bb_ids_matcher,
          "has full_bb_ids that " +
              testing::DescribeMatcher<std::vector<FullIntraCfgId>>(
                  full_bb_ids_matcher, negation)) {
  return testing::ExplainMatchResult(
      testing::Field("full_bb_ids", &FunctionLayoutInfo::BbBundle::full_bb_ids,
                     full_bb_ids_matcher),
      arg, result_listener);
}

MATCHER_P2(
    BbChainIs, layout_index_matcher, bb_bundles_matcher,
    "has layout_index that " +
        testing::DescribeMatcher<unsigned>(layout_index_matcher, negation) +
        " and bb_bundles that " +
        testing::DescribeMatcher<std::vector<FunctionLayoutInfo::BbBundle>>(
            bb_bundles_matcher, negation)) {
  return testing::ExplainMatchResult(
      testing::AllOf(
          testing::Field("layout_index",
                         &FunctionLayoutInfo::BbChain::layout_index,
                         layout_index_matcher),
          testing::Field("bb_bundles", &FunctionLayoutInfo::BbChain::bb_bundles,
                         bb_bundles_matcher)),
      arg, result_listener);
}

MATCHER_P4(
    FunctionLayoutInfoIs, bb_chains_matcher, original_score_matcher,
    optimized_score_matcher, cold_chain_layout_index_matcher,
    "has bb_chains that " +
        testing::DescribeMatcher<std::vector<FunctionLayoutInfo::BbChain>>(
            bb_chains_matcher, negation) +
        " and original_score that " +
        testing::DescribeMatcher<CFGScore>(original_score_matcher, negation) +
        " and optimized_score that " +
        testing::DescribeMatcher<CFGScore>(optimized_score_matcher, negation) +
        " and cold_chain_layout_index that " +
        testing::DescribeMatcher<unsigned>(cold_chain_layout_index_matcher,
                                           negation)) {
  return testing::ExplainMatchResult(
      testing::AllOf(
          testing::Field("bb_chains", &FunctionLayoutInfo::bb_chains,
                         bb_chains_matcher),
          testing::Field("intra_cfg_score", &FunctionLayoutInfo::original_score,
                         original_score_matcher),
          testing::Field("inter_cfg_score",
                         &FunctionLayoutInfo::optimized_score,
                         optimized_score_matcher),
          testing::Field("cold_chain_layout_index",
                         &FunctionLayoutInfo::cold_chain_layout_index,
                         cold_chain_layout_index_matcher)),
      arg, result_listener);
}

}  // namespace propeller
#endif  // PROPELLER_FUNCTION_LAYOUT_INFO_MATCHERS_H_
