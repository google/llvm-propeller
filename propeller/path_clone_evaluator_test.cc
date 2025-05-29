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

#include "propeller/path_clone_evaluator.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/bb_handle.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_matchers.h"
#include "propeller/cfg_node.h"
#include "propeller/cfg_testutil.h"
#include "propeller/code_layout.h"
#include "propeller/function_chain_info.h"
#include "propeller/function_chain_info_matchers.h"
#include "propeller/mock_program_cfg_builder.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::DescribeMatcher;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::Key;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::Property;
using ::testing::UnorderedElementsAre;

constexpr double kEpsilon = 1e-2;

MATCHER_P3(EvaluatedPathCloningIs, path_cloning_matcher, score_matcher,
           cfg_change_matcher,
           absl::StrCat(
               "is an evaluated path cloning that ",
               (negation ? " doesn't have" : " has"), " path cloning that ",
               DescribeMatcher<PathCloning>(path_cloning_matcher, negation),
               (negation ? " or doesn't have" : " and has"), " score that ",
               DescribeMatcher<std::optional<double>>(score_matcher, negation),
               (negation ? " or doesn't have" : " and has"),
               " cfg change that ",
               DescribeMatcher<CfgChangeFromPathCloning>(cfg_change_matcher,
                                                         negation))) {
  return ExplainMatchResult(path_cloning_matcher, arg.path_cloning,
                            result_listener) &&
         ExplainMatchResult(score_matcher, arg.score, result_listener) &&
         ExplainMatchResult(cfg_change_matcher, arg.cfg_change,
                            result_listener);
}

// Returns a map from bb_index to PathNodeArg from `args`.
absl::node_hash_map<int, PathNodeArg> GetMapByIndex(
    absl::Span<const PathNodeArg> args) {
  absl::node_hash_map<int, PathNodeArg> result;
  for (const auto& arg : args) {
    result[arg.node_bb_index] = arg;
  }
  return result;
}

// Returns a map from function_index to FunctionPathProfileArg from `args`.
absl::flat_hash_map<int, FunctionPathProfileArg> GetMapByIndex(
    absl::Span<const FunctionPathProfileArg> args) {
  absl::flat_hash_map<int, FunctionPathProfileArg> result;
  for (const auto& arg : args) {
    result[arg.function_index] = arg;
  }
  return result;
}

ProgramPathProfileArg GetDefaultPathProfileArg() {
  auto children_of_3_args = GetMapByIndex(
      {{.node_bb_index = 4,
        .path_pred_info =
            {.entries = {{1,
                          {.freq = 160,
                           .cache_pressure = 7.2,
                           .call_freqs = {{CallRetInfo{.callee = 7}, 80},
                                          {CallRetInfo{.callee = 8}, 80}}}},
                         {2,
                          {.freq = 4,
                           .cache_pressure = 6.2,
                           .call_freqs = {{CallRetInfo{.callee = 7}, 4},
                                          {CallRetInfo{.callee = 8}, 0}}}}},
             .missing_pred_entry =
                 {.freq = 9,
                  .call_freqs = {{CallRetInfo{.callee = 7}, 4},
                                 {CallRetInfo{.callee = 8}, 5}}}},
        .children_args = GetMapByIndex(
            {{.node_bb_index = 5,
              .path_pred_info = {.entries = {{1, {.freq = 160}},
                                             {2, {.freq = 4}}},
                                 .missing_pred_entry = {.freq = 9}}}})},
       {.node_bb_index = 5,
        .path_pred_info = {.entries = {{1, {.freq = 13}}, {2, {.freq = 649}}},
                           .missing_pred_entry = {.freq = 28}}},
       {.node_bb_index = 1,
        .path_pred_info = {.entries = {{1, {.freq = 9}}},
                           .missing_pred_entry = {.freq = 1}}}});

  auto children_of_4_args = GetMapByIndex(
      {{.node_bb_index = 5,
        .path_pred_info = {.entries = {{2, {.freq = 10}}, {3, {.freq = 173}}},
                           .missing_pred_entry = {.freq = 2}}}});

  return {
      .function_path_profile_args = GetMapByIndex(
          {{.function_index = 6,
            .path_node_args = GetMapByIndex(
                {{.node_bb_index = 3,
                  .path_pred_info = {.entries = {{1, {.freq = 195}},
                                                 {2, {.freq = 656}}},
                                     .missing_pred_entry = {.freq = 38}},
                  .children_args = children_of_3_args},
                 {.node_bb_index = 4,
                  .path_pred_info =
                      {.entries =
                           {{2,
                             {.freq = 10,
                              .cache_pressure = 8.2,
                              .call_freqs = {{CallRetInfo{.callee = 7}, 10},
                                             {CallRetInfo{.callee = 8}, 0}}}},
                            {3,
                             {.freq = 173,
                              .cache_pressure = 9.2,
                              .call_freqs = {{CallRetInfo{.callee = 7}, 89},
                                             {CallRetInfo{.callee = 8}, 84}}}}},
                       .missing_pred_entry =
                           {.freq = 2,
                            .call_freqs = {{CallRetInfo{.callee = 7}, 1},
                                           {CallRetInfo{.callee = 8}, 1}}}},
                  .children_args = children_of_4_args}})}})};
}

