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

#include "propeller/program_cfg_path_analyzer.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/mock_program_cfg_builder.h"
#include "propeller/path_node.h"
#include "propeller/path_node_matchers.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::testing::_;
using ::testing::Contains;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

constexpr double kEpsilon = 0.001;

// Returns the max depth in the path tree rooted at `path_node`, with the root
// having a depth of 1.
int GetMaxDepthForPathTree(const PathNode &path_node) {
  int depth = 0;
  for (const auto &[bb_index, child_path_node] : path_node.children()) {
    depth = std::max(depth, GetMaxDepthForPathTree(*child_path_node));
  }
  return 1 + depth;
}

class MockPathTraceHandler : public PathTraceHandler {
 public:
  MOCK_METHOD(void, VisitBlock, (int bb_index, absl::Time sample_time),
              (override));
  MOCK_METHOD(void, HandleCalls, (absl::Span<const CallRetInfo>), (override));
  MOCK_METHOD(void, HandleReturn, (const FlatBbHandle &), (override));
  MOCK_METHOD(void, ResetPath, (), (override));
};

TEST(PathTracer, TracePathUpdatesCachePressureWithShortPaths) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".anysection",
                     5,
                     "foo",
                     {{0x1000, 0, 0x10, {.CanFallThrough = true}},
                      {0x1010, 1, 0x7, {.CanFallThrough = true}},
                      {0x102a, 2, 0x4, {.CanFallThrough = true}},
                      {0x1030, 3, 0x8, {.CanFallThrough = true}}},
                     {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
                      {0, 2, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 3, 10, CFGEdgeKind::kBranchOrFallthough}}}}});
  std::vector<FlatBbHandleBranchPath> paths = {
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1010),
       .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 1}},
                     .to_bb = {{.function_index = 5, .flat_bb_index = 3}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1020),
       .branches = {{.from_bb = {{.function_index = 5, .flat_bb_index = 1}},
                     .to_bb = {{.function_index = 5, .flat_bb_index = 3}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1030),
       .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 0}},
                     .to_bb = {{.function_index = 5, .flat_bb_index = 2}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 3}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1040),
       .branches = {{.from_bb = {{.function_index = 5, .flat_bb_index = 0}},
                     .to_bb = {{.function_index = 5, .flat_bb_index = 2}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1050),
       .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 3}}}}}};
  PathProfileOptions options;
  options.set_hot_cutoff_percentile(10);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  path_analyzer.StoreAndAnalyzePaths(paths);
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  ASSERT_THAT(path_profile.path_profiles_by_function_index(), Contains(Key(5)));
  ASSERT_THAT(path_profile.path_profiles_by_function_index()
                  .at(5)
                  .path_trees_by_root_bb_index(),
              Contains(Key(2)));
  EXPECT_THAT(
      path_profile.path_profiles_by_function_index()
          .at(5)
          .path_trees_by_root_bb_index()
          .at(2)
          ->path_pred_info()
          .entries,
      UnorderedElementsAre(Pair(0, Field(&PathPredInfoEntry::cache_pressure,
                                         DoubleNear(0.999, kEpsilon))),
                           Pair(1, Field(&PathPredInfoEntry::cache_pressure,
                                         DoubleNear(0.999, kEpsilon)))));
  ASSERT_THAT(path_profile.path_profiles_by_function_index()
                  .at(5)
                  .path_trees_by_root_bb_index()
                  .at(2)
                  ->children(),
              Contains(Key(3)));
  EXPECT_THAT(
      path_profile.path_profiles_by_function_index()
          .at(5)
          .path_trees_by_root_bb_index()
          .at(2)
          ->children()
          .at(3)
          ->path_pred_info()
          .entries,
      UnorderedElementsAre(Pair(0, Field(&PathPredInfoEntry::cache_pressure,
                                         DoubleNear(1.997, kEpsilon))),
                           Pair(1, Field(&PathPredInfoEntry::cache_pressure,
                                         DoubleNear(0.998, kEpsilon)))));
}

