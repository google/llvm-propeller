#ifndef PROPELLER_CFG_H_
#define PROPELLER_CFG_H_

#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"

namespace propeller {

// Represents the set of original edges impacted by applied clonings which can
// be used to decide if a new path cloning can be applied. This includes the
// path predecessor edges of all paths cloned so far, along with all the
// original edges whose frequency has been reduced due to the applied clonings.
// A new path cloning conflicts with prior clonings if either its path
// predecessor edge is in `affected_edges` or if it results in reducing the edge
// frequency of any edges in `path_pred_edges`.
// Every edge in `path_pred_edges` should also be in `affected_edges`.
struct ConflictEdges {
  // Structure representing an original (non-cloned) intra-procedural edge in
  // the CFG.
  struct IntraEdge {
    int from_bb_index, to_bb_index;

    bool operator==(const IntraEdge &other) const {
      return from_bb_index == other.from_bb_index &&
             to_bb_index == other.to_bb_index;
    }
    template <typename H>
    friend H AbslHashValue(H h, const IntraEdge &edge) {
      return H::combine(std::move(h), edge.from_bb_index, edge.to_bb_index);
    }
  };
  // All path predecessor edges for the already-applied cloning.
  absl::flat_hash_set<IntraEdge> path_pred_edges;
  // All original intra-function edges which have been modified by the
  // already-applied clonings.
  absl::flat_hash_set<IntraEdge> affected_edges;
};

// Represents a CFG change from applying a single `PathCloning`.
struct CfgChangeFromPathCloning {
  // Represents rerouting the control flow for a single intra-function edge.
  struct IntraEdgeReroute {
    // The edge to reroute the control flow from, specified by the bb indexes
    // of its source and sink.
    int src_bb_index, sink_bb_index;
    // Whether src or sink will be cloned.
    bool src_is_cloned, sink_is_cloned;
    CFGEdgeKind kind;
    int weight;
  };
  // Represents rerouting the control flow for a single inter-function edge.
  struct InterEdgeReroute {
    // The edge to reroute the control flow from, specified by the function and
    // bb indexes of its source and sink.
    int src_function_index, sink_function_index;
    int src_bb_index, sink_bb_index;
    // Whether source or sink will be cloned.
    bool src_is_cloned, sink_is_cloned;
    CFGEdgeKind kind;
    int weight;
  };

  // Predecessor block of the path;
  int path_pred_bb_index;
  // bb_indexes of CFG nodes along the path (excluding the path predecessor).
  std::vector<int> path_to_clone;
  // Intra-function edge weight reroutes.
  std::vector<IntraEdgeReroute> intra_edge_reroutes;
  // Inter-function edge weight reroutes.
  std::vector<InterEdgeReroute> inter_edge_reroutes;
};

class ControlFlowGraph {
 public:
  // hot basic block stats for a single cfg.
  struct NodeFrequencyStats {
    // Number of hot (non-zero frequency) basic blocks.
    int n_hot_blocks = 0;
    // Number of hot landing pad basic blocks.
    int n_hot_landing_pads = 0;
    // Number of hot blocks with zero size.
    int n_hot_empty_blocks = 0;
  };
  ControlFlowGraph(llvm::StringRef section_name, int function_index,
                   std::optional<llvm::StringRef> module_name,
                   const llvm::SmallVectorImpl<llvm::StringRef> &names)
      : section_name_(section_name),
        function_index_(function_index),
        module_name_(module_name),
        names_(names.begin(), names.end()) {}
  ControlFlowGraph(llvm::StringRef section_name, int function_index,
                   std::optional<llvm::StringRef> module_name,
                   llvm::SmallVectorImpl<llvm::StringRef> &&names)
      : section_name_(section_name),
        function_index_(function_index),
        module_name_(module_name),
        names_(std::move(names)) {}
  ControlFlowGraph(llvm::StringRef section_name, int function_index,
                   std::optional<llvm::StringRef> module_name,
                   const llvm::SmallVectorImpl<llvm::StringRef> &names,
                   std::vector<std::unique_ptr<CFGNode>> nodes,
                   std::vector<std::unique_ptr<CFGEdge>> intra_edges = {},
                   std::vector<std::vector<int>> clone_paths = {})
      : section_name_(section_name),
        function_index_(function_index),
        module_name_(module_name),
        names_(names.begin(), names.end()),
        nodes_(std::move(nodes)),
        clone_paths_(std::move(clone_paths)),
        intra_edges_(std::move(intra_edges)) {
    int bb_index = 0;
    for (auto &n : nodes_) {
      CHECK_EQ(n->function_index(), function_index_);
      if (!n->is_cloned()) {
        CHECK_EQ(n->bb_index(), bb_index++);
      } else {
        clones_by_bb_index_[n->bb_index()].push_back(n->node_index());
        CHECK_EQ(n->clone_number(), clones_by_bb_index_[n->bb_index()].size());
      }
      if (n->is_landing_pad()) ++n_landing_pads_;
    }
    for (const auto &e : intra_edges_) {
      e->src()->intra_outs_.push_back(e.get());
      e->sink()->intra_ins_.push_back(e.get());
    }
  }

