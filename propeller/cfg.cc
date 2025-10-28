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

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"
#include "propeller/path_node.h"

namespace propeller {

bool CFGEdge::IsAlwaysTaken() const {
  if (!IsBranchOrFallthrough()) return false;
  if (src_->has_indirect_branch()) return false;
  if (weight_ == 0) return false;
  return absl::c_none_of(src_->intra_outs(), [&](const CFGEdge* edge) {
    return edge->IsBranchOrFallthrough() && edge->weight() != 0 && edge != this;
  });
}

bool CFGEdge::IsIndirectBranch() const {
  return kind_ == CFGEdgeKind::kBranchOrFallthough &&
         src_->has_indirect_branch();
}

CFGEdge* ControlFlowGraph::CreateOrUpdateEdge(CFGNode* from, CFGNode* to,
                                              int weight, CFGEdgeKind kind,
                                              bool inter_section) {
  CFGEdge* edge = from->GetEdgeTo(*to, kind);
  if (edge == nullptr) return CreateEdge(from, to, weight, kind, inter_section);
  edge->IncrementWeight(weight);
  return edge;
}

CFGEdge* ControlFlowGraph::CreateEdge(CFGNode* from, CFGNode* to, int weight,
                                      CFGEdgeKind kind, bool inter_section) {
  if (inter_section)
    CHECK_NE(from->function_index(), to->function_index())
        << " intra-function edges cannot be inter-section.";
  auto edge = std::make_unique<CFGEdge>(from, to, weight, kind, inter_section);
  auto* ret = edge.get();
  auto has_duplicates = [from,
                         to](absl::Span<const std::unique_ptr<CFGEdge>> edges) {
    for (auto& e : edges)
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
    std::ostream& os,
    const absl::flat_hash_map<IntraCfgId, int>& layout_index_map) const {
  os << "digraph {\n";
  os << "label=\"" << GetPrimaryName().str() << "#" << function_index_
     << "\"\n";
  os << "forcelabels=true;\n";
  for (const auto& node : nodes_) {
    os << node->GetDotFormatLabel() << " [xlabel=\"" << node->freq_ << "#"
       << node->size_ << "#" << node->bb_index() << "\", color = \""
       << (node->clone_number() ? "red" : "black") << "\" ];\n";
  }
  for (const auto& edge : intra_edges_) {
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
  for (const std::unique_ptr<CFGNode>& node : nodes_) {
    if (node->is_entry()) continue;
    if (node->CalculateFrequency() < hot_node_frequency_threshold) continue;
    auto num_hot_branches_to =
        std::count_if(node->intra_ins().begin(), node->intra_ins().end(),
                      [&](const CFGEdge* edge) {
                        return edge->src() != edge->sink() && !edge->IsCall() &&
                               !edge->IsReturn() &&
                               edge->weight() >= hot_edge_frequency_threshold;
                      });
    if (num_hot_branches_to <= 1) continue;
    ret.push_back(node->bb_index());
  }
  return ret;
}

std::unique_ptr<ControlFlowGraph> CloneCfg(const ControlFlowGraph& cfg) {
  // Create a clone of `cfg` with all the nodes copied.
  std::vector<std::unique_ptr<CFGNode>> nodes;
  for (const std::unique_ptr<CFGNode>& node : cfg.nodes())
    nodes.push_back(node->Clone(node->clone_number(), nodes.size()));
  auto cfg_clone = std::make_unique<ControlFlowGraph>(
      cfg.section_name(), cfg.function_index(), cfg.module_name(), cfg.names(),
      std::move(nodes));
  // Now copy the intra-function edges.
  for (const std::unique_ptr<CFGEdge>& edge : cfg.intra_edges()) {
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
  for (const auto& node : nodes_) {
    if (node->CalculateFrequency() == 0) continue;
    ++stats.n_hot_blocks;
    if (node->size() == 0) ++stats.n_hot_empty_blocks;
    if (node->is_landing_pad()) ++stats.n_hot_landing_pads;
  }
  return stats;
}

std::ostream& operator<<(std::ostream& out, const CFGEdge& edge) {
  return out << edge.src()->GetName() << " -> " << edge.sink()->GetName()
             << "[ weight: " << edge.weight()
             << "] [type: " << GetCfgEdgeKindString(edge.kind()) << "]";
}

void CfgBuilder::AddCfgChange(const CfgChangeFromPathCloning& cfg_change) {
  for (const CfgChangeFromPathCloning::IntraEdgeReroute& edge_reroute :
       cfg_change.intra_edge_reroutes) {
    // Update the set of affected original edges.
    conflict_edges_.affected_edges.insert(
        {.from_bb_index = edge_reroute.src_bb_index,
         .to_bb_index = edge_reroute.sink_bb_index});
    // If the source is not cloned, it means this is the path predecessor
    // edge. Update the set of path predecessor edges now.
    if (!edge_reroute.src_is_cloned) {
      conflict_edges_.path_pred_edges.insert(
          {.from_bb_index = edge_reroute.src_bb_index,
           .to_bb_index = edge_reroute.sink_bb_index});
    }
  }
  ClonePath(cfg_change.path_pred_bb_index, cfg_change.path_to_clone);
  cfg_changes_.push_back(cfg_change);
}

std::unique_ptr<ControlFlowGraph> CfgBuilder::Build() && {
  std::vector<std::unique_ptr<CFGEdge>> intra_edges;
  intra_edges.reserve(cfg_->intra_edges().size());
  // Now copy the intra-function edges.
  for (const std::unique_ptr<CFGEdge>& edge : cfg_->intra_edges()) {
    CHECK_EQ(edge->src()->function_index(), edge->sink()->function_index())
        << *edge;
    intra_edges.push_back(std::make_unique<CFGEdge>(
        nodes_.at(edge->src()->node_index()).get(),
        nodes_.at(edge->sink()->node_index()).get(), edge->weight(),
        edge->kind(), edge->inter_section()));
  }
  ApplyIntraCfgChanges(intra_edges);
  return std::make_unique<ControlFlowGraph>(
      cfg_->section_name(), cfg_->function_index(), cfg_->module_name(),
      cfg_->names(), std::move(nodes_), std::move(intra_edges),
      std::move(clone_paths_));
}

// Clones the basic blocks along the path `path_to_clone` given path
// predecessor block `path_pred_bb_index`. Both `path_pred_bb_index` and
// `path_to_clone` are specified in terms of bb_indices of the original nodes.
void CfgBuilder::ClonePath(int path_pred_bb_index,
                           absl::Span<const int> path_to_clone) {
  std::vector<int> clone_path;
  clone_path.reserve(path_to_clone.size() + 1);
  clone_path.push_back(path_pred_bb_index);

  for (int bb_index : path_to_clone) {
    // Get the next available clone number for `bb_index`.
    int clone_number = ++current_clone_numbers_[bb_index];
    int new_node_index = nodes_.size();
    // Create and insert the clone node.
    nodes_.push_back(nodes_.at(bb_index)->Clone(clone_number, new_node_index));
    clone_path.push_back(new_node_index);
  }
  // Add this path to `clone_paths_`.
  clone_paths_.push_back(std::move(clone_path));
}

void CfgBuilder::ApplyIntraCfgChanges(
    std::vector<std::unique_ptr<CFGEdge>>& intra_edges) {
  absl::flat_hash_map<int, std::vector<CFGEdge*>>
      original_edges_by_src_bb_index;
  for (const std::unique_ptr<CFGEdge>& edge : intra_edges) {
    if (!edge->src()->is_cloned() && !edge->sink()->is_cloned() &&
        edge->kind() == CFGEdgeKind::kBranchOrFallthough) {
      original_edges_by_src_bb_index[edge->src()->bb_index()].push_back(
          edge.get());
    }
  }
  // Helper for finding the original intra-function edge from `src_bb_index`
  // to `sink_bb_index`.
  auto find_original_edge = [&](int src_bb_index,
                                int sink_bb_index) -> CFGEdge& {
    auto it = original_edges_by_src_bb_index.find(src_bb_index);
    if (it != original_edges_by_src_bb_index.end()) {
      for (CFGEdge* edge : it->second) {
        if (edge->sink()->bb_index() == sink_bb_index) {
          return *edge;
        }
      }
    }
    LOG(FATAL) << "No edge from block with index " << src_bb_index
               << " to block with index" << sink_bb_index << " in function "
               << cfg_->GetPrimaryName().str()
               << " [function index: " << cfg_->function_index() << "]";
  };

  // Path profiles are not continuous (due to the limited LBR stack depth).
  // Therefore, if we have an LBR path that starts from a block in the middle of
  // the cloned path (and doesn't include the path predecessor), we may not be
  // able to confidently determine if the predecessor edge was taken or not, and
  // thus cannot precisely reroute the weights along that path. Therefore, we
  // drop the weights along the edges of those paths as if they were not
  // sampled. Although this loses a small amount of profile information, it
  // warrants that we don't leave any residual edge weights in the edges that
  // are rerouted to the clones, thereby enabling us to determine if a branch
  // will be **never-taken** (which can be used in PropellerCodeLayoutScorer).
  absl::flat_hash_set<const PathNode*> all_paths_to_drop;
  for (const CfgChangeFromPathCloning& cfg_change : cfg_changes_) {
    all_paths_to_drop.insert(cfg_change.paths_to_drop.begin(),
                             cfg_change.paths_to_drop.end());
  }
  for (const PathNode* path_node : all_paths_to_drop) {
    for (const auto& [child_bb_index, child] : path_node->children()) {
      // We don't drop the weights of the path predecessor edges since they will
      // be rerouted to the clones.
      // Note that we don't need to check the affected edges here. Affected
      // edges of other paths cannot overlap these weights, because if they do,
      // their path predecessor would have overlapped with the edges along a
      // path. This cannot happen because we don't clone a path when its path
      // predecessor is in the affected edges of another path.
      if (conflict_edges_.path_pred_edges.contains(
              {.from_bb_index = path_node->node_bb_index(),
               .to_bb_index = child_bb_index})) {
        continue;
      }
      CFGEdge& edge = find_original_edge(path_node->node_bb_index(),
                                         child->node_bb_index());
      edge.DecrementWeight(child->path_pred_info().missing_pred_entry.freq);
    }
  }

  for (int i = 0; i < cfg_changes_.size(); ++i) {
    int clone_path_index = clone_paths_.size() - cfg_changes_.size() + i;
    const CfgChangeFromPathCloning& cfg_change = cfg_changes_[i];
    absl::flat_hash_map<int, CFGNode*> clones;
    for (int j = 0; j < cfg_change.path_to_clone.size(); ++j) {
      int bb_index = cfg_change.path_to_clone[j];
      clones[bb_index] =
          nodes_.at(clone_paths_.at(clone_path_index).at(j + 1)).get();
    }
    // Apply all intra-procedural edge weight reroutes. The inter-procedural
    // edge reroutes will be applied in `CloneApplicator` after all clonings
    // have been applied.
    for (const CfgChangeFromPathCloning::IntraEdgeReroute& edge_reroute :
         cfg_change.intra_edge_reroutes) {
      CFGNode& from_src_node = *nodes_[edge_reroute.src_bb_index];
      CFGNode& from_sink_node = *nodes_[edge_reroute.sink_bb_index];
      if (edge_reroute.kind != CFGEdgeKind::kBranchOrFallthough) continue;
      CFGEdge& edge = find_original_edge(edge_reroute.src_bb_index,
                                         edge_reroute.sink_bb_index);
      // Find and decrement the weight of the original edge.
      edge.DecrementWeight(edge_reroute.weight);
      CFGNode* to_src_node = edge_reroute.src_is_cloned
                                 ? clones[edge_reroute.src_bb_index]
                                 : &from_src_node;
      CFGNode* to_sink_node = edge_reroute.sink_is_cloned
                                  ? clones[edge_reroute.sink_bb_index]
                                  : &from_sink_node;
      // Create the edge to reroute the control flow to.
      intra_edges.push_back(std::make_unique<CFGEdge>(
          to_src_node, to_sink_node, edge_reroute.weight, edge_reroute.kind,
          /*inter_section=*/false));
    }
  }
}
}  // namespace propeller