TEST(PathTracer, TracePathUpdatesCachePressureWithUnsortedSampleTimes) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {
           {".anysection",
            5,
            "foo",
            {{0x1000, 0, 0x10, {.CanFallThrough = true}},
             {0x1010, 1, 0x7, {.CanFallThrough = true}},
             {0x102a, 2, 0x4, {.CanFallThrough = true}},
             {0x1030, 3, 0x8, {.HasReturn = true, .CanFallThrough = false}}},
            {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
             {0, 3, 20, CFGEdgeKind::kBranchOrFallthough},
             {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
             {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  // Branch paths with unsorted sample times.
  std::vector<FlatBbHandleBranchPath> paths = {
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1010),
       .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 2}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(910),
       .branches = {{.from_bb = {{.function_index = 5, .flat_bb_index = 2}},
                     .to_bb = {{.function_index = 5, .flat_bb_index = 1}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1900),
       .branches = {{.from_bb = {{.function_index = 5, .flat_bb_index = 2}},
                     .to_bb = {{.function_index = 5, .flat_bb_index = 1}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1800),
       .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 2}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(2020),
       .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .flat_bb_index = 2}}}}}};
  PathProfileOptions options;
  options.set_hot_cutoff_percentile(10);
  options.set_max_icache_penalty_interval_millis(2000);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  // Store and analyze the paths. This should analyze 2 paths.
  path_analyzer.StoreAndAnalyzePaths(paths);
  EXPECT_THAT(path_analyzer.bb_branch_paths(),
              ElementsAre(Field(&FlatBbHandleBranchPath::sample_time,
                                Eq(absl::FromUnixMillis(1800))),
                          Field(&FlatBbHandleBranchPath::sample_time,
                                Eq(absl::FromUnixMillis(1900))),
                          Field(&FlatBbHandleBranchPath::sample_time,
                                Eq(absl::FromUnixMillis(2020)))));
  // Analyze the remaining paths.
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  EXPECT_THAT(path_analyzer.bb_branch_paths(), IsEmpty());
  ASSERT_THAT(path_profile.path_profiles_by_function_index(), Contains(Key(5)));
  ASSERT_THAT(path_profile.path_profiles_by_function_index()
                  .at(5)
                  .path_trees_by_root_bb_index(),
              Contains(Key(1)));
  EXPECT_THAT(
      path_profile.path_profiles_by_function_index()
          .at(5)
          .path_trees_by_root_bb_index()
          .at(1)
          ->path_pred_info()
          .entries,
      UnorderedElementsAre(Pair(0, Field(&PathPredInfoEntry::cache_pressure,
                                         DoubleNear(2.84, kEpsilon))),
                           Pair(2, Field(&PathPredInfoEntry::cache_pressure,
                                         DoubleNear(2.84, kEpsilon)))));
}