// Returns the default MultiCfgArg to build a ProgramCfg as shown below.
//
//                      **function foo**
// **************************************************************
//      +---+    660     +--------+
// +--- | 2 | <--------- |   0    |
// |    +---+            +--------+
// |      |                |
// |      |                | 181
// |      |                v
// |      |              +--------+
// |      |              |   1    | <---------+
// |      |              +--------+           |
// |      |                  |                |
// |      |                  | 196            | 10
// |      |                  v                |
// |      |     656        +--------+         |
// |      +--------------> |   3    | --------+
//                         |        | --------------+
// |                       +--------+               |
// |                           |                    |
// |                           | 175                |
// |                           v                    |
// |       10                +------------+         |
// +-----------------------> |      4     |         | 690
//                           +------------+         |
//                             |    |   |           |
//                             |    |   | 185       |
//      +----------------------+    |   |           |
//      |                           |   |           |
//      |                    +------+   |           |
// call |                    |          v           |
//  90  |              call  |       +---------+    |
//      |               85   |       |    5    | <--+
//      |                    |       +---------+
//      |                    |
// **************************************************************
//      |            *       |
//      v            *       |
//  **function bar** *       |
//   +-------+       *       |
//   |   0   |       *       |
//   +-------+       *       v
//      |            *    **function baz**
//      |            *   +-------+
//      | 90         *   |   0   |
//      v            *   +-------+
//   +-------+       *      |
//   |   1   |       *      | 85
//   +-------+       *      v
//                   *   +-------+
//                   *   |   1   |
//                   *   +-------+
// **************************************************************
MultiCfgArg GetDefaultProgramCfgArg() {
  return {
      .cfg_args =
          {{".text",
            6,
            "foo",
            {{0x1000, 0, 0x10, {.CanFallThrough = true}},
             {0x1010,
              1,
              0x7,
              {.CanFallThrough = false, .HasIndirectBranch = true}},
             {0x102a, 2, 0x4, {.CanFallThrough = true}},
             {0x1030, 3, 0x8, {.CanFallThrough = true}},
             {0x1038, 4, 0x20, {.CanFallThrough = true}},
             {0x1060, 5, 0x6, {.HasReturn = true, .CanFallThrough = false}}},
            {{0, 1, 181, CFGEdgeKind::kBranchOrFallthough},
             {0, 2, 660, CFGEdgeKind::kBranchOrFallthough},
             {1, 3, 196, CFGEdgeKind::kBranchOrFallthough},
             {2, 3, 656, CFGEdgeKind::kBranchOrFallthough},
             {2, 4, 10, CFGEdgeKind::kBranchOrFallthough},
             {3, 1, 10, CFGEdgeKind::kBranchOrFallthough},
             {3, 4, 175, CFGEdgeKind::kBranchOrFallthough},
             {3, 5, 690, CFGEdgeKind::kBranchOrFallthough},
             {4, 5, 185, CFGEdgeKind::kBranchOrFallthough}}},
           {".text",
            7,
            "bar",
            {{0x2000, 0, 0x20, {.CanFallThrough = true}},
             {0x2020, 1, 0x12, {.HasReturn = true}}},
            {{0, 1, 90, CFGEdgeKind::kBranchOrFallthough}}},
           {".text",
            8,
            "baz",
            {{0x3000, 0, 0x30, {.CanFallThrough = true}},
             {0x3030, 1, 0x13, {.HasReturn = true}}},
            {{0, 1, 85, CFGEdgeKind::kBranchOrFallthough}}}},
      .inter_edge_args = {{6, 4, 7, 0, 90, CFGEdgeKind::kCall},
                          {7, 1, 6, 4, 90, CFGEdgeKind::kRet},
                          {6, 4, 8, 0, 85, CFGEdgeKind::kCall},
                          {8, 1, 6, 4, 85, CFGEdgeKind::kRet}}};
}

