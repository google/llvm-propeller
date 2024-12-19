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
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Pointer;
using ::testing::Property;
using ::testing::UnorderedElementsAre;

constexpr double kEpsilon = 0.001;

MATCHER_P4(PathPredInfoIs, freq_matcher, cache_pressure_matcher,
           call_freqs_matcher, return_to_freqs_matcher, "") {
  return ExplainMatchResult(freq_matcher, arg.freq, result_listener) &&
         ExplainMatchResult(cache_pressure_matcher, arg.cache_pressure,
                            result_listener) &&
         ExplainMatchResult(call_freqs_matcher, arg.call_freqs,
                            result_listener) &&
         ExplainMatchResult(return_to_freqs_matcher, arg.return_to_freqs,
                            result_listener);
}

MATCHER_P3(PathNodeIs, bb_id_matcher, path_pred_info_matcher, children_matcher,
           "") {
  return ExplainMatchResult(
             AllOf(Property("node_bb_index", &PathNode::node_bb_index,
                            bb_id_matcher),
                   Property("path_pred_info", &PathNode::path_pred_info,
                            path_pred_info_matcher),
                   Property("children", &PathNode::children, children_matcher)),
             arg, result_listener) &&
         // Also check that parent pointers of the children point to this
         // PathNode.
         ExplainMatchResult(
             Each(Pair(_, Pointee(Property("parent", &PathNode::parent,
                                           Pointer(std::addressof(arg)))))),
             arg.children(), result_listener);
}

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
  MOCK_METHOD(void, HandleReturn, (const BbHandle &), (override));
  MOCK_METHOD(void, ResetPath, (), (override));
};