TEST(PathTracer, TracePathVisitsBlocksAndCalls) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {
           {".anysection",
            5,
            "foo",
            {{0x1000, 0, 0x10, {.CanFallThrough = true}},
             {0x1010, 1, 0x7, {.CanFallThrough = true}},
             {0x102a, 2, 0x4, {.CanFallThrough = true}},
             {0x1030, 3, 0x8, {.HasReturn = true, .CanFallThrough = false}}},
            {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
             {0, 3, 20, CFGEdgeKind::kBranchOrFallthough},
             {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
             {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});

  FlatBbHandleBranchPath path = {
      .pid = 123456,
      .sample_time = absl::FromUnixNanos(1001),
      .branches = {{.to_bb = {{.function_index = 5, .flat_bb_index = 0}}},
                   {.from_bb = {{.function_index = 5, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 5, .flat_bb_index = 3}},
                    .call_rets = {{{.callee = std::nullopt,
                                    .return_bb = std::nullopt},
                                   {.callee = 17,
                                    .return_bb = {{.function_index = 18,
                                                   .flat_bb_index = 11}}}}}},
                   {.from_bb = {{.function_index = 5, .flat_bb_index = 3}}}},
      .returns_to = {{.function_index = 1, .flat_bb_index = 1}}};

  MockPathTraceHandler mock_path_trace_handler;
  EXPECT_CALL(mock_path_trace_handler, ResetPath()).Times(0);
  {
    InSequence s;
    EXPECT_CALL(mock_path_trace_handler,
                VisitBlock(0, absl::FromUnixNanos(1001)));
    EXPECT_CALL(mock_path_trace_handler,
                VisitBlock(1, absl::FromUnixNanos(1001)));
    EXPECT_CALL(mock_path_trace_handler,
                VisitBlock(2, absl::FromUnixNanos(1001)));
    EXPECT_CALL(
        mock_path_trace_handler,
        HandleCalls(ElementsAre(
            CallRetInfo{.callee = std::nullopt, .return_bb = std::nullopt},
            CallRetInfo{
                .callee = 17,
                .return_bb = {{.function_index = 18, .flat_bb_index = 11}}})));
    EXPECT_CALL(mock_path_trace_handler,
                VisitBlock(3, absl::FromUnixNanos(1001)));
    EXPECT_CALL(
        mock_path_trace_handler,
        HandleReturn(FlatBbHandle{.function_index = 1, .flat_bb_index = 1}));
  }

  PathTracer(program_cfg->GetCfgByIndex(/*index=*/5), &mock_path_trace_handler)
      .TracePath(path);
}

TEST(ProgramCfgPathAnalyzer, BuildPathTree) {
  // A realistic example from go/propeller-path-cloning. Includes a function
  // with a loop, where the header is the hot join block. Also, the return block
  // calls either of two functions depending on the path.
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args =
           {{".anysection",
             6,
             "foo",
             {{0x1000, 0, 0x10, {.CanFallThrough = true}},
              {0x1010, 1, 0x7, {.CanFallThrough = true}},
              {0x102a, 2, 0x4, {.CanFallThrough = true}},
              {0x1030, 3, 0x8, {.CanFallThrough = true}},
              {0x1038, 4, 0x20, {.CanFallThrough = true}},
              {0x1060, 5, 0x6, {.HasReturn = true, .CanFallThrough = false}}},
             {{0, 1, 1699, CFGEdgeKind::kBranchOrFallthough},
              {1, 2, 2585, CFGEdgeKind::kBranchOrFallthough},
              {2, 5, 1782, CFGEdgeKind::kBranchOrFallthough},
              {2, 3, 834, CFGEdgeKind::kBranchOrFallthough},
              {3, 4, 834, CFGEdgeKind::kBranchOrFallthough},
              {4, 1, 887, CFGEdgeKind::kBranchOrFallthough}}},
            {".anysection",
             7,
             "bar",
             {{0x2000, 0, 0x50, {.HasReturn = true, .CanFallThrough = false}}},
             {}},
            {".anysection",
             8,
             "baz",
             {{0x3000, 0, 0x50, {.HasReturn = true, .CanFallThrough = false}}},
             {}}},
       .inter_edge_args = {{6, 5, 7, 0, 841, CFGEdgeKind::kCall},
                           {6, 5, 8, 0, 951, CFGEdgeKind::kCall},
                           {7, 0, 6, 5, 839, CFGEdgeKind::kRet},
                           {8, 0, 6, 5, 952, CFGEdgeKind::kRet}}});

  std::vector<FlatBbHandleBranch> path1_branches = {
      {.to_bb = {{.function_index = 6, .flat_bb_index = 0}}},
      {.from_bb = {{.function_index = 6, .flat_bb_index = 4}},
       .to_bb = {{.function_index = 6, .flat_bb_index = 1}}},
      {.from_bb = {{.function_index = 6, .flat_bb_index = 2}},
       .to_bb = {{.function_index = 6, .flat_bb_index = 5}}},
      {.from_bb = {{.function_index = 6, .flat_bb_index = 5}},
       .to_bb = {{.function_index = 6, .flat_bb_index = 5}},
       .call_rets = {{{.callee = 7}}}}};
  std::vector<FlatBbHandleBranchPath> path1_1s(
      5, FlatBbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(100001),
             .branches = path1_branches,
             .returns_to = {{.function_index = 123, .flat_bb_index = 45}}});
  std::vector<FlatBbHandleBranchPath> path1_2s(
      5, FlatBbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(300001),
             .branches = path1_branches,
             .returns_to = {{.function_index = 123, .flat_bb_index = 45}}});
  std::vector<FlatBbHandleBranch> path2_branches = {
      {.to_bb = {{.function_index = 6, .flat_bb_index = 0}}},
      {.from_bb = {{.function_index = 6, .flat_bb_index = 2}},
       .to_bb = {{.function_index = 6, .flat_bb_index = 5}}},
      {.from_bb = {{.function_index = 6, .flat_bb_index = 5}},
       .to_bb = {{.function_index = 6, .flat_bb_index = 5}},
       .call_rets = {{{.callee = 8}}}},
  };
  std::vector<FlatBbHandleBranchPath> path2_1s(
      5, FlatBbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(200001),
             .branches = path2_branches,
             .returns_to = {{.function_index = 678, .flat_bb_index = 90}}});
  std::vector<FlatBbHandleBranchPath> path2_2s(
      5, FlatBbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(300001),
             .branches = path2_branches,
             .returns_to = {{.function_index = 678, .flat_bb_index = 90}}});

  std::vector<FlatBbHandleBranchPath> paths;
  absl::c_copy(path1_1s, std::back_inserter(paths));
  absl::c_copy(path2_1s, std::back_inserter(paths));
  absl::c_copy(path1_2s, std::back_inserter(paths));
  absl::c_copy(path2_2s, std::back_inserter(paths));

  PathProfileOptions options;
  options.set_hot_cutoff_percentile(30);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  path_analyzer.StoreAndAnalyzePaths(paths);
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  ASSERT_THAT(path_profile.path_profiles_by_function_index(),
              UnorderedElementsAre(Key(6)));

  EXPECT_THAT(
      path_profile.path_profiles_by_function_index().at(6),
      FunctionPathProfileIs(
          6,
          UnorderedElementsAre(Pair(
              1,
              PathNodeIs(
                  1, 2,
                  PathPredInfoIs(UnorderedElementsAre(
                                     Pair(0, PathPredInfoEntryIs(
                                                 20, DoubleNear(20, kEpsilon),
                                                 IsEmpty(), IsEmpty())),
                                     Pair(4, PathPredInfoEntryIs(
                                                 10, DoubleNear(20, kEpsilon),
                                                 IsEmpty(), IsEmpty()))),
                                 _),
                  UnorderedElementsAre(Pair(
                      2,
                      PathNodeIs(
                          2, 3,
                          PathPredInfoIs(
                              UnorderedElementsAre(
                                  Pair(0, PathPredInfoEntryIs(
                                              20, DoubleNear(20, kEpsilon),
                                              IsEmpty(), IsEmpty())),
                                  Pair(4, PathPredInfoEntryIs(
                                              10, DoubleNear(20, kEpsilon),
                                              IsEmpty(), IsEmpty()))),
                              _),
                          UnorderedElementsAre(
                              Pair(
                                  5,
                                  PathNodeIs(
                                      5, 4,
                                      PathPredInfoIs(
                                          UnorderedElementsAre(
                                              Pair(
                                                  0,
                                                  PathPredInfoEntryIs(
                                                      10,
                                                      DoubleNear(3, kEpsilon),
                                                      UnorderedElementsAre(Pair(
                                                          CallRetInfo{.callee =
                                                                          8},
                                                          10)),
                                                      UnorderedElementsAre(Pair(
                                                          FlatBbHandle{
                                                              .function_index =
                                                                  678,
                                                              .flat_bb_index =
                                                                  90},
                                                          10)))),
                                              Pair(
                                                  4,
                                                  PathPredInfoEntryIs(
                                                      10,
                                                      DoubleNear(3, kEpsilon),
                                                      UnorderedElementsAre(Pair(
                                                          CallRetInfo{.callee =
                                                                          7},
                                                          10)),
                                                      UnorderedElementsAre(Pair(
                                                          FlatBbHandle{
                                                              .function_index =
                                                                  123,
                                                              .flat_bb_index =
                                                                  45},
                                                          10))))),
                                          _),
                                      IsEmpty())),
                              Pair(3,
                                   PathNodeIs(
                                       3, 4,
                                       PathPredInfoIs(
                                           UnorderedElementsAre(
                                               Pair(0, PathPredInfoEntryIs(
                                                           10, 0, IsEmpty(),
                                                           IsEmpty()))),
                                           _),
                                       UnorderedElementsAre(Pair(
                                           4,
                                           PathNodeIs(
                                               4, 5,
                                               PathPredInfoIs(
                                                   UnorderedElementsAre(Pair(
                                                       0, PathPredInfoEntryIs(
                                                              10, 0, IsEmpty(),
                                                              IsEmpty()))),
                                                   _),
                                               IsEmpty()))))))))))))));
}

