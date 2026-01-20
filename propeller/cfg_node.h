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

#ifndef PROPELLER_CFG_NODE_H_
#define PROPELLER_CFG_NODE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"

namespace propeller {

class CFGEdge;
class ControlFlowGraph;

// All instances of CFGNode are owned by their cfg_.
class CFGNode final {
 public:
  CFGNode(uint64_t addr, int bb_index, int bb_id, int size,
          const llvm::object::BBAddrMap::BBEntry::Metadata& metadata,
          uint64_t hash, int function_index, int clone_number = 0,
          int node_index = -1)
      : inter_cfg_id_({function_index, {bb_index, clone_number}}),
        bb_id_(bb_id),
        node_index_(clone_number == 0 ? bb_index : node_index),
        addr_(addr),
        size_(size),
        metadata_(metadata),
        hash_(hash) {}

  // Returns a clone of `*this` with the given assigned `clone_number`, but with
  // zero frequency and empty edges.
  std::unique_ptr<CFGNode> Clone(int clone_number, int node_index) const {
    return std::make_unique<CFGNode>(addr_, bb_index(), bb_id_, size_,
                                     metadata_, hash_, function_index(),
                                     clone_number, node_index);
  }

  // Returns a program-wide unique id for this node.
  const InterCfgId& inter_cfg_id() const { return inter_cfg_id_; }
  // Returns a cfg-wide unique id for this node.
  const IntraCfgId& intra_cfg_id() const { return inter_cfg_id_.intra_cfg_id; }
  FullIntraCfgId full_intra_cfg_id() const {
    return {.bb_id = bb_id_, .intra_cfg_id = intra_cfg_id()};
  }
  uint64_t addr() const { return addr_; }
  int bb_id() const { return bb_id_; }
  int bb_index() const { return intra_cfg_id().bb_index; }
  int node_index() const { return node_index_; }
  int clone_number() const { return intra_cfg_id().clone_number; }
  bool is_cloned() const { return clone_number() != 0; }
  // Computes and returns the execution frequency of the node based on its
  // edges.
  int64_t CalculateFrequency() const;
  int size() const { return size_; }
  uint64_t hash() const { return hash_; }
  bool is_landing_pad() const { return metadata_.IsEHPad; }
  bool can_fallthrough() const { return metadata_.CanFallThrough; }
  bool has_return() const { return metadata_.HasReturn; }
  bool has_tail_call() const { return metadata_.HasTailCall; }
  bool has_indirect_branch() const { return metadata_.HasIndirectBranch; }
  int function_index() const { return inter_cfg_id_.function_index; }

  const std::vector<CFGEdge*>& intra_outs() const { return intra_outs_; }
  const std::vector<CFGEdge*>& intra_ins() const { return intra_ins_; }
  const std::vector<CFGEdge*>& inter_outs() const { return inter_outs_; }
  const std::vector<CFGEdge*>& inter_ins() const { return inter_ins_; }

  bool has_edges() const {
    return !intra_outs_.empty() || !intra_ins_.empty() ||
           !inter_outs_.empty() || !inter_ins_.empty();
  }

  void ForEachInEdgeRef(absl::FunctionRef<void(CFGEdge& edge)> func) const {
    for (CFGEdge* edge : intra_ins_) func(*edge);
    for (CFGEdge* edge : inter_ins_) func(*edge);
  }

  void ForEachOutEdgeRef(absl::FunctionRef<void(CFGEdge& edge)> func) const {
    for (CFGEdge* edge : intra_outs_) func(*edge);
    for (CFGEdge* edge : inter_outs_) func(*edge);
  }

  // Iterates over the out edges in the order of their sink's inter_cfg_id, and
  // applies `func` to each edge. Use this to iterate over the edges in a
  // deterministic order, since the order of the edges in the vectors
  // `intra_outs_`, `intra_ins_`, `inter_outs_` and `inter_ins_` is
  // non-deterministic.
  void ForEachOutEdgeInOrder(absl::FunctionRef<void(CFGEdge& edge)> func) const;

  // Returns if this is the entry of the function.
  bool is_entry() const { return bb_index() == 0; }

  std::string GetName() const;

  // Returns the edge from `*this` to `node` of kind `kind`, or `nullptr` if
  // no such edge exists.
  CFGEdge* GetEdgeTo(const CFGNode& node, CFGEdgeKind kind) const;

  // Returns if there are any edge from `*this` to `node` of kind `kind`.
  bool HasEdgeTo(const CFGNode& node, CFGEdgeKind kind) const {
    return GetEdgeTo(node, kind) != nullptr;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const CFGNode& node);

 private:
  friend class ControlFlowGraph;

  // Returns the profile bb id as a string to be used in the dot format.
  std::string GetDotFormatLabel() const {
    return full_intra_cfg_id().profile_bb_id();
  }

  std::string GetExtendedDotFormatLabel() const {
    return absl::StrJoin({absl::StrCat("id: ", GetDotFormatLabel()),
                          absl::StrCat("index: ", intra_cfg_id().bb_index),
                          absl::StrCat("freq: ", CalculateFrequency()),
                          absl::StrCat("size: ", size_)},
                         "\\n");
  }

  InterCfgId inter_cfg_id_;
  // Fixed ID of the basic block, as defined by the compiler. Must be unique
  // within each cfg. Will be used in the propeller profile.
  const int bb_id_;
  // Index of the node in its CFG's `nodes()`.
  const int node_index_;
  const int addr_;
  int size_ = 0;
  const llvm::object::BBAddrMap::BBEntry::Metadata metadata_;
  uint64_t hash_;

  std::vector<CFGEdge*> intra_outs_ = {};  // Intra function edges.
  std::vector<CFGEdge*> intra_ins_ = {};   // Intra function edges.
  std::vector<CFGEdge*> inter_outs_ = {};  // Calls to other functions.
  std::vector<CFGEdge*> inter_ins_ = {};   // Returns from other functions.
};

template <typename Sink>
inline void AbslStringify(Sink& sink, const CFGNode& node) {
  absl::Format(&sink, "[id: %v, addr:%llu size: %d]", node.inter_cfg_id_,
               node.addr_, node.size_);
}

template <typename Sink>
inline void AbslStringify(Sink& sink, const CFGEdge& edge) {
  absl::Format(&sink, "[%s -> %s, weight(%lld), type(%s), inter-section(%d)]",
               edge.src()->GetName(), edge.sink()->GetName(), edge.weight(),
               GetCfgEdgeKindString(edge.kind()), edge.inter_section());
}

}  // namespace propeller
#endif  // PROPELLER_CFG_NODE_H_