  ControlFlowGraph(const ControlFlowGraph &) = delete;
  ControlFlowGraph &operator=(const ControlFlowGraph &) = delete;
  ControlFlowGraph(ControlFlowGraph &&) = default;
  ControlFlowGraph &operator=(ControlFlowGraph &&) = default;

  int n_landing_pads() const { return n_landing_pads_; }

  // Returns if this CFG has any hot landing pads. Has a worst-case linear-time
  // complexity w.r.t the number of nodes.
  int has_hot_landing_pads() const {
    if (n_landing_pads_ == 0) return false;
    for (const auto &node : nodes_) {
      if (!node->is_landing_pad()) continue;
      if (node->CalculateFrequency() != 0) return true;
    }
    return false;
  }

  // Returns if this CFG has any edges. Has a worst-case linear time complexity
  // w.r.t the number of nodes.
  bool is_hot() const {
    if (!inter_edges_.empty() || !intra_edges_.empty()) return true;
    return absl::c_any_of(
        nodes_, [](const auto &node) { return !node->inter_ins().empty(); });
  }

  CFGNode *GetEntryNode() const {
    CHECK(!nodes_.empty());
    return nodes_.front().get();
  }

  const std::optional<llvm::StringRef> &module_name() const {
    return module_name_;
  }

  llvm::StringRef GetPrimaryName() const {
    CHECK(!names_.empty());
    return names_.front();
  }

  void ForEachNodeRef(absl::FunctionRef<void(const CFGNode &)> fn) const {
    for (const auto &node : nodes_) fn(*node);
  }

  // Create edge and take ownership. Note: the caller must be responsible for
  // not creating duplicated edges.
  CFGEdge *CreateEdge(CFGNode *from, CFGNode *to, int weight, CFGEdgeKind kind,
                      bool inter_section);

  // If an edge already exists from `from` to `to` of kind `kind`, then
  // increments its edge weight by weight. Otherwise, creates the edge.
  CFGEdge *CreateOrUpdateEdge(CFGNode *from, CFGNode *to, int weight,
                              CFGEdgeKind kind, bool inter_section);

  // Returns the frequencies of nodes in this CFG in a vector, in the same order
  // as in `nodes_`.
  std::vector<int> GetNodeFrequencies() const {
    std::vector<int> node_frequencies;
    node_frequencies.reserve(nodes_.size());
    for (const auto &node : nodes_)
      node_frequencies.push_back(node->CalculateFrequency());
    return node_frequencies;
  }

  llvm::StringRef section_name() const { return section_name_; }

  int function_index() const { return function_index_; }

  CFGNode &GetNodeById(const IntraCfgId &id) const {
    if (id.clone_number == 0) {
      CHECK_LE(id.bb_index, nodes_.size());
      CFGNode *node = nodes_.at(id.bb_index).get();
      CHECK_NE(node, nullptr);
      CHECK_EQ(node->bb_index(), id.bb_index);
      return *node;
    }
    CHECK(clones_by_bb_index_.contains(id.bb_index)) << "For id = " << id;
    CHECK_GT(clones_by_bb_index_.at(id.bb_index).size(), id.clone_number - 1)
        << "For id = " << id;
    return *nodes_.at(
        clones_by_bb_index_.at(id.bb_index).at(id.clone_number - 1));
  }

  const llvm::SmallVector<llvm::StringRef, 3> &names() const { return names_; }
  const std::vector<std::unique_ptr<CFGNode>> &nodes() const { return nodes_; }

