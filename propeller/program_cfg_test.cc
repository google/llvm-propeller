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

#include "propeller/program_cfg.h"

#include <memory>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/mock_program_cfg_builder.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

// This test checks that the mock can load a CFG from the serialized format
// correctly.
TEST(ProgramCfg, CreateCfgInfoFromProto) {
  const std::string protobuf_input =
      absl::StrCat(::testing::SrcDir(),
                   "_main/propeller/testdata/propeller_sample.protobuf");

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(protobuf_input));
  EXPECT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(), SizeIs(Gt(10)));

  // Check some inter-func edge is valid.
  const ControlFlowGraph *main =
      proto_program_cfg->program_cfg().GetCfgByIndex(9);
  ASSERT_NE(main, nullptr);
  EXPECT_EQ(main->GetPrimaryName(), "main");
  EXPECT_THAT(main->inter_edges(), Not(IsEmpty()));
  CFGEdge &edge = *(main->inter_edges().front());
  EXPECT_EQ(edge.src()->function_index(), main->function_index());
  EXPECT_NE(edge.sink()->function_index(), main->function_index());
  // The same "edge" instance exists both in src->inter_outs_ and
  // sink->inter_ins_.
  EXPECT_NE(absl::c_find(edge.sink()->inter_ins(), &edge),
            edge.sink()->inter_ins().end());
}

TEST(ProgramCfg, GetNodeFrequencyThreshold) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10}, {0x1010, 1, 0x7}, {0x1020, 2, 0xa}},
                     {{0, 2, 8, CFGEdgeKind::kBranchOrFallthough}}},
                    {".bar_section",
                     1,
                     "bar",
                     {{0x2000, 0, 0x20}, {0x2020, 1, 0x10}},
                     {}},
                    {".baz_section",
                     2,
                     "baz",
                     {{0x3000, 0, 0x10}, {0x3010, 1, 0x5}},
                     {}}},
       .inter_edge_args = {{0, 0, 1, 0, 10, CFGEdgeKind::kCall},
                           {0, 0, 2, 0, 5, CFGEdgeKind::kCall},
                           {0, 1, 1, 0, 2, CFGEdgeKind::kCall}}});
  EXPECT_EQ(program_cfg->GetNodeFrequencyThreshold(100), 12);
  EXPECT_EQ(program_cfg->GetNodeFrequencyThreshold(80), 10);
  EXPECT_EQ(program_cfg->GetNodeFrequencyThreshold(50), 5);
  EXPECT_EQ(program_cfg->GetNodeFrequencyThreshold(10), 0);
  EXPECT_EQ(program_cfg->GetNodeFrequencyThreshold(1), 0);
}

TEST(ProgramCfg, GetHotJoinNodes) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {
           {".foo_section",
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
           {".bar_section",
            1,
            "bar",
            {{0x2000, 0, 0x10}, {0x2010, 1, 0x7}, {0x202a, 2, 0x4}},
            {{0, 2, 50, CFGEdgeKind::kBranchOrFallthough},
             {0, 1, 30, CFGEdgeKind::kBranchOrFallthough},
             {1, 2, 30, CFGEdgeKind::kBranchOrFallthough}}},
       }});
  EXPECT_THAT(program_cfg->GetHotJoinNodes(30, 20),
              UnorderedElementsAre(Pair(1, ElementsAre(2))));
  EXPECT_THAT(
      program_cfg->GetHotJoinNodes(30, 10),
      UnorderedElementsAre(Pair(0, ElementsAre(1)), Pair(1, ElementsAre(2))));
}
}  // namespace
}  // namespace propeller