TEST(ProgramCfgPathAnalyzer, HandlesPathPredecessorWithIndirectBranch) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".anysection",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10, {.HasIndirectBranch = true}},
                      {0x1010, 1, 0x9, {.CanFallThrough = true}},
                      {0x1020, 2, 0x8, {.CanFallThrough = true}},
                      {0x1030, 3, 0x7, {.HasReturn = true}}},
                     {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 3, 10, CFGEdgeKind::kBranchOrFallthough}}}}});

  FlatBbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 0}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 3}}}}};

  FlatBbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 0}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 1}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 1}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 1}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 3}}}}};
  std::vector<FlatBbHandleBranchPath> path1s(10, path1);
  std::vector<FlatBbHandleBranchPath> path2s(10, path2);
  std::vector<FlatBbHandleBranchPath> paths;
  absl::c_copy(path1s, std::back_inserter(paths));
  absl::c_copy(path2s, std::back_inserter(paths));

  PathProfileOptions options;
  options.set_hot_cutoff_percentile(30);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  path_analyzer.StoreAndAnalyzePaths(paths);
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);

  EXPECT_THAT(
      path_profile.path_profiles_by_function_index(),
      UnorderedElementsAre(Pair(
          0,
          FunctionPathProfileIs(
              0,
              Contains(Pair(
                  1, PathNodeIs(
                         1, 2,
                         PathPredInfoIs(
                             UnorderedElementsAre(
                                 Pair(0, PathPredInfoEntryIs(10, _, IsEmpty(),
                                                             IsEmpty())),
                                 Pair(2, PathPredInfoEntryIs(10, _, IsEmpty(),
                                                             IsEmpty()))),
                             _),
                         UnorderedElementsAre(
                             Pair(2, PathNodeIs(2, 3,
                                                PathPredInfoIs(
                                                    UnorderedElementsAre(Pair(
                                                        0, PathPredInfoEntryIs(
                                                               10, _, IsEmpty(),
                                                               IsEmpty()))),
                                                    _),
                                                IsEmpty())),
                             Pair(3, PathNodeIs(3, 3,
                                                PathPredInfoIs(
                                                    UnorderedElementsAre(Pair(
                                                        2, PathPredInfoEntryIs(
                                                               10, _, IsEmpty(),
                                                               IsEmpty()))),
                                                    _),
                                                IsEmpty()))))))))));
}