TEST(PathCloneEvaluator, EvaluatesAndReturnsClonings) {
  std::unique_ptr<ProgramCfg> program_cfg =
      BuildFromCfgArg(GetDefaultProgramCfgArg());
  ProgramPathProfile path_profile(GetDefaultPathProfileArg());
  ASSERT_THAT(path_profile.path_profiles_by_function_index(), Contains(Key(6)));
  const FunctionPathProfile& function_path_profile =
      path_profile.path_profiles_by_function_index().at(6);
  PathProfileOptions path_profile_options;
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_call_chain_clustering(false);
  absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>>
      evaluated_clonings =
          EvaluateAllClonings(program_cfg.get(), &path_profile,
                              code_layout_params, path_profile_options);
  EXPECT_THAT(
      evaluated_clonings,
      UnorderedElementsAre(Pair(
          6,
          UnorderedElementsAre(
              EvaluatedPathCloningIs(
                  Property("full_path", &PathCloning::GetFullPath,
                           ElementsAre(2, 3)),
                  Optional(DoubleNear(3102.28, kEpsilon)),
                  Field("paths_to_drop",
                        &CfgChangeFromPathCloning::paths_to_drop,
                        UnorderedElementsAre(
                            function_path_profile.GetPathTree(3)))),
              EvaluatedPathCloningIs(
                  Property("full_path", &PathCloning::GetFullPath,
                           ElementsAre(2, 3, 4, 5)),
                  Optional(DoubleNear(2981.55, kEpsilon)),
                  Field(
                      "paths_to_drop", &CfgChangeFromPathCloning::paths_to_drop,
                      UnorderedElementsAre(
                          function_path_profile.GetPathTree(3),
                          function_path_profile.GetPathTree(3)->GetChild(4),
                          function_path_profile.GetPathTree(4),
                          function_path_profile.GetPathTree(3)
                              ->GetChild(4)
                              ->GetChild(5),
                          function_path_profile.GetPathTree(4)->GetChild(5)))),
              EvaluatedPathCloningIs(
                  Property("full_path", &PathCloning::GetFullPath,
                           ElementsAre(2, 3, 5)),
                  Optional(DoubleNear(4775.69, kEpsilon)),
                  Field(
                      "paths_to_drop", &CfgChangeFromPathCloning::paths_to_drop,
                      UnorderedElementsAre(
                          function_path_profile.GetPathTree(3),
                          function_path_profile.GetPathTree(3)->GetChild(5)))),
              EvaluatedPathCloningIs(
                  Property("full_path", &PathCloning::GetFullPath,
                           ElementsAre(3, 4, 5)),
                  Optional(DoubleNear(1352.8, kEpsilon)),
                  Field(
                      "paths_to_drop", &CfgChangeFromPathCloning::paths_to_drop,
                      UnorderedElementsAre(function_path_profile.GetPathTree(4),
                                           function_path_profile.GetPathTree(4)
                                               ->GetChild(5))))))));
}

