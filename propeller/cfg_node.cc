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

#include "propeller/cfg_node.h"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "propeller/cfg_edge_kind.h"

namespace propeller {

std::string CFGNode::GetName() const {
  std::string bb_name = absl::StrCat(function_index());
  if (!is_entry()) absl::StrAppend(&bb_name, ".", bb_index(), ".id", bb_id());
  if (clone_number() != 0) absl::StrAppend(&bb_name, ".c", clone_number());
  return bb_name;
}

CFGEdge *CFGNode::GetEdgeTo(const CFGNode &node, CFGEdgeKind kind) const {
  for (const std::vector<CFGEdge *> *edges : {&intra_outs_, &inter_outs_}) {
    for (CFGEdge *edge : *edges) {
      if (edge->kind() != kind) continue;
      if (edge->sink() == &node) return edge;
    }
  }
  return nullptr;
}
int CFGNode::CalculateFrequency() const {
  // A node (basic block) may have multiple outgoing calls to different
  // functions. In that case, a single execution of that node counts toward
  // the weight of each of its calls as wells as returns back to the
  // callsites. To avoid double counting, we only consider the heaviest
  // call-out and return-in towards calculating the node's frequency. This
  // mitigates the case discussed in b/155488527 at the expense of possible
  // underestimation. The underestimation may happen when these calls and
  // returns occur in separate LBR stacks. Another source of underestimation
  // is indirect calls. A node may only have one indirect call instruction,
  // but if different functions are called by that indirect call, the node's
  // frequency is equal to the aggregation of call-outs rather than their max.

  int max_call_out = 0;
  int max_ret_in = 0;

  // Total incoming edge frequency to the node's entry (first instruction).
  int sum_in = 0;
  // Total outgoing edge frequency from the node's exit (last instruction).
  int sum_out = 0;

  for (auto *out_edges : {&inter_outs_, &intra_outs_}) {
    for (auto &edge : *out_edges) {
      if (edge->IsCall()) {
        max_call_out = std::max(max_call_out, edge->weight());
      } else {
        sum_out += edge->weight();
      }
    }
  }

  for (auto *in_edges : {&inter_ins_, &intra_ins_}) {
    for (auto &edge : *in_edges) {
      if (edge->IsReturn()) {
        max_ret_in = std::max(max_ret_in, edge->weight());
      } else {
        sum_in += edge->weight();
      }
    }
  }
  return std::max({max_call_out, max_ret_in, sum_out, sum_in});
}
}  // namespace propeller
