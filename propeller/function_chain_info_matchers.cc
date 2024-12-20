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