TEST(PathCloneEvaluator, LimitsCloningPathLength) {
  std::unique_ptr<ProgramCfg> program_cfg =
      BuildFromCfgArg(GetDefaultProgramCfgArg());
  ProgramPathProfile path_profile(GetDefaultPathProfileArg());
  PathProfileOptions path_profile_options;
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_call_chain_clustering(false);
  path_profile_options.set_max_path_length(1);
  absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>>
      evaluated_clonings =
          EvaluateAllClonings(program_cfg.get(), &path_profile,
                              code_layout_params, path_profile_options);
  EXPECT_THAT(evaluated_clonings,
              UnorderedElementsAre(Pair(
                  6, UnorderedElementsAre(Field(
                         "path_cloning", &EvaluatedPathCloning::path_cloning,
                         Property("full_path", &PathCloning::GetFullPath,
                                  ElementsAre(2, 3)))))));
}

TEST(PathCloneEvaluator, GetsInitialChains) {
  std::unique_ptr<ProgramCfg> program_cfg =
      BuildFromCfgArg(GetDefaultProgramCfgArg());
  const ControlFlowGraph& foo_cfg = *program_cfg->GetCfgByIndex(/*index=*/6);
  ProgramPathProfile path_profile(GetDefaultPathProfileArg());
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_call_chain_clustering(false);
  FunctionChainInfo::BbChain one_chain(/*_layout_index=*/0);
  one_chain.bb_bundles.push_back(
      {.full_bb_ids = {{.intra_cfg_id = IntraCfgId{.bb_index = 0}},
                       {.intra_cfg_id = IntraCfgId{.bb_index = 2}},
                       {.intra_cfg_id = IntraCfgId{.bb_index = 5}}}});
  FunctionChainInfo optimal_chain_info =
      CodeLayout(code_layout_params, {&foo_cfg},
                 /*initial_chains=*/{{6, {std::move(one_chain)}}})
          .OrderAll()
          .front();
  ASSERT_THAT(optimal_chain_info,
              FunctionChainInfoIs(
                  6,
                  ElementsAre(BbChainIs(
                      _, ElementsAre(BbBundleIs(ElementsAre(
                                         BbIdIs(0), BbIdIs(2), BbIdIs(5))),
                                     BbBundleIs(ElementsAre(BbIdIs(1))),
                                     BbBundleIs(ElementsAre(BbIdIs(3))),
                                     BbBundleIs(ElementsAre(BbIdIs(4)))))),
                  _, _, _));
  PathCloning cloning = {
      .path_node = path_profile.path_profiles_by_function_index()
                       .at(6)
                       .path_trees_by_root_bb_index()
                       .at(3)
                       ->children()
                       .at(5)
                       .get(),
      .function_index = 6,
      .path_pred_bb_index = 1};
  ASSERT_OK_AND_ASSIGN(
      CfgChangeFromPathCloning cfg_change,
      CfgChangeBuilder(cloning, /*conflict_edges=*/{},
                       path_profile.path_profiles_by_function_index().at(6))
          .Build());

  EXPECT_THAT(
      GetInitialChains(foo_cfg, optimal_chain_info, cfg_change),
      ElementsAre(BbChainIs(
          _, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0), BbIdIs(2)))))));
}

