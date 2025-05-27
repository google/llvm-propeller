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

#include "propeller/cfg.h"

#include <algorithm>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"

namespace propeller {

CFGEdge *ControlFlowGraph::CreateOrUpdateEdge(CFGNode *from, CFGNode *to,
                                              int weight, CFGEdgeKind kind,
                                              bool inter_section) {
  CFGEdge *edge = from->GetEdgeTo(*to, kind);
  if (edge == nullptr) return CreateEdge(from, to, weight, kind, inter_section);
  edge->IncrementWeight(weight);
  return edge;
}

CFGEdge *ControlFlowGraph::CreateEdge(CFGNode *from, CFGNode *to, int weight,
                                      CFGEdgeKind kind, bool inter_section) {
  if (inter_section)
    CHECK_NE(from->function_index(), to->function_index())
        << " intra-function edges cannot be inter-section.";
  auto edge = std::make_unique<CFGEdge>(from, to, weight, kind, inter_section);
  auto *ret = edge.get();
  auto has_duplicates = [from,
                         to](absl::Span<const std::unique_ptr<CFGEdge>> edges) {
    for (auto &e : edges)
      if (e->src() == from && e->sink() == to) return true;
    return false;
  };
  (void)(has_duplicates);  // For release build warning.
  if (from->function_index() == to->function_index()) {
    CHECK(!has_duplicates(intra_edges_))
        << " " << from->inter_cfg_id() << " to " << to->inter_cfg_id();
    from->intra_outs_.push_back(edge.get());
    to->intra_ins_.push_back(edge.get());
    intra_edges_.push_back(std::move(edge));
  } else {
    DCHECK(!has_duplicates(inter_edges_));
    from->inter_outs_.push_back(edge.get());
    to->inter_ins_.push_back(edge.get());
    inter_edges_.push_back(std::move(edge));
  }
  return ret;
}

void ControlFlowGraph::WriteDotFormat(
    std::ostream &os,
    const absl::flat_hash_map<IntraCfgId, int> &layout_index_map) const {
  os << "digraph {\n";
  os << "label=\"" << GetPrimaryName().str() << "#" << function_index_
     << "\"\n";
  os << "forcelabels=true;\n";
  for (const auto &node : nodes_) {
    os << node->GetDotFormatLabel() << " [xlabel=\"" << node->freq_ << "#"
       << node->size_ << "#" << node->bb_index() << "\", color = \""
       << (node->clone_number() ? "red" : "black") << "\" ];\n";
  }
  for (const auto &edge : intra_edges_) {
    bool is_layout_edge =
        layout_index_map.contains(edge->sink()->intra_cfg_id()) &&
        layout_index_map.contains(edge->src()->intra_cfg_id()) &&
        layout_index_map.at(edge->sink()->intra_cfg_id()) -
                layout_index_map.at(edge->src()->intra_cfg_id()) ==
            1;
    os << edge->src()->GetDotFormatLabel() << " -> "
       << edge->sink()->GetDotFormatLabel() << "[ label=\""
       << edge->GetDotFormatLabel() << "\", color =\""
       << (is_layout_edge ? "red" : "black") << "\"];\n";
  }
  os << "}\n";
}

std::vector<int> ControlFlowGraph::GetHotJoinNodes(
    int hot_node_frequency_threshold, int hot_edge_frequency_threshold) const {
  std::vector<int> ret;
  for (const std::unique_ptr<CFGNode> &node : nodes_) {
    if (node->is_entry()) continue;
    if (node->CalculateFrequency() < hot_node_frequency_threshold) continue;
    auto num_hot_branches_to =
        std::count_if(node->intra_ins().begin(), node->intra_ins().end(),
                      [&](const CFGEdge *edge) {
                        return edge->src() != edge->sink() && !edge->IsCall() &&
                               !edge->IsReturn() &&
                               edge->weight() >= hot_edge_frequency_threshold;
                      });
    if (num_hot_branches_to <= 1) continue;
    ret.push_back(node->bb_index());
  }
  return ret;
}

std::unique_ptr<ControlFlowGraph> CloneCfg(const ControlFlowGraph &cfg) {
  // Create a clone of `cfg` with all the nodes copied.
  std::vector<std::unique_ptr<CFGNode>> nodes;
  for (const std::unique_ptr<CFGNode> &node : cfg.nodes())
    nodes.push_back(node->Clone(node->clone_number(), nodes.size()));
  auto cfg_clone = std::make_unique<ControlFlowGraph>(
      cfg.section_name(), cfg.function_index(), cfg.module_name(), cfg.names(),
      std::move(nodes));
  // Now copy the intra-function edges.
  for (const std::unique_ptr<CFGEdge> &edge : cfg.intra_edges()) {
    CHECK_EQ(edge->src()->function_index(), edge->sink()->function_index());
    cfg_clone->CreateEdge(&cfg_clone->GetNodeById(edge->src()->intra_cfg_id()),
                          &cfg_clone->GetNodeById(edge->sink()->intra_cfg_id()),
                          edge->weight(), edge->kind(), edge->inter_section());
  }
  return cfg_clone;
}

ControlFlowGraph::NodeFrequencyStats ControlFlowGraph::GetNodeFrequencyStats()
    const {
  ControlFlowGraph::NodeFrequencyStats stats;
  for (const auto &node : nodes_) {
    if (node->CalculateFrequency() == 0) continue;
    ++stats.n_hot_blocks;
    if (node->size() == 0) ++stats.n_hot_empty_blocks;
    if (node->is_landing_pad()) ++stats.n_hot_landing_pads;
  }
  return stats;
}

std::ostream &operator<<(std::ostream &out, const CFGEdge &edge) {
  return out << edge.src()->GetName() << " -> " << edge.sink()->GetName()
             << "[ weight: " << edge.weight()
             << "] [type: " << GetCfgEdgeKindString(edge.kind()) << "]";
}
}  // namespace propeller