  const std::vector<std::unique_ptr<CFGEdge>> &intra_edges() const {
    return intra_edges_;
  }

  const std::vector<std::unique_ptr<CFGEdge>> &inter_edges() const {
    return inter_edges_;
  }

  const absl::flat_hash_map<int, std::vector<int>> &clones_by_bb_index() const {
    return clones_by_bb_index_;
  }

  // Returns a vector of clone nodes (including the original node) for the given
  // `bb_index`, in increasing order of their clone_number.
  std::vector<CFGNode *> GetAllClonesForBbIndex(int bb_index) const {
    CFGNode &original_node =
        GetNodeById(IntraCfgId{.bb_index = bb_index, .clone_number = 0});
    std::vector<CFGNode *> clone_instances(1, &original_node);
    auto it = clones_by_bb_index_.find(bb_index);
    if (it != clones_by_bb_index_.end()) {
      absl::c_transform(
          it->second, std::back_inserter(clone_instances),
          [&](int node_index) { return &*nodes_.at(node_index); });
    }
    return clone_instances;
  }

  // Returns the cloned paths in this CFG. Each path is represented as a vector
  // of indices in `nodes_` corresponding to the original nodes.
  const std::vector<std::vector<int>> &clone_paths() const {
    return clone_paths_;
  }

  // Adds a path to cloned paths. `clone_path` is represented as a vector of
  // indices in `nodes_` corresponding to the original nodes.
  void AddClonePath(std::vector<int> clone_path) {
    clone_paths_.push_back(std::move(clone_path));
  }

  // Clones basic blocks along the path `path_to_clone` given path predecessor
  // block `path_pred_bb_index`. Both `path_pred_bb_index` and `path_to_clone`
  // are specified in terms of bb_indices of the original nodes.
  void ClonePath(int path_pred_bb_index, absl::Span<const int> path_to_clone) {
    std::vector<int> clone_path;
    clone_path.reserve(path_to_clone.size() + 1);
    clone_path.push_back(path_pred_bb_index);

    for (int bb_index : path_to_clone) {
      // Get the next available clone number for `bb_index`.
      auto &clones = clones_by_bb_index_[bb_index];
      // Create and insert the clone node.
      nodes_.emplace_back(nodes_.at(bb_index)->Clone(
          clones.size() + 1, static_cast<int>(nodes_.size())));
      clones.push_back(nodes_.size() - 1);
      clone_path.push_back(nodes_.size() - 1);
      if (nodes_.back()->is_landing_pad()) ++n_landing_pads_;
    }
    // Add this path to `clone_paths_`.
    clone_paths_.push_back(std::move(clone_path));
  }

  // Writes the dot format of CFG into the given stream. `layout_index_map`
  // specifies a layout by mapping basic block intra_cfg_id to their positions
  // in the layout. Fall-through edges will be colored differently
  // (red) in the dot format. `layout_indexx_map` can be a partial map.
  void WriteDotFormat(
      std::ostream &os,
      const absl::flat_hash_map<IntraCfgId, int> &layout_index_map) const;

  // Returns the bb_indexes of hot join nodes in this CFG. These are nodes which
  // have a frequency of at least `hot_node_frequency_threshold` and at least
  // two incoming intra-function edges at least as heavy as
  // hot_edge_frequency_threshold`.
  std::vector<int> GetHotJoinNodes(int hot_node_frequency_threshold,
                                   int hot_edge_frequency_threshold) const;

  NodeFrequencyStats GetNodeFrequencyStats() const;

 private:
  // The output section name for this function within which it can be reordered.
  llvm::StringRef section_name_;

  // Unique index of the function in the SHT_LLVM_BB_ADDR_MAP section.
  int function_index_;

  std::optional<llvm::StringRef> module_name_;

  // Function names associated with this CFG: The first name is the primary
  // function name and the rest are aliases. The primary name is necessary.
  llvm::SmallVector<llvm::StringRef, 3> names_;

  // CFGs own all nodes. Nodes here are *strictly* sorted by addresses /
  // ordinals.
  std::vector<std::unique_ptr<CFGNode>> nodes_;

  // Number of nodes which are exception handling pads.
  int n_landing_pads_ = 0;

