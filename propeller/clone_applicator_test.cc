#include "propeller/clone_applicator.h"

#include <memory>
#include <string>
#include <vector>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_matchers.h"
#include "propeller/cfg_testutil.h"
#include "propeller/mock_program_cfg_builder.h"
#include "propeller/multi_cfg_test_case.h"
#include "propeller/parse_text_proto.h"
#include "propeller/path_clone_evaluator.h"
#include "propeller/path_node.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
namespace {
using ::propeller_testing::ParseTextProtoOrDie;
using ::testing::_;
using ::testing::Contains;
using ::testing::Pair;
using ::testing::Pointee;

struct ApplyCloningsTestCase {
  struct PathCloningArg {
    int function_index;
    // Path predecessor block.
    int pred_bb_index;
    // A vector of bb_indexes representing a path from root of the path tree.
    std::vector<int> path;
  };

  std::string test_name;
  PropellerOptions propeller_options;
  // Argument for building the `ProgramPathProfile`.
  ProgramPathProfileArg path_profile_arg = GetDefaultPathProfileArg();
  // Argument for building the `ProgramCfg`.
  MultiCfgArg program_cfg_arg = GetDefaultProgramCfgArg();

  // Arguments for building a `std::vector<PathCloning>`.
  std::vector<PathCloningArg> path_args;