TEST(ProgramCfgPathAnalyzer, AnalyzesPathEndingWithIndirectBranch) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".anysection",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10, {.CanFallThrough = true}},
                      {0x1010, 1, 0x9, {.CanFallThrough = true}},
                      {0x1020, 2, 0x8, {.HasIndirectBranch = true}},
                      {0x1030, 3, 0x7, {.HasReturn = true}},
                      {0x1040, 4, 0x6, {.CanFallThrough = true}},
                      {0x1050, 5, 0x5, {.HasReturn = true}}},
                     {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
                      {0, 2, 5, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 4, 5, CFGEdgeKind::kBranchOrFallthough},
                      {4, 5, 5, CFGEdgeKind::kBranchOrFallthough}}}}});

  FlatBbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 0}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 2}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 4}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 5}}}}};

  FlatBbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.to_bb = {{.function_index = 0, .flat_bb_index = 0}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 3}}}}};

  std::vector<FlatBbHandleBranchPath> path1s(5, path1);
  std::vector<FlatBbHandleBranchPath> path2s(10, path2);
  std::vector<FlatBbHandleBranchPath> paths;
  absl::c_copy(path1s, std::back_inserter(paths));
  absl::c_copy(path2s, std::back_inserter(paths));

  PathProfileOptions options;
  options.set_hot_cutoff_percentile(30);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  path_analyzer.StoreAndAnalyzePaths(paths);
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);

  // Check that path_profile includes a path tree rooted at 2, with only one
  // level below 2, since 2 has an indirect branch.
  EXPECT_THAT(
      path_profile.path_profiles_by_function_index(),
      UnorderedElementsAre(Pair(
          0,
          FunctionPathProfileIs(
              0,
              UnorderedElementsAre(Pair(
                  2, PathNodeIs(
                         2, 2,
                         PathPredInfoIs(
                             UnorderedElementsAre(
                                 Pair(0, PathPredInfoEntryIs(5, _, IsEmpty(),
                                                             IsEmpty())),
                                 Pair(1, PathPredInfoEntryIs(10, _, IsEmpty(),
                                                             IsEmpty()))),
                             _),
                         UnorderedElementsAre(
                             Pair(4, PathNodeIs(4, 3,
                                                PathPredInfoIs(
                                                    UnorderedElementsAre(Pair(
                                                        0, PathPredInfoEntryIs(
                                                               5, _, IsEmpty(),
                                                               IsEmpty()))),
                                                    _),
                                                IsEmpty())),
                             Pair(3, PathNodeIs(3, 3,
                                                PathPredInfoIs(
                                                    UnorderedElementsAre(Pair(
                                                        1, PathPredInfoEntryIs(
                                                               10, _, IsEmpty(),
                                                               IsEmpty()))),
                                                    _),
                                                IsEmpty()))))))))));
}