  // Indices of cloned CFG nodes mapped by bb_indexes of the original nodes.
  // `clone_number` of each node in this map must be equal to 1 + its index in
  // its vector.
  absl::flat_hash_map<int, std::vector<int>> clones_by_bb_index_;

  // Cloned paths starting with their path predecessor block. Each path is
  // represented as a vector of indices in `nodes_`.
  std::vector<std::vector<int>> clone_paths_;

  // CFGs own all edges. All edges are owned by their src's CFGs and they
  // appear exactly once in one of the following two fields. The src and sink
  // nodes of each edge contain a pointer to the edge, which means, each edge is
  // recorded exactly twice in Nodes' inter_ins_, inter_outs, intra_ints or
  // intra_out_.
  std::vector<std::unique_ptr<CFGEdge>> intra_edges_;
  std::vector<std::unique_ptr<CFGEdge>> inter_edges_;
};

std::ostream &operator<<(std::ostream &os, const CFGEdgeKind &kind);

// Returns a clone of `cfg` with its nodes and intra-function edges cloned and
// its inter-function edges dropped.
std::unique_ptr<ControlFlowGraph> CloneCfg(const ControlFlowGraph &cfg);

// Class for cloning a CFG from another CFG and then applying path clonings.
// This class should be used as:
//
// CfgBuilder cfg_builder(cfg);
// cfg_builder.RecordCfgChange(cfg_change);
// std::unique_ptr<ControlFlowGraph> clone_cfg = std::move(cfg_builder).Build();
//
// The CFG edges are only constructed at `Build()` and after all nodes are
// created.
class CfgBuilder {
 public:
  explicit CfgBuilder(
      ABSL_ATTRIBUTE_LIFETIME_BOUND absl::Nonnull<const ControlFlowGraph *> cfg)
      : cfg_(cfg), clone_paths_(cfg->clone_paths()) {
    for (const auto &node : cfg_->nodes()) {
      nodes_.push_back(node->Clone(node->clone_number(), nodes_.size()));
    }
    for (const auto &[bb_index, clones] : cfg_->clones_by_bb_index())
      current_clone_numbers_[bb_index] = clones.size();
  }

  CfgBuilder(const CfgBuilder &) = delete;
  CfgBuilder &operator=(const CfgBuilder &) = delete;
  CfgBuilder(CfgBuilder &&) = default;
  CfgBuilder &operator=(CfgBuilder &&) = default;

  // Returns a clone of `*this` with its `nodes_` cloned and `cfg_changes_`
  // added.
  CfgBuilder Clone() const {
    CfgBuilder cfg_builder(cfg_);
    for (const CfgChangeFromPathCloning &cfg_change : cfg_changes_) {
      cfg_builder.AddCfgChange(cfg_change);
    }
    return cfg_builder;
  }

