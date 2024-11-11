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