using TreePathLengthTest = testing::TestWithParam<int>;

TEST_P(TreePathLengthTest, LimitsPathLength) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".text",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10, {.CanFallThrough = true}},
                      {0x1010, 1, 0x9, {.CanFallThrough = true}},
                      {0x1020, 2, 0x8, {.CanFallThrough = true}},
                      {0x1030, 3, 0x7, {.HasReturn = true}},
                      {0x1040, 4, 0x6, {.CanFallThrough = true}},
                      {0x1050, 5, 0x5, {.CanFallThrough = true}},
                      {0x1060, 6, 0x6, {.HasReturn = true}}},
                     {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
                      {0, 2, 5, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 4, 5, CFGEdgeKind::kBranchOrFallthough},
                      {4, 5, 5, CFGEdgeKind::kBranchOrFallthough},
                      {5, 6, 5, CFGEdgeKind::kBranchOrFallthough}}}}});

  FlatBbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 0}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 2}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 4}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 6}}}}};

  FlatBbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.to_bb = {{.function_index = 0, .flat_bb_index = 0}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 3}}}}};

  std::vector<FlatBbHandleBranchPath> path1s(5, path1);
  std::vector<FlatBbHandleBranchPath> path2s(10, path2);
  std::vector<FlatBbHandleBranchPath> paths;
  absl::c_copy(path1s, std::back_inserter(paths));
  absl::c_copy(path2s, std::back_inserter(paths));

  PathProfileOptions options;
  options.set_hot_cutoff_percentile(30);
  options.set_max_path_length(GetParam());
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  path_analyzer.StoreAndAnalyzePaths(paths);
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  ASSERT_THAT(path_profile.path_profiles_by_function_index(), Contains(Key(0)));
  ASSERT_THAT(path_profile.path_profiles_by_function_index()
                  .at(0)
                  .path_trees_by_root_bb_index(),
              Contains(Key(2)));
  EXPECT_EQ(
      GetMaxDepthForPathTree(*path_profile.path_profiles_by_function_index()
                                  .at(0)
                                  .path_trees_by_root_bb_index()
                                  .at(2)),
      GetParam());
}