  // Adds the path cloning `cfg_change` to `cfg_changes_` and clondes the nodes
  // in the path accordingly. Also updates `conflict_edges_`.
  void AddCfgChange(const CfgChangeFromPathCloning &cfg_change) {
    for (const CfgChangeFromPathCloning::IntraEdgeReroute &edge_reroute :
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

  int GetNodeSize(int bb_index) const { return nodes_.at(bb_index)->size(); }

  // Builds the `ControlFlowGraph` by cloning the intra-function edges from
  // the original cfg and then applying the path cloning changes in
  // `cfg_changes_`.
  std::unique_ptr<ControlFlowGraph> Build() && {
    std::vector<std::unique_ptr<CFGEdge>> intra_edges;
    // Now copy the intra-function edges.
    for (const std::unique_ptr<CFGEdge> &edge : cfg_->intra_edges()) {
      CHECK_EQ(edge->src()->function_index(), edge->sink()->function_index());
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

  absl::Span<const CfgChangeFromPathCloning> cfg_changes() const {
    return cfg_changes_;
  }
  const ConflictEdges &conflict_edges() const { return conflict_edges_; }
  const ControlFlowGraph &cfg() const { return *cfg_; }

 private:
  // Applies the intra-function changes from `cfg_changes_` to `intra_edges`.
  void ApplyIntraCfgChanges(
      std::vector<std::unique_ptr<CFGEdge>> &intra_edges) {
    absl::flat_hash_map<int, std::vector<CFGEdge *>>
        original_edges_by_src_bb_index;
    for (const std::unique_ptr<CFGEdge> &edge : intra_edges) {
      if (!edge->src()->is_cloned() && !edge->sink()->is_cloned() &&
          edge->kind() == CFGEdgeKind::kBranchOrFallthough) {
        original_edges_by_src_bb_index[edge->src()->bb_index()].push_back(
            edge.get());
      }
    }
    // Helper for finding the original intra-function edge from `src_bb_index`
    // to `sink_bb_index`.
    auto find_original_edge = [&](int src_bb_index,
                                  int sink_bb_index) -> CFGEdge * {
      auto it = original_edges_by_src_bb_index.find(src_bb_index);
      if (it == original_edges_by_src_bb_index.end()) {
        return nullptr;
      }
      for (CFGEdge *edge : it->second) {
        if (edge->sink()->bb_index() == sink_bb_index) {
          return edge;
        }
      }
      return nullptr;
    };

    for (int i = 0; i < cfg_changes_.size(); ++i) {
      int clone_path_index = clone_paths_.size() - cfg_changes_.size() + i;
      const CfgChangeFromPathCloning &cfg_change = cfg_changes_[i];
      absl::flat_hash_map<int, CFGNode *> clones;
      for (int j = 0; j < cfg_change.path_to_clone.size(); ++j) {
        int bb_index = cfg_change.path_to_clone[j];
        clones[bb_index] =
            nodes_.at(clone_paths_.at(clone_path_index).at(j + 1)).get();
      }
      // Apply all intra-procedural edge weight reroutes.
      for (const CfgChangeFromPathCloning::IntraEdgeReroute &edge_reroute :
           cfg_change.intra_edge_reroutes) {
        CFGNode &from_src_node = *nodes_[edge_reroute.src_bb_index];
        CFGNode &from_sink_node = *nodes_[edge_reroute.sink_bb_index];
        if (edge_reroute.kind != CFGEdgeKind::kBranchOrFallthough) continue;
        CFGEdge *edge = find_original_edge(edge_reroute.src_bb_index,
                                           edge_reroute.sink_bb_index);
        CHECK_NE(edge, nullptr)
            << "No edge from block " << edge_reroute.src_bb_index << " to block"
            << edge_reroute.sink_bb_index << " in function "
            << cfg_->GetPrimaryName().str()
            << " [index: " << cfg_->function_index() << "]";
        // Find and decrement the weight of the original edge.
        edge->DecrementWeight(edge_reroute.weight);
        CFGNode *to_src_node = edge_reroute.src_is_cloned
                                   ? clones[edge_reroute.src_bb_index]
                                   : &from_src_node;
        CFGNode *to_sink_node = edge_reroute.sink_is_cloned
                                    ? clones[edge_reroute.sink_bb_index]
                                    : &from_sink_node;
        // Create the edge to reroute the control flow to.
        intra_edges.push_back(std::make_unique<CFGEdge>(
            to_src_node, to_sink_node, edge_reroute.weight, edge_reroute.kind,
            /*inter_section=*/false));
      }
    }
  }

  // Clones the basic blocks along the path `path_to_clone` given path
  // predecessor block `path_pred_bb_index`. Both `path_pred_bb_index` and
  // `path_to_clone` are specified in terms of bb_indices of the original nodes.
  void ClonePath(int path_pred_bb_index, absl::Span<const int> path_to_clone) {
    std::vector<int> clone_path;
    clone_path.reserve(path_to_clone.size() + 1);
    clone_path.push_back(path_pred_bb_index);

    for (int bb_index : path_to_clone) {
      // Get the next available clone number for `bb_index`.
      int clone_number = ++current_clone_numbers_[bb_index];
      // Create and insert the clone node.
      nodes_.emplace_back(nodes_.at(bb_index)->Clone(
          clone_number, static_cast<int>(nodes_.size())));
      clone_path.push_back(nodes_.size() - 1);
    }
    // Add this path to `clone_paths_`.
    clone_paths_.push_back(std::move(clone_path));
  }

  const ControlFlowGraph *cfg_;
  std::vector<std::unique_ptr<CFGNode>> nodes_;
  std::vector<std::vector<int>> clone_paths_;
  absl::flat_hash_map<int, int> current_clone_numbers_;
  std::vector<CfgChangeFromPathCloning> cfg_changes_;
  ConflictEdges conflict_edges_;
};
}  // namespace propeller
#endif  // PROPELLER_CFG_H_