TEST(ApplyCloningsTest, ChangesIntraCfg) {
  std::unique_ptr<ProgramCfg> program_cfg =
      BuildFromCfgArg(GetDefaultProgramCfgArg());
  ProgramPathProfile path_profile(GetDefaultPathProfileArg());
  ASSERT_TRUE(path_profile.path_profiles_by_function_index().contains(6));
  const FunctionPathProfile& function_path_profile =
      path_profile.path_profiles_by_function_index().at(6);
  CfgChangeFromPathCloning cfg_change = {
      .path_pred_bb_index = 1,
      .path_to_clone = {3, 4},
      .paths_to_drop =
          {
              function_path_profile.GetPathTree(3),
              function_path_profile.GetPathTree(3)->GetChild(4),
              function_path_profile.GetPathTree(4),
          },
      .intra_edge_reroutes = {{.src_bb_index = 1,
                               .sink_bb_index = 3,
                               .src_is_cloned = false,
                               .sink_is_cloned = true,
                               .kind = CFGEdgeKind::kBranchOrFallthough,
                               .weight = 20},
                              {.src_bb_index = 3,
                               .sink_bb_index = 4,
                               .src_is_cloned = true,
                               .sink_is_cloned = true,
                               .kind = CFGEdgeKind::kBranchOrFallthough,
                               .weight = 30},
                              {.src_bb_index = 3,
                               .sink_bb_index = 5,
                               .src_is_cloned = true,
                               .sink_is_cloned = false,
                               .kind = CFGEdgeKind::kBranchOrFallthough,
                               .weight = 40},
                              {.src_bb_index = 4,
                               .sink_bb_index = 5,
                               .src_is_cloned = true,
                               .sink_is_cloned = false,
                               .kind = CFGEdgeKind::kBranchOrFallthough,
                               .weight = 50}}};
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs_by_index =
      std::move(*std::move(program_cfg)).release_cfgs_by_index();
  ControlFlowGraph* cfg = cfgs_by_index.at(6).get();
  ASSERT_NE(cfg, nullptr);
  CfgBuilder cfg_builder(cfg);
  cfg_builder.AddCfgChange(cfg_change);
  EXPECT_THAT(
      *std::move(cfg_builder).Build(),
      CfgMatcher(
          CfgNodesMatcher(
              {NodeIntraIdIs(IntraCfgId{0, 0}), NodeIntraIdIs(IntraCfgId{1, 0}),
               NodeIntraIdIs(IntraCfgId{2, 0}), NodeIntraIdIs(IntraCfgId{3, 0}),
               NodeIntraIdIs(IntraCfgId{4, 0}), NodeIntraIdIs(IntraCfgId{5, 0}),
               NodeIntraIdIs(IntraCfgId{3, 1}),
               NodeIntraIdIs(IntraCfgId{4, 1})}),
          CfgIntraEdgesMatcher(
              {IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                         NodeIntraIdIs(IntraCfgId{1, 0}), 181, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{0, 0}),
                         NodeIntraIdIs(IntraCfgId{2, 0}), 660, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                         NodeIntraIdIs(IntraCfgId{3, 0}), 176, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                         NodeIntraIdIs(IntraCfgId{1, 0}), 9, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                         NodeIntraIdIs(IntraCfgId{3, 0}), 656, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{2, 0}),
                         NodeIntraIdIs(IntraCfgId{4, 0}), 10, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                         NodeIntraIdIs(IntraCfgId{4, 0}), 136, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 0}),
                         NodeIntraIdIs(IntraCfgId{5, 0}), 622, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 0}),
                         NodeIntraIdIs(IntraCfgId{5, 0}), 124, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{1, 0}),
                         NodeIntraIdIs(IntraCfgId{3, 1}), 20, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                         NodeIntraIdIs(IntraCfgId{4, 1}), 30, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{3, 1}),
                         NodeIntraIdIs(IntraCfgId{5, 0}), 40, _),
               IsCfgEdge(NodeIntraIdIs(IntraCfgId{4, 1}),
                         NodeIntraIdIs(IntraCfgId{5, 0}), 50, _)})));
}

TEST(EvaluateOneCloning, RejectsNonProfitableCloning) {
  std::unique_ptr<ProgramCfg> program_cfg =
      BuildFromCfgArg(GetDefaultProgramCfgArg());
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs_by_index =
      std::move(*std::move(program_cfg)).release_cfgs_by_index();
  ControlFlowGraph* foo_cfg = cfgs_by_index.at(6).get();
  ASSERT_NE(foo_cfg, nullptr);
  ProgramPathProfile path_profile(GetDefaultPathProfileArg());
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_call_chain_clustering(false);
  PathProfileOptions path_profile_options;

  PathCloning cloning = {
      .path_node = path_profile.path_profiles_by_function_index()
                       .at(6)
                       .path_trees_by_root_bb_index()
                       .at(4)
                       .get(),
      .function_index = 6,
      .path_pred_bb_index = 2};

  EXPECT_THAT(
      EvaluateCloning(CfgBuilder(foo_cfg), cloning, code_layout_params,
                      path_profile_options,
                      /*min_score=*/-170,
                      CodeLayout(code_layout_params, {foo_cfg},
                                 /*initial_chains=*/{})
                          .OrderAll()
                          .front(),
                      path_profile.path_profiles_by_function_index().at(6)),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Cloning is not acceptable with score gain: -190.223 < -170"));
}
}  // namespace
}  // namespace propeller