INSTANTIATE_TEST_SUITE_P(ProgramCfgPathAnalyzer, TreePathLengthTest,
                         testing::Values(2, 3, 4),
                         [](const testing::TestParamInfo<int> &param_info) {
                           return absl::StrCat("MaxPathLength",
                                               param_info.param);
                         });

TEST(ProgramCfgPathAnalyzer, TracksMissingPathPredecessorInfo) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".text",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10, {.CanFallThrough = true}},
                      {0x1010, 1, 0x9, {.CanFallThrough = true}},
                      {0x1020, 2, 0x8, {.CanFallThrough = true}},
                      {0x1030, 3, 0x7, {}},
                      {0x1040, 4, 0x6, {.CanFallThrough = true}},
                      {0x1050, 5, 0x5, {.CanFallThrough = true}},
                      {0x1060, 6, 0x6, {.HasReturn = true}}},
                     {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
                      {0, 2, 5, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 10, CFGEdgeKind::kBranchOrFallthough},
                      {2, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {3, 6, 12, CFGEdgeKind::kBranchOrFallthough},
                      {2, 4, 6, CFGEdgeKind::kBranchOrFallthough},
                      {4, 5, 6, CFGEdgeKind::kBranchOrFallthough},
                      {5, 6, 6, CFGEdgeKind::kBranchOrFallthough}}}}});
  FlatBbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 0}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 2}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 4}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 6}}}},
      .returns_to = {{.function_index = 2, .flat_bb_index = 98}}};

  FlatBbHandleBranchPath path1_missing_pred = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 2}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 4}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 6}}}},
      .returns_to = {{.function_index = 2, .flat_bb_index = 98}}};

  FlatBbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.to_bb = {{.function_index = 0, .flat_bb_index = 0}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 3}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 3}},
                    .call_rets = {{.callee = 1,
                                   .return_bb = {{.function_index = 1,
                                                  .flat_bb_index = 87}}}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 3}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 6}}}},
      .returns_to = {{.function_index = 2, .flat_bb_index = 98}}};
  FlatBbHandleBranchPath path2_missing_pred = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .flat_bb_index = 3}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 3}},
                    .call_rets = {{.callee = 1,
                                   .return_bb = {{.function_index = 1,
                                                  .flat_bb_index = 87}}}}},
                   {.from_bb = {{.function_index = 0, .flat_bb_index = 3}},
                    .to_bb = {{.function_index = 0, .flat_bb_index = 6}}}},
      .returns_to = {{.function_index = 2, .flat_bb_index = 98}}};

  std::vector<FlatBbHandleBranchPath> path1s(5, path1);
  std::vector<FlatBbHandleBranchPath> path1_missing_preds(1,
                                                          path1_missing_pred);
  std::vector<FlatBbHandleBranchPath> path2s(10, path2);
  std::vector<FlatBbHandleBranchPath> path2_missing_preds(2,
                                                          path2_missing_pred);
  std::vector<FlatBbHandleBranchPath> paths;
  for (const auto &path :
       {path1s, path1_missing_preds, path2s, path2_missing_preds}) {
    absl::c_copy(path, std::back_inserter(paths));
  }

  PathProfileOptions options;
  options.set_hot_cutoff_percentile(30);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  path_analyzer.StoreAndAnalyzePaths(paths);
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  auto path_node_is_5_matcher = PathNodeIs(
      5, 4,
      PathPredInfoIs(UnorderedElementsAre(Pair(
                         0, PathPredInfoEntryIs(5, _, IsEmpty(), IsEmpty()))),
                     PathPredInfoEntryIs(1, 0, IsEmpty(), IsEmpty())),
      UnorderedElementsAre(Pair(
          6, PathNodeIs(
                 6, 5,
                 PathPredInfoIs(
                     UnorderedElementsAre(Pair(
                         0, PathPredInfoEntryIs(
                                5, _, IsEmpty(),
                                UnorderedElementsAre(Pair(
                                    FlatBbHandle{.function_index = 2,
                                                 .flat_bb_index = 98},
                                    5))))),
                     PathPredInfoEntryIs(1, 0, IsEmpty(),
                                         UnorderedElementsAre(Pair(
                                             FlatBbHandle{.function_index = 2,
                                                          .flat_bb_index = 98},
                                             1)))),
                 IsEmpty()))));
  auto path_node_is_3_matcher = PathNodeIs(
      3, 3,
      PathPredInfoIs(
          UnorderedElementsAre(Pair(
              1, PathPredInfoEntryIs(
                     10, _,
                     UnorderedElementsAre(Pair(
                         CallRetInfo{
                             .callee = 1,
                             .return_bb = std::optional<FlatBbHandle>(
                                 {.function_index = 1, .flat_bb_index = 87})},
                         10)),
                     IsEmpty()))),
          PathPredInfoEntryIsEmpty()),
      UnorderedElementsAre(Pair(
          6, PathNodeIs(6, 4,
                        PathPredInfoIs(
                            UnorderedElementsAre(Pair(
                                1, PathPredInfoEntryIs(
                                       10, _, IsEmpty(),
                                       UnorderedElementsAre(Pair(
                                           FlatBbHandle{.function_index = 2,
                                                        .flat_bb_index = 98},
                                           10))))),
                            PathPredInfoEntryIsEmpty()),
                        IsEmpty()))));
  EXPECT_THAT(
      path_profile.path_profiles_by_function_index(),
      UnorderedElementsAre(Pair(
          0,
          FunctionPathProfileIs(
              0,
              AllOf(
                  Contains(Pair(
                      3, PathNodeIs(
                             3, 2,
                             PathPredInfoIs(
                                 IsEmpty(),
                                 PathPredInfoEntryIs(
                                     2, 0,
                                     UnorderedElementsAre(Pair(
                                         CallRetInfo{
                                             .callee = 1,
                                             .return_bb =
                                                 std::optional<FlatBbHandle>(
                                                     {.function_index = 1,
                                                      .flat_bb_index = 87})},
                                         2)),
                                     IsEmpty())),
                             UnorderedElementsAre(Pair(
                                 6, PathNodeIs(
                                        6, 3,
                                        PathPredInfoIs(
                                            IsEmpty(),
                                            PathPredInfoEntryIs(
                                                2, 0, IsEmpty(),
                                                UnorderedElementsAre(Pair(
                                                    FlatBbHandle{
                                                        .function_index = 2,
                                                        .flat_bb_index = 98},
                                                    2)))),
                                        IsEmpty())))))),

                  Contains(Pair(
                      2, PathNodeIs(
                             2, 2,
                             PathPredInfoIs(
                                 UnorderedElementsAre(
                                     Pair(0, PathPredInfoEntryIs(
                                                 5, _, IsEmpty(), IsEmpty())),
                                     Pair(1, PathPredInfoEntryIs(
                                                 10, _, IsEmpty(), IsEmpty()))),
                                 PathPredInfoEntryIs(1, 0, IsEmpty(),
                                                     IsEmpty())),
                             UnorderedElementsAre(
                                 Pair(4,
                                      PathNodeIs(
                                          4, 3,
                                          PathPredInfoIs(
                                              UnorderedElementsAre(
                                                  Pair(0, PathPredInfoEntryIs(
                                                              5, _, IsEmpty(),
                                                              IsEmpty()))),
                                              PathPredInfoEntryIs(
                                                  1, 0, IsEmpty(), IsEmpty())),
                                          UnorderedElementsAre(Pair(
                                              5, path_node_is_5_matcher)))),
                                 Pair(3, path_node_is_3_matcher))))))))));
}
}  // namespace
}  // namespace propeller