  // Matcher.
  absl::flat_hash_map<int, CfgMatcher> cfg_matcher_by_function_index;
};

using ApplyCloningsTest = testing::TestWithParam<ApplyCloningsTestCase>;

TEST_P(ApplyCloningsTest, TestCloningChangeOnCfgs) {
  const ApplyCloningsTestCase& test_case = GetParam();
  std::unique_ptr<ProgramCfg> program_cfg =
      BuildFromCfgArg(test_case.program_cfg_arg);
  ProgramPathProfile path_profile(test_case.path_profile_arg);
  // Synthesize clonings based on `path_args`.
  absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>> clonings;
  for (const auto& path_arg : test_case.path_args) {
    const PathNode* path_node = path_profile.path_profiles_by_function_index()
                                    .at(path_arg.function_index)
                                    .path_trees_by_root_bb_index()
                                    .at(path_arg.path[0])
                                    .get();
    // Follow the path to find the corresponding `PathNode`.
    for (int i = 1; i < path_arg.path.size(); ++i) {
      int bb_index = path_arg.path[i];
      path_node = path_node->children().at(bb_index).get();
    }
    clonings[path_arg.function_index].push_back(
        {.path_cloning = {.path_node = path_node,
                          .function_index = path_arg.function_index,
                          .path_pred_bb_index = path_arg.pred_bb_index}});
  }
  CloneApplicatorStats clone_applicator_stats =
      ApplyClonings(test_case.propeller_options.code_layout_params(),
                    test_case.propeller_options.path_profile_options(),
                    clonings, *program_cfg);

  for (const auto& [function_index, cfg_matcher] :
       test_case.cfg_matcher_by_function_index) {
    EXPECT_THAT(clone_applicator_stats.clone_cfgs_by_function_index,
                Contains(Pair(function_index, Pointee(cfg_matcher))));
  }
}

INSTANTIATE_TEST_SUITE_P(
    ApplyCloningsTests, ApplyCloningsTest,
    ::testing::ValuesIn<ApplyCloningsTestCase>(
        {{.test_name = "Path3Pred1",
          .propeller_options = ParseTextProtoOrDie(R"pb(code_layout_params {
                                                          call_chain_clustering:
                                                              false
                                                        })pb"),
          .path_args = {{.function_index = 6, .pred_bb_index = 1, .path = {3}}},
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(
                    CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                     NodeIntraIdIs(IntraCfgId{1, 0}),
                                     NodeIntraIdIs(IntraCfgId{2, 0}),
                                     NodeIntraIdIs(IntraCfgId{3, 0}),
                                     NodeIntraIdIs(IntraCfgId{4, 0}),
                                     NodeIntraIdIs(IntraCfgId{5, 0}),
                                     NodeIntraIdIs(IntraCfgId{3, 1})}),
                    CfgIntraEdgesMatcher(
                        {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                   NodeIntraIdIs(IntraCfgId{1, 0}), 181, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                   NodeIntraIdIs(IntraCfgId{2, 0}), 660, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                   NodeIntraIdIs(IntraCfgId{3, 0}), 1, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                   NodeIntraIdIs(IntraCfgId{3, 0}), 656, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                   NodeIntraIdIs(IntraCfgId{4, 0}), 10, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                   NodeIntraIdIs(IntraCfgId{4, 0}), 5, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                   NodeIntraIdIs(IntraCfgId{5, 0}), 677, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                   NodeIntraIdIs(IntraCfgId{5, 0}), 185, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                   NodeIntraIdIs(IntraCfgId{3, 1}), 185, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                   NodeIntraIdIs(IntraCfgId{4, 0}), 170, _),
                         IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                   NodeIntraIdIs(IntraCfgId{5, 0}), 13, _)}),
                    CfgInterEdgesMatcher(
                        {IsCfgEdge(
                             NodeInterIdIs(InterCfgId{.function_index = 6,
                                                      .intra_cfg_id = {4, 0}}),
                             NodeInterIdIs(InterCfgId{.function_index = 7,
                                                      .intra_cfg_id = {0, 0}}),
                             100, CFGEdgeKind::kCall),
                         IsCfgEdge(
                             NodeInterIdIs(InterCfgId{.function_index = 6,
                                                      .intra_cfg_id = {4, 0}}),
                             NodeInterIdIs(InterCfgId{.function_index = 8,
                                                      .intra_cfg_id = {0, 0}}),
                             85, CFGEdgeKind::kCall),
                         IsCfgEdge(
                             NodeInterIdIs(InterCfgId{.function_index = 6,
                                                      .intra_cfg_id = {5, 0}}),
                             NodeInterIdIs(InterCfgId{.function_index = 9,
                                                      .intra_cfg_id = {1, 0}}),
                             875, CFGEdgeKind::kRet)}))},
               {7, CfgMatcher(
                       CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                       CfgInterEdgesMatcher({IsCfgEdge(
                           NodeInterIdIs(InterCfgId{.function_index = 7,
                                                    .intra_cfg_id = {1, 0}}),
                           NodeInterIdIs(InterCfgId{.function_index = 6,
                                                    .intra_cfg_id = {4, 0}}),
                           100, CFGEdgeKind::kRet)}))},
               {8, CfgMatcher(
                       CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                       CfgInterEdgesMatcher({IsCfgEdge(
                           NodeInterIdIs(InterCfgId{.function_index = 8,
                                                    .intra_cfg_id = {1, 0}}),
                           NodeInterIdIs(InterCfgId{
                               .function_index = 10, .intra_cfg_id = {0, 0}}),
                           85, CFGEdgeKind::kCall)}))},
               {10, CfgMatcher(
                        CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                        CfgInterEdgesMatcher({IsCfgEdge(
                            NodeInterIdIs(InterCfgId{
                                .function_index = 10, .intra_cfg_id = {0, 0}}),
                            NodeInterIdIs(InterCfgId{.function_index = 6,
                                                     .intra_cfg_id = {4, 0}}),
                            85, CFGEdgeKind::kRet)}))}}},
         {.test_name = "Path3To4Pred1",
          .propeller_options = ParseTextProtoOrDie(R"pb(code_layout_params {
                                                          call_chain_clustering:
                                                              false
                                                        })pb"),
          .path_args =
              {{.function_index = 6, .pred_bb_index = 1, .path = {3, 4}}},
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                            NodeIntraIdIs(IntraCfgId{1, 0}),
                                            NodeIntraIdIs(IntraCfgId{2, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 0}),
                                            NodeIntraIdIs(IntraCfgId{5, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 1}),
                                            NodeIntraIdIs(IntraCfgId{4, 1})}),
                           CfgIntraEdgesMatcher(
                               {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{1, 0}),
                                          181, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{2, 0}),
                                          660, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}), 1,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          656, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 5,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          677, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 15,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 1}),
                                          185, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{4, 1}),
                                          170, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 13,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          170, _)}),
                           CfgInterEdgesMatcher(
                               {IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    15, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    0, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    85, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    85, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    875, CFGEdgeKind::kRet)}))},
               {7, CfgMatcher(CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                              CfgInterEdgesMatcher(
                                  {IsCfgEdge(
                                       NodeInterIdIs(
                                           InterCfgId{.function_index = 7,
                                                      .intra_cfg_id = {1, 0}}),
                                       NodeInterIdIs(
                                           InterCfgId{.function_index = 6,
                                                      .intra_cfg_id = {4, 1}}),
                                       85, CFGEdgeKind::kRet),
                                   IsCfgEdge(
                                       NodeInterIdIs(
                                           InterCfgId{.function_index = 7,
                                                      .intra_cfg_id = {1, 0}}),
                                       NodeInterIdIs(
                                           InterCfgId{.function_index = 6,
                                                      .intra_cfg_id = {4, 0}}),
                                       15, CFGEdgeKind::kRet)}))},
               {8, CfgMatcher(
                       CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                       CfgInterEdgesMatcher(
                           {IsCfgEdge(NodeInterIdIs(
                                          InterCfgId{.function_index = 8,
                                                     .intra_cfg_id = {1, 0}}),
                                      NodeInterIdIs(
                                          InterCfgId{.function_index = 10,
                                                     .intra_cfg_id = {0, 0}}),
                                      85, CFGEdgeKind::kCall)}))},
               {10, CfgMatcher(
                        CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                        CfgInterEdgesMatcher(
                            {IsCfgEdge(
                                 NodeInterIdIs(
                                     InterCfgId{.function_index = 10,
                                                .intra_cfg_id = {0, 0}}),
                                 NodeInterIdIs(InterCfgId{.function_index = 6,
                                                          .intra_cfg_id = {4,
                                                                           1}}),
                                 85, CFGEdgeKind::kRet),
                             IsCfgEdge(
                                 NodeInterIdIs(
                                     InterCfgId{.function_index = 10,
                                                .intra_cfg_id = {0, 0}}),
                                 NodeInterIdIs(InterCfgId{.function_index = 6,
                                                          .intra_cfg_id = {4,
                                                                           0}}),
                                 0,
                                 CFGEdgeKind::kRet)}))}}},
         {.test_name = "Path3To5Pred2",
          .propeller_options = ParseTextProtoOrDie(R"pb(code_layout_params {
                                                          call_chain_clustering:
                                                              false
                                                        })pb"),
          .path_args =
              {{.function_index = 6, .pred_bb_index = 2, .path = {3, 5}}},
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                            NodeIntraIdIs(IntraCfgId{1, 0}),
                                            NodeIntraIdIs(IntraCfgId{2, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 0}),
                                            NodeIntraIdIs(IntraCfgId{5, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 1}),
                                            NodeIntraIdIs(IntraCfgId{5, 1})}),
                           CfgIntraEdgesMatcher(
                               {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{1, 0}),
                                          181, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{2, 0}),
                                          660, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          186, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}),
                                          170, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 41,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          185, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 1}),
                                          656, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 1}),
                                          649, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 5,
                                          _)}),
                           CfgInterEdgesMatcher(
                               {IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    100, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    85, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    226, CFGEdgeKind::kRet),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    649, CFGEdgeKind::kRet)}))},
               {7, CfgMatcher(
                       CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                       CfgInterEdgesMatcher(
                           {IsCfgEdge(NodeInterIdIs(
                                          InterCfgId{.function_index = 7,
                                                     .intra_cfg_id = {1, 0}}),
                                      NodeInterIdIs(
                                          InterCfgId{.function_index = 6,
                                                     .intra_cfg_id = {4, 0}}),
                                      100, CFGEdgeKind::kRet)}))},
               {8, CfgMatcher(
                       CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                       CfgInterEdgesMatcher(
                           {IsCfgEdge(NodeInterIdIs(
                                          InterCfgId{.function_index = 8,
                                                     .intra_cfg_id = {1, 0}}),
                                      NodeInterIdIs(
                                          InterCfgId{.function_index = 10,
                                                     .intra_cfg_id = {0, 0}}),
                                      85, CFGEdgeKind::kCall)}))},
               {10, CfgMatcher(CfgNodesMatcher(), CfgIntraEdgesMatcher(),
                               CfgInterEdgesMatcher(
                                   {IsCfgEdge(NodeInterIdIs(InterCfgId{
                                                  .function_index = 10,
                                                  .intra_cfg_id = {0, 0}}),
                                              NodeInterIdIs(InterCfgId{
                                                  .function_index = 6,
                                                  .intra_cfg_id = {4, 0}}),
                                              85, CFGEdgeKind::kRet)}))}}},
         {.test_name =
              "TwoPaths3Pred1And4Pred2WithNegativeCloningScoreThreshold",
          .propeller_options = ParseTextProtoOrDie(
              R"pb(code_layout_params { call_chain_clustering: false }
                   path_profile_options { min_final_cloning_score: -50 })pb"),
          .path_args = {{.function_index = 6, .pred_bb_index = 1, .path = {3}},
                        {.function_index = 6, .pred_bb_index = 2, .path = {4}}},
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                            NodeIntraIdIs(IntraCfgId{1, 0}),
                                            NodeIntraIdIs(IntraCfgId{2, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 0}),
                                            NodeIntraIdIs(IntraCfgId{5, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 1}),
                                            NodeIntraIdIs(IntraCfgId{3, 1})}),
                           CfgIntraEdgesMatcher(
                               {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{1, 0}),
                                          181, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{2, 0}),
                                          660, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}), 1,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          656, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 5,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          677, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          175, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 1}),
                                          185, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}),
                                          170, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 13,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 1}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 10,
                                          _)}),
                           CfgInterEdgesMatcher(
                               {IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    90, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    85, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    875, CFGEdgeKind::kRet),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    10, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    0, CFGEdgeKind::kCall)}))}}},
         {.test_name =
              "TwoPaths4Pred2And4Pred3WithNegativeCloningScoreThreshold",
          .propeller_options = ParseTextProtoOrDie(
              R"pb(code_layout_params { call_chain_clustering: false }
                   path_profile_options { min_final_cloning_score: -50 })pb"),
          .path_args =
              {
                  {.function_index = 6, .pred_bb_index = 3, .path = {4}},
                  {.function_index = 6, .pred_bb_index = 2, .path = {4}},
              },
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                            NodeIntraIdIs(IntraCfgId{1, 0}),
                                            NodeIntraIdIs(IntraCfgId{2, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 0}),
                                            NodeIntraIdIs(IntraCfgId{5, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 1}),
                                            NodeIntraIdIs(IntraCfgId{4, 2})}),
                           CfgIntraEdgesMatcher(
                               {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{1, 0}),
                                          181, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{2, 0}),
                                          660, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          186, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          656, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          690, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 1}),
                                          175, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          175, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 2}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 2}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 10,
                                          _)}),
                           CfgInterEdgesMatcher(
                               {IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    0, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    0, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    875, CFGEdgeKind::kRet),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    90, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    85, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 2}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    10, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 2}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    0, CFGEdgeKind::kCall)}))}}},
         {.test_name = "TwoPaths3To4Pred1And3To5Pred2",
          .propeller_options = ParseTextProtoOrDie(R"pb(code_layout_params {
                                                          call_chain_clustering:
                                                              false
                                                        })pb"),
          .path_args =
              {{.function_index = 6, .pred_bb_index = 1, .path = {3, 4}},
               {.function_index = 6, .pred_bb_index = 2, .path = {3, 5}}},
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                            NodeIntraIdIs(IntraCfgId{1, 0}),
                                            NodeIntraIdIs(IntraCfgId{2, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 0}),
                                            NodeIntraIdIs(IntraCfgId{5, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 1}),
                                            NodeIntraIdIs(IntraCfgId{5, 1})}),
                           CfgIntraEdgesMatcher(
                               {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{1, 0}),
                                          181, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{2, 0}),
                                          660, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          186, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}),
                                          170, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 41,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          185, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 1}),
                                          656, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 1}),
                                          649, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 5,
                                          _)}),
                           CfgInterEdgesMatcher(
                               {IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 7,
                                                   .intra_cfg_id = {0, 0}}),
                                    100, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {4, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 8,
                                                   .intra_cfg_id = {0, 0}}),
                                    85, CFGEdgeKind::kCall),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 0}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    226, CFGEdgeKind::kRet),
                                IsCfgEdge(
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 6,
                                                   .intra_cfg_id = {5, 1}}),
                                    NodeInterIdIs(
                                        InterCfgId{.function_index = 9,
                                                   .intra_cfg_id = {1, 0}}),
                                    649, CFGEdgeKind::kRet)}))}}},
         {.test_name = "TwoConflictingPaths3To4Pred1And4To5Pred2",
          .propeller_options = ParseTextProtoOrDie(R"pb(code_layout_params {
                                                          call_chain_clustering:
                                                              false
                                                        })pb"),
          .path_args =
              {{.function_index = 6, .pred_bb_index = 1, .path = {3, 4}},
               {.function_index = 6, .pred_bb_index = 3, .path = {4, 5}}},
          .cfg_matcher_by_function_index =
              {{6,
                CfgMatcher(CfgNodesMatcher({NodeIntraIdIs(IntraCfgId{0, 0}),
                                            NodeIntraIdIs(IntraCfgId{1, 0}),
                                            NodeIntraIdIs(IntraCfgId{2, 0}),
                                            NodeIntraIdIs(IntraCfgId{3, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 0}),
                                            NodeIntraIdIs(IntraCfgId{5, 0}),
                                            NodeIntraIdIs(IntraCfgId{4, 1}),
                                            NodeIntraIdIs(IntraCfgId{5, 1})}),
                           CfgIntraEdgesMatcher(
                               {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{1, 0}),
                                          181, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                                          NodeIntraIdIs(IntraCfgId{2, 0}),
                                          660, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          186, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{3, 0}),
                                          656, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 0}), 0,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}),
                                          690, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                                          NodeIntraIdIs(IntraCfgId{5, 0}), 10,
                                          _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                                          NodeIntraIdIs(IntraCfgId{4, 1}),
                                          175, _),
                                IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 1}),
                                          NodeIntraIdIs(IntraCfgId{5, 1}),
                                          175, _)}))}}}}),
    [](const ::testing::TestParamInfo<ApplyCloningsTest::ParamType>& info) {
      return info.param.test_name;
    });
}  // namespace
}  // namespace propeller
