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

#ifndef PROPELLER_PATH_NODE_MATCHERS_H_
#define PROPELLER_PATH_NODE_MATCHERS_H_

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "propeller/bb_handle.h"
#include "propeller/path_node.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
using ::testing::_;
using ::testing::DescribeMatcher;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Pointer;
using ::testing::Property;
using ::testing::UnorderedElementsAre;

MATCHER_P4(
    PathPredInfoEntryIs, frequency_matcher, cache_pressure_matcher,
    call_freqs_matcher, return_to_freqs_matcher,
    absl::StrCat(
        "is a path predecessor info entry that",
        (negation ? " doesn't have" : " has"), " frequency that ",
        DescribeMatcher<int>(frequency_matcher, negation),
        (negation ? " or doesn't have" : " and has"), " cache pressure that ",
        DescribeMatcher<double>(cache_pressure_matcher, negation),
        (negation ? " or doesn't have" : " and has"), " call frequencies that ",
        DescribeMatcher<absl::flat_hash_map<propeller::CallRetInfo, int>>(
            call_freqs_matcher, negation),
        (negation ? " or doesn't have" : " and has"),
        " return to frequencies that ",
        DescribeMatcher<absl::flat_hash_map<propeller::FlatBbHandle, int>>(
            return_to_freqs_matcher, negation))) {
  return ExplainMatchResult(frequency_matcher, arg.freq, result_listener) &&
         ExplainMatchResult(cache_pressure_matcher, arg.cache_pressure,
                            result_listener);
}

MATCHER_P4(
    PathNodeIs, node_bb_index_matcher, path_length_matcher,
    path_pred_info_matcher, children_matcher,
    absl::StrCat(
        "is a path node that", (negation ? " doesn't have" : " has"),
        " bb_index that ",
        DescribeMatcher<int>(node_bb_index_matcher, negation),
        (negation ? " or doesn't have" : " and has"), " path length that ",
        DescribeMatcher<int>(path_length_matcher, negation),
        (negation ? " or doesn't have" : " and has"),
        " path predecessor info that ",
        DescribeMatcher<absl::flat_hash_map<int, propeller::PathPredInfo>>(
            path_pred_info_matcher, negation),
        (negation ? " or doesn't have" : " and has"), " branches that ",
        DescribeMatcher<
            absl::flat_hash_map<int, std::unique_ptr<propeller::PathNode>>>(
            children_matcher, negation),
        (negation ? " or doesn't have" : " and has"),
        " children whose parent points to this node")) {
  return ExplainMatchResult(node_bb_index_matcher, arg->node_bb_index(),
                            result_listener) &&
         ExplainMatchResult(path_length_matcher, arg->path_length(),
                            result_listener) &&
         ExplainMatchResult(path_pred_info_matcher, arg->path_pred_info(),
                            result_listener) &&
         ExplainMatchResult(children_matcher, arg->children(),
                            result_listener) &&
         // Also check that parent pointers of the children point to this
         // PathNode.
         ExplainMatchResult(
             Each(Pair(_, Pointee(Property("parent", &PathNode::parent,
                                           Pointer(Eq(arg.get())))))),
             arg->children(), result_listener);
}

MATCHER_P2(
    FunctionPathProfileIs, function_index_matcher,
    path_trees_by_root_bb_index_matcher,
    absl::StrCat(
        "is a function path profile that",
        (negation ? " doesn't have" : " has"), " function index that ",
        DescribeMatcher<int>(function_index_matcher, negation),
        (negation ? " or doesn't have" : " and has"), " path length that ",
        DescribeMatcher<
            absl::flat_hash_map<int, std::unique_ptr<propeller::PathNode>>>(
            path_trees_by_root_bb_index_matcher, negation))) {
  return ExplainMatchResult(function_index_matcher, arg.function_index(),
                            result_listener) &&
         ExplainMatchResult(path_trees_by_root_bb_index_matcher,
                            arg.path_trees_by_root_bb_index(), result_listener);
}
}  // namespace propeller
#endif  // PROPELLER_PATH_NODE_MATCHERS_H_