TEST(ProgramCfgPathAnalyzer, GetsPathsWithHotJoinBbs) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {
           {".anysection",
            0,
            "foo",
            {{0x1000, 0, 0x10},
             {0x1010, 1, 0x7},
             {0x102a, 2, 0x4},
             {0x1030, 3, 0x8}},
            {{0, 1, 10, CFGEdgeKind::kBranchOrFallthough},
             {0, 3, 20, CFGEdgeKind::kBranchOrFallthough},
             {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
             {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}},
           {".anysection",
            1,
            "bar",
            {{0x2000, 0, 0x10}, {0x2010, 1, 0x7}, {0x202a, 2, 0x4}},
            {{0, 2, 50, CFGEdgeKind::kBranchOrFallthough},
             {0, 1, 30, CFGEdgeKind::kBranchOrFallthough},
             {1, 2, 30, CFGEdgeKind::kBranchOrFallthough}}},
       }});

  std::vector<BbHandleBranchPath> paths = {
      {.pid = 2080799,
       .sample_time = absl::FromUnixNanos(1010),
       .branches = {{.to_bb = {{.function_index = 1, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 1, .bb_index = 0}},
                     .to_bb = {{.function_index = 1, .bb_index = 2}}},
                    {.from_bb = {{.function_index = 1, .bb_index = 2}}}}},
      {.pid = 2080799,
       .sample_time = absl::FromUnixNanos(1020),
       .branches = {{.to_bb = {{.function_index = 1, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 1, .bb_index = 2}}}}},
      {.pid = 2080799,
       .sample_time = absl::FromUnixNanos(1020),
       .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 0, .bb_index = 2}},
                     .to_bb = {{.function_index = 0, .bb_index = 2}},
                     .call_rets = {{{.callee = 1}}}},
                    {.from_bb = {{.function_index = 0, .bb_index = 2}},
                     .to_bb = {{.function_index = 0, .bb_index = 1}}},
                    {.from_bb = {{.function_index = 1, .bb_index = 2}}}}},
      {.pid = 2080799,
       .sample_time = absl::FromUnixNanos(1030),
       .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 0, .bb_index = 0}},
                     .to_bb = {{.function_index = 0, .bb_index = 3}}}}}};

  PathProfileOptions options;
  options.set_hot_cutoff_percentile(10);
  ProgramPathProfile program_path_profile;
  EXPECT_THAT(
      ProgramCfgPathAnalyzer(&options, program_cfg.get(), &program_path_profile)
          .GetPathsWithHotJoinBbs(paths),
      ElementsAreArray(
          std::vector<BbHandleBranchPath>(paths.begin(), paths.begin() + 3)));
}

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
  std::vector<BbHandleBranchPath> paths = {
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1010),
       .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 1}},
                     .to_bb = {{.function_index = 5, .bb_index = 3}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1020),
       .branches = {{.from_bb = {{.function_index = 5, .bb_index = 1}},
                     .to_bb = {{.function_index = 5, .bb_index = 3}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1030),
       .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 0}},
                     .to_bb = {{.function_index = 5, .bb_index = 2}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 3}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1040),
       .branches = {{.from_bb = {{.function_index = 5, .bb_index = 0}},
                     .to_bb = {{.function_index = 5, .bb_index = 2}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1050),
       .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 3}}}}}};
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
          ->path_pred_info(),
      UnorderedElementsAre(Pair(0, Field(&PathPredInfo::cache_pressure,
                                         DoubleNear(0.999, kEpsilon))),
                           Pair(1, Field(&PathPredInfo::cache_pressure,
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
          ->path_pred_info(),
      UnorderedElementsAre(Pair(0, Field(&PathPredInfo::cache_pressure,
                                         DoubleNear(1.997, kEpsilon))),
                           Pair(1, Field(&PathPredInfo::cache_pressure,
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
  std::vector<BbHandleBranchPath> paths = {
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1010),
       .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 2}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(910),
       .branches = {{.from_bb = {{.function_index = 5, .bb_index = 2}},
                     .to_bb = {{.function_index = 5, .bb_index = 1}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1900),
       .branches = {{.from_bb = {{.function_index = 5, .bb_index = 2}},
                     .to_bb = {{.function_index = 5, .bb_index = 1}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(1800),
       .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 2}}}}},
      {.pid = 123456,
       .sample_time = absl::FromUnixMillis(2020),
       .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                    {.from_bb = {{.function_index = 5, .bb_index = 2}}}}}};
  PathProfileOptions options;
  options.set_hot_cutoff_percentile(10);
  options.set_max_icache_penalty_interval_millis(2000);
  ProgramPathProfile path_profile;
  ProgramCfgPathAnalyzer path_analyzer(&options, program_cfg.get(),
                                       &path_profile);
  // Store and analyze the paths. This should analyze 2 paths.
  path_analyzer.StoreAndAnalyzePaths(paths);
  EXPECT_THAT(path_analyzer.bb_branch_paths(),
              ElementsAre(Field(&BbHandleBranchPath::sample_time,
                                Eq(absl::FromUnixMillis(1800))),
                          Field(&BbHandleBranchPath::sample_time,
                                Eq(absl::FromUnixMillis(1900))),
                          Field(&BbHandleBranchPath::sample_time,
                                Eq(absl::FromUnixMillis(2020)))));
  // Analyze the remaining paths.
  path_analyzer.AnalyzePaths(/*paths_to_analyze=*/std::nullopt);
  EXPECT_THAT(path_analyzer.bb_branch_paths(), IsEmpty());
  ASSERT_THAT(path_profile.path_profiles_by_function_index(), Contains(Key(5)));
  ASSERT_THAT(path_profile.path_profiles_by_function_index()
                  .at(5)
                  .path_trees_by_root_bb_index(),
              Contains(Key(1)));
  EXPECT_THAT(path_profile.path_profiles_by_function_index()
                  .at(5)
                  .path_trees_by_root_bb_index()
                  .at(1)
                  ->path_pred_info(),
              UnorderedElementsAre(Pair(0, Field(&PathPredInfo::cache_pressure,
                                                 DoubleNear(2.84, kEpsilon))),
                                   Pair(2, Field(&PathPredInfo::cache_pressure,
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

  BbHandleBranchPath path = {
      .pid = 123456,
      .sample_time = absl::FromUnixNanos(1001),
      .branches = {{.to_bb = {{.function_index = 5, .bb_index = 0}}},
                   {.from_bb = {{.function_index = 5, .bb_index = 2}},
                    .to_bb = {{.function_index = 5, .bb_index = 3}},
                    .call_rets = {{{.callee = 17,
                                    .return_bb = {{.function_index = 18,
                                                   .bb_index = 11}}}}}},
                   {.from_bb = {{.function_index = 5, .bb_index = 3}}}},
      .returns_to = {{.function_index = 1, .bb_index = 1}}};

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
    EXPECT_CALL(mock_path_trace_handler,
                HandleCalls(ElementsAre(CallRetInfo{
                    .callee = 17,
                    .return_bb = {{.function_index = 18, .bb_index = 11}}})));
    EXPECT_CALL(mock_path_trace_handler,
                VisitBlock(3, absl::FromUnixNanos(1001)));
    EXPECT_CALL(mock_path_trace_handler,
                HandleReturn(BbHandle{.function_index = 1, .bb_index = 1}));
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

  std::vector<BbHandleBranch> path1_branches = {
      {.to_bb = {{.function_index = 6, .bb_index = 0}}},
      {.from_bb = {{.function_index = 6, .bb_index = 4}},
       .to_bb = {{.function_index = 6, .bb_index = 1}}},
      {.from_bb = {{.function_index = 6, .bb_index = 2}},
       .to_bb = {{.function_index = 6, .bb_index = 5}}},
      {.from_bb = {{.function_index = 6, .bb_index = 5}},
       .to_bb = {{.function_index = 6, .bb_index = 5}},
       .call_rets = {{{.callee = 7}}}}};
  std::vector<BbHandleBranchPath> path1_1s(
      5, BbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(100001),
             .branches = path1_branches,
             .returns_to = {{.function_index = 123, .bb_index = 45}}});
  std::vector<BbHandleBranchPath> path1_2s(
      5, BbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(300001),
             .branches = path1_branches,
             .returns_to = {{.function_index = 123, .bb_index = 45}}});
  std::vector<BbHandleBranch> path2_branches = {
      {.to_bb = {{.function_index = 6, .bb_index = 0}}},
      {.from_bb = {{.function_index = 6, .bb_index = 2}},
       .to_bb = {{.function_index = 6, .bb_index = 5}}},
      {.from_bb = {{.function_index = 6, .bb_index = 5}},
       .to_bb = {{.function_index = 6, .bb_index = 5}},
       .call_rets = {{{.callee = 8}}}},
  };
  std::vector<BbHandleBranchPath> path2_1s(
      5, BbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(200001),
             .branches = path2_branches,
             .returns_to = {{.function_index = 678, .bb_index = 90}}});
  std::vector<BbHandleBranchPath> path2_2s(
      5, BbHandleBranchPath{
             .pid = 9876,
             .sample_time = absl::FromUnixNanos(300001),
             .branches = path2_branches,
             .returns_to = {{.function_index = 678, .bb_index = 90}}});

  std::vector<BbHandleBranchPath> paths;
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

  EXPECT_THAT(
      path_profile.path_profiles_by_function_index(),
      UnorderedElementsAre(Pair(
          6,
          Property(
              "path_trees_by_root_bb_index",
              &FunctionPathProfile::path_trees_by_root_bb_index,
              UnorderedElementsAre(Pair(
                  1,
                  Pointee(PathNodeIs(
                      1,
                      UnorderedElementsAre(
                          Pair(0, PathPredInfoIs(20, DoubleNear(20, kEpsilon),
                                                 IsEmpty(), IsEmpty())),
                          Pair(4, PathPredInfoIs(10, DoubleNear(20, kEpsilon),
                                                 IsEmpty(), IsEmpty()))),
                      UnorderedElementsAre(Pair(
                          2,
                          Pointee(PathNodeIs(
                              2,
                              UnorderedElementsAre(
                                  Pair(0, PathPredInfoIs(
                                              20, DoubleNear(20, kEpsilon),
                                              IsEmpty(), IsEmpty())),
                                  Pair(4, PathPredInfoIs(
                                              10, DoubleNear(20, kEpsilon),
                                              IsEmpty(), IsEmpty()))),
                              UnorderedElementsAre(
                                  Pair(
                                      5,
                                      Pointee(PathNodeIs(
                                          5,
                                          UnorderedElementsAre(
                                              Pair(
                                                  0,
                                                  PathPredInfoIs(
                                                      10,
                                                      DoubleNear(3, kEpsilon),
                                                      UnorderedElementsAre(Pair(
                                                          CallRetInfo{.callee =
                                                                          8},
                                                          10)),
                                                      UnorderedElementsAre(Pair(
                                                          BbHandle{
                                                              .function_index =
                                                                  678,
                                                              .bb_index = 90},
                                                          10)))),
                                              Pair(
                                                  4,
                                                  PathPredInfoIs(
                                                      10,
                                                      DoubleNear(3, kEpsilon),
                                                      UnorderedElementsAre(Pair(
                                                          CallRetInfo{.callee =
                                                                          7},
                                                          10)),
                                                      UnorderedElementsAre(Pair(
                                                          BbHandle{
                                                              .function_index =
                                                                  123,
                                                              .bb_index = 45},
                                                          10))))),
                                          IsEmpty()))),
                                  Pair(
                                      3,
                                      Pointee(PathNodeIs(
                                          3,
                                          UnorderedElementsAre(Pair(
                                              0,
                                              PathPredInfoIs(10, 0, IsEmpty(),
                                                             IsEmpty()))),
                                          UnorderedElementsAre(Pair(
                                              4,
                                              Pointee(PathNodeIs(
                                                  4,
                                                  UnorderedElementsAre(Pair(
                                                      0, PathPredInfoIs(
                                                             10, 0, IsEmpty(),
                                                             IsEmpty()))),
                                                  IsEmpty()))))))))))))))))))));
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

  BbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .bb_index = 0}},
                    .to_bb = {{.function_index = 0, .bb_index = 3}}}}};

  BbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .bb_index = 0}},
                    .to_bb = {{.function_index = 0, .bb_index = 1}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 2}},
                    .to_bb = {{.function_index = 0, .bb_index = 1}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 1}},
                    .to_bb = {{.function_index = 0, .bb_index = 3}}}}};
  std::vector<BbHandleBranchPath> path1s(10, path1);
  std::vector<BbHandleBranchPath> path2s(10, path2);
  std::vector<BbHandleBranchPath> paths;
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
          Property(
              "path_trees_by_root_bb_index",
              &FunctionPathProfile::path_trees_by_root_bb_index,
              Contains(Pair(
                  1,
                  Pointee(PathNodeIs(
                      1,
                      UnorderedElementsAre(
                          Pair(0, PathPredInfoIs(10, _, IsEmpty(), IsEmpty())),
                          Pair(2, PathPredInfoIs(10, _, IsEmpty(), IsEmpty()))),
                      UnorderedElementsAre(
                          Pair(2,
                               Pointee(PathNodeIs(
                                   2,
                                   UnorderedElementsAre(
                                       Pair(0, PathPredInfoIs(10, _, IsEmpty(),
                                                              IsEmpty()))),
                                   IsEmpty()))),
                          Pair(3,
                               Pointee(PathNodeIs(
                                   3,
                                   UnorderedElementsAre(
                                       Pair(2, PathPredInfoIs(10, _, IsEmpty(),
                                                              IsEmpty()))),
                                   IsEmpty()))))))))))));
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

  BbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .bb_index = 0}},
                    .to_bb = {{.function_index = 0, .bb_index = 2}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 2}},
                    .to_bb = {{.function_index = 0, .bb_index = 4}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 5}}}}};

  BbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 2}},
                    .to_bb = {{.function_index = 0, .bb_index = 3}}}}};

  std::vector<BbHandleBranchPath> path1s(5, path1);
  std::vector<BbHandleBranchPath> path2s(10, path2);
  std::vector<BbHandleBranchPath> paths;
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
          Property(
              "path_trees_by_root_bb_index",
              &FunctionPathProfile::path_trees_by_root_bb_index,
              UnorderedElementsAre(Pair(
                  2,
                  Pointee(PathNodeIs(
                      2,
                      UnorderedElementsAre(
                          Pair(0, PathPredInfoIs(5, _, IsEmpty(), IsEmpty())),
                          Pair(1, PathPredInfoIs(10, _, IsEmpty(), IsEmpty()))),
                      UnorderedElementsAre(
                          Pair(4, Pointee(PathNodeIs(4,
                                                     UnorderedElementsAre(Pair(
                                                         0, PathPredInfoIs(
                                                                5, _, IsEmpty(),
                                                                IsEmpty()))),
                                                     IsEmpty()))),
                          Pair(3,
                               Pointee(PathNodeIs(
                                   3,
                                   UnorderedElementsAre(
                                       Pair(1, PathPredInfoIs(10, _, IsEmpty(),
                                                              IsEmpty()))),
                                   IsEmpty()))))))))))));
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

  BbHandleBranchPath path1 = {
      .pid = 2080799,
      .branches = {{.from_bb = {{.function_index = 0, .bb_index = 0}},
                    .to_bb = {{.function_index = 0, .bb_index = 2}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 2}},
                    .to_bb = {{.function_index = 0, .bb_index = 4}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 6}}}}};

  BbHandleBranchPath path2 = {
      .pid = 2080799,
      .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                   {.from_bb = {{.function_index = 0, .bb_index = 3}}}}};

  std::vector<BbHandleBranchPath> path1s(5, path1);
  std::vector<BbHandleBranchPath> path2s(10, path2);
  std::vector<BbHandleBranchPath> paths;
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
}  // namespace
}  // namespace propeller
