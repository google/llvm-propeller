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

#include "propeller/multi_cfg_test_case.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/types/span.h"
#include "propeller/bb_handle.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_testutil.h"
#include "propeller/path_node.h"

namespace propeller {
namespace {
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
}  // namespace

ProgramPathProfileArg GetDefaultPathProfileArg() {
  FlatBbHandle bb_7_1 = {.function_index = 7, .flat_bb_index = 1};
  FlatBbHandle bb_10_0 = {.function_index = 10, .flat_bb_index = 0};
  auto children_of_3_args = GetMapByIndex(
      {{.node_bb_index = 4,
        .path_pred_info =
            {.entries =
                 {{1,
                   {.freq = 170,
                    .call_freqs =
                        {{CallRetInfo{.callee = 7, .return_bb = bb_7_1}, 85},
                         {CallRetInfo{.callee = 8, .return_bb = bb_10_0},
                          85}}}},
                  {2,
                   {.freq = 5,
                    .call_freqs =
                        {{CallRetInfo{.callee = 7, .return_bb = bb_7_1}, 5},
                         {CallRetInfo{.callee = 8, .return_bb = bb_10_0},
                          0}}}}},
             .missing_pred_entry =
                 {.freq = 1,
                  .call_freqs =
                      {{CallRetInfo{.callee = 7, .return_bb = bb_7_1}, 1},
                       {CallRetInfo{.callee = 8, .return_bb = bb_10_0}, 1}}}},
        .children_args = GetMapByIndex(
            {{.node_bb_index = 5,
              .path_pred_info =
                  {.entries =
                       {{1,
                         {.freq = 170,
                          .return_to_freqs = {{FlatBbHandle{.function_index = 9,
                                                            .flat_bb_index = 1},
                                               170}}}},
                        {2,
                         {.freq = 5,
                          .return_to_freqs = {{FlatBbHandle{.function_index = 9,
                                                            .flat_bb_index = 1},
                                               5}}}}}}}})},
       {.node_bb_index = 5,
        .path_pred_info = {
            .entries = {{1,
                         {.freq = 13,
                          .return_to_freqs = {{FlatBbHandle{.function_index = 9,
                                                            .flat_bb_index = 1},
                                               13}}}},
                        {2,
                         {.freq = 649,
                          .return_to_freqs = {{FlatBbHandle{.function_index = 9,
                                                            .flat_bb_index = 1},
                                               649}}}}},
            .missing_pred_entry = {
                .freq = 1,
                .return_to_freqs = {
                    {FlatBbHandle{.function_index = 9, .flat_bb_index = 1},
                     1}}}}}});

  auto children_of_4_args =
      GetMapByIndex({{.node_bb_index = 5,
                      .path_pred_info = {.entries = {{2, {.freq = 10}},
                                                     {3, {.freq = 175}}}}}});

  return {
      .function_path_profile_args = GetMapByIndex(
          {{.function_index = 6,
            .path_node_args = GetMapByIndex(
                {{.node_bb_index = 3,
                  .path_pred_info = {.entries = {{1, {.freq = 186}},
                                                 {2, {.freq = 656}}},
                                     .missing_pred_entry = {.freq = 3}},
                  .children_args = children_of_3_args},
                 {.node_bb_index = 4,
                  .path_pred_info =
                      {.entries =
                           {{2,
                             {.freq = 10,
                              .call_freqs =
                                  {{CallRetInfo{.callee = 7, .return_bb = {}},
                                    10},
                                   {CallRetInfo{
                                        .callee =
                                            8,
                                        .return_bb =
                                            bb_10_0},
                                    0}}}},
                            {3,
                             {.freq = 175,
                              .call_freqs =
                                  {{CallRetInfo{
                                        .callee =
                                            7,
                                        .return_bb =
                                            bb_7_1},
                                    90},
                                   {CallRetInfo{
                                        .callee =
                                            8,
                                        .return_bb =
                                            bb_10_0},
                                    85}}}}}},
                  .children_args = children_of_4_args}})}})};
}

MultiCfgArg GetDefaultProgramCfgArg() {
  return {
      .cfg_args =
          {{".text",
            6,
            "foo",
            {{0x1000, 0, 0x10, {.CanFallThrough = true}},
             {0x1010, 1, 0x7, {.CanFallThrough = false}},
             {0x102a, 2, 0x4, {.CanFallThrough = true}},
             {0x1030, 3, 0x8, {.CanFallThrough = true}},
             {0x1038, 4, 0x20, {.CanFallThrough = true}},
             {0x1060, 5, 0x6, {.HasReturn = true, .CanFallThrough = false}}},
            {{0, 1, 181, CFGEdgeKind::kBranchOrFallthough},
             {0, 2, 660, CFGEdgeKind::kBranchOrFallthough},
             {1, 3, 186, CFGEdgeKind::kBranchOrFallthough},
             {2, 3, 656, CFGEdgeKind::kBranchOrFallthough},
             {2, 4, 10, CFGEdgeKind::kBranchOrFallthough},
             {3, 4, 176, CFGEdgeKind::kBranchOrFallthough},
             {3, 5, 663, CFGEdgeKind::kBranchOrFallthough},
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
             {0x3030, 1, 0x13, {.HasReturn = true, .HasTailCall = true}}},
            {{0, 1, 85, CFGEdgeKind::kBranchOrFallthough}}},
           {".text",
            9,
            "qux",
            {{0x4000, 0, 0x40, {.CanFallThrough = true}},
             {0x4040, 1, 0x14, {.HasReturn = true}}},
            {{0, 1, 870, CFGEdgeKind::kBranchOrFallthough}}},
           {".text.", 10, "fred", {{0x5000, 0, 0x50, {.HasReturn = true}}}}},
      .inter_edge_args = {{6, 4, 7, 0, 101, CFGEdgeKind::kCall},
                          {7, 1, 6, 4, 101, CFGEdgeKind::kRet},
                          {6, 4, 8, 0, 86, CFGEdgeKind::kCall},
                          {8, 1, 10, 0, 85, CFGEdgeKind::kCall},
                          {10, 0, 6, 4, 86, CFGEdgeKind::kRet},
                          {9, 1, 6, 0, 874, CFGEdgeKind::kCall},
                          {6, 5, 9, 1, 875, CFGEdgeKind::kRet}}};
}
}  // namespace propeller
