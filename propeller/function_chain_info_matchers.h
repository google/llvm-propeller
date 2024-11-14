#ifndef PROPELLER_FUNCTION_CHAIN_INFO_MATCHERS_H_
#define PROPELLER_FUNCTION_CHAIN_INFO_MATCHERS_H_

#include <vector>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/cfg_id.h"
#include "propeller/function_chain_info.h"

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
      testing::Property("full_bb_ids", &FunctionChainInfo::BbChain::GetAllBbs,
                        full_bb_ids_matcher),
      arg, result_listener);
}

MATCHER_P(BbBundleIs, full_bb_ids_matcher,
          "has full_bb_ids that " +
              testing::DescribeMatcher<std::vector<FullIntraCfgId>>(
                  full_bb_ids_matcher, negation)) {
  return testing::ExplainMatchResult(
      testing::Field("full_bb_ids", &FunctionChainInfo::BbBundle::full_bb_ids,
                     full_bb_ids_matcher),
      arg, result_listener);
}

MATCHER_P2(
    BbChainIs, layout_index_matcher, bb_bundles_matcher,
    "has layout_index that " +
        testing::DescribeMatcher<unsigned>(layout_index_matcher, negation) +
        " and bb_bundles that " +
        testing::DescribeMatcher<std::vector<FunctionChainInfo::BbBundle>>(
            bb_bundles_matcher, negation)) {
  return testing::ExplainMatchResult(
      testing::AllOf(
          testing::Field("layout_index",
                         &FunctionChainInfo::BbChain::layout_index,
                         layout_index_matcher),
          testing::Field("bb_bundles", &FunctionChainInfo::BbChain::bb_bundles,
                         bb_bundles_matcher)),
      arg, result_listener);
}

MATCHER_P5(
    FunctionChainInfoIs, function_index_matcher, bb_chains_matcher,
    original_score_matcher, optimized_score_matcher,
    cold_chain_layout_index_matcher,
    "has function_index that " +
        testing::DescribeMatcher<int>(function_index_matcher, negation) +
        "has bb_chains that " +
        testing::DescribeMatcher<std::vector<FunctionChainInfo::BbChain>>(
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
          testing::Field("function_index", &FunctionChainInfo::function_index,
                         function_index_matcher),
          testing::Field("bb_chains", &FunctionChainInfo::bb_chains,
                         bb_chains_matcher),
          testing::Field("intra_cfg_score", &FunctionChainInfo::original_score,
                         original_score_matcher),
          testing::Field("inter_cfg_score", &FunctionChainInfo::optimized_score,
                         optimized_score_matcher),
          testing::Field("cold_chain_layout_index",
                         &FunctionChainInfo::cold_chain_layout_index,
                         cold_chain_layout_index_matcher)),
      arg, result_listener);
}

}  // namespace propeller
#endif  // PROPELLER_FUNCTION_CHAIN_INFO_MATCHERS_H_
