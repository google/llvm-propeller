#ifndef PROPELLER_PATH_NODE_H_
#define PROPELLER_PATH_NODE_H_

#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "propeller/bb_handle.h"

namespace propeller {
// This struct represents the information for a path node, given a path
// predecessor block. The struct doesn't store the path predecessor block
// or the path node themselves.
struct PathPredInfo {
  // Frequency of the path from root to this path node, given a specific path
  // predecessor block.
  int freq = 0;
  // Instruction cache pressure for cloning this path node along the given
  // path predecessor block.
  double cache_pressure = 0;
  // Frequencies of the calls from this path node, for one path predecessor
  // bock. Maps from the callsite (callee's function index and return block) to
  // its frequency.
  absl::flat_hash_map<propeller::CallRetInfo, int> call_freqs;
  // Frequencies of the returns from this path node, for one path
  // predecessor block. Maps from the `BbHandle` of each block to the
  // frequency of returns into it.
  absl::flat_hash_map<propeller::BbHandle, int> return_to_freqs;

  // Implementation of the `AbslStringify` interface.
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const PathPredInfo &path_pred_info);
};

template <typename Sink>
void AbslStringify(Sink &sink, const PathPredInfo &path_pred_info) {
  absl::Format(&sink, "  frequency: {%d}\n", path_pred_info.freq);
  absl::Format(&sink, "  cache pressure: {%f}\n",
               path_pred_info.cache_pressure);

  if (!path_pred_info.call_freqs.empty()) {
    absl::Format(&sink, "  call frequencies: {%s}\n",
                 absl::StrJoin(path_pred_info.call_freqs, ", ",
                               absl::PairFormatter(":")));
  }
  if (!path_pred_info.return_to_freqs.empty()) {
    absl::Format(&sink, "  return frequencies: {%s}\n",
                 absl::StrJoin(path_pred_info.return_to_freqs, ", ",
                               absl::PairFormatter(":")));
  }
}

struct PathNodeArg {
  int node_bb_index = 0;
  absl::flat_hash_map<int, PathPredInfo> path_pred_info;
  // `PropellerzPathProfileConverter` needs pointer stability. So we use
  // `absl::node_hash_map` instead of `absl::flat_hash_map`.
  absl::node_hash_map<int, PathNodeArg> children_args;
};

// Argument for constructing a path profile for a function. Used by
// `PropellerzPathProfileConverter`.
struct FunctionPathProfileArg {
  int function_index = 0;
  // `PropellerzPathProfileConverter` needs pointer stability. So we use
  // `absl::node_hash_map` instead of `absl::flat_hash_map`.
  absl::node_hash_map<int, PathNodeArg> path_node_args;

  PathNodeArg &GetOrInsertPathTree(int bb_index) {
    return path_node_args
        .try_emplace(bb_index, PathNodeArg{.node_bb_index = bb_index})
        .first->second;
  }
};

struct ProgramPathProfileArg {
  absl::flat_hash_map<int, FunctionPathProfileArg> function_path_profile_args;
  FunctionPathProfileArg &GetProfileForFunctionIndex(int function_index) {
    return function_path_profile_args
        .lazy_emplace(
            function_index,
            [function_index](const auto &ctor) {
              ctor(function_index,
                   FunctionPathProfileArg{.function_index = function_index});
            })
        ->second;
  }
};

// Represents a path node in a path tree.
// Each path tree represents many paths which share their first block. The
// shared block corresponds to the root of this tree. Every path node in the
// tree represents all the program paths which follow the basic block path
// corresponding to the path from the root. These paths may have different
// predecessor blocks (the block executed before their first block). The
// associated path node stores the frequency of the corresponding path given
// every possible path predecessor block (`freqs_by_path_pred`). It also stores
// the frequency of every call from the corresponding ending block, given every
// possible path predecessor block (`callee_freqs_by_path_pred`).
class PathNode {
 public:
  PathNode(int bb_index, const PathNode *parent)
      : node_bb_index_(bb_index),
        parent_(parent),
        path_length_(parent == nullptr ? 2 : parent->path_length() + 1) {}

  // Constructor for creating a path tree from `arg` as a child of `parent`.
  // This will recursively construct the child path nodes and places them in
  // `this->children_`. If `parent` is `nullptr`, this will be the root of the
  // path tree.
  explicit PathNode(
      const PathNodeArg &arg,
      ABSL_ATTRIBUTE_LIFETIME_BOUND absl::Nullable<const PathNode *> parent)
      : node_bb_index_(arg.node_bb_index),
        path_pred_info_(std::move(arg.path_pred_info)),
        parent_(parent),
        path_length_(parent == nullptr ? 2 : parent->path_length() + 1) {
    for (const auto &[child_bb_index, child_arg] : arg.children_args) {
      auto child = std::make_unique<PathNode>(child_arg, this);
      children_.emplace(child->node_bb_index_, std::move(child));
    }
  }

  PathNode(const PathNode &) = delete;
  PathNode &operator=(const PathNode &) = delete;
  PathNode(PathNode &&) = default;
  PathNode &operator=(PathNode &&) = default;

  int node_bb_index() const { return node_bb_index_; }

  int path_length() const { return path_length_; }

  const absl::flat_hash_map<int, PathPredInfo> &path_pred_info() const {
    return path_pred_info_;
  }

  const absl::flat_hash_map<int, std::unique_ptr<PathNode>> &children() const {
    return children_;
  }

  const PathNode *parent() const { return parent_; }
  const PathNode *root() const {
    return parent_ == nullptr ? this : parent_->root();
  }

  absl::flat_hash_map<int, PathPredInfo> &mutable_path_pred_info() {
    return path_pred_info_;
  }

  absl::flat_hash_map<int, std::unique_ptr<PathNode>> &mutable_children() {
    return children_;
  }

  // Returns the path to this path node, from the root of its tree.
  std::vector<const PathNode *> path_from_root() const {
    std::vector<const PathNode *> result;
    for (const PathNode *path_node = this; path_node != nullptr;
         path_node = path_node->parent_) {
      result.push_back(path_node);
    }
    absl::c_reverse(result);
    return result;
  }

  bool operator<(const PathNode &other) const {
    // Order first by `node_bb_index_`, then by `parent_`. Finally, roots are
    // ordered bigger than non-roots.
    if (node_bb_index_ == other.node_bb_index_) {
      if (this->parent_ == nullptr) return false;
      if (other.parent_ == nullptr) return true;
      return *this->parent_ < *other.parent_;
    }
    return node_bb_index_ < other.node_bb_index_;
  }

  int GetTotalChildrenFreqForPathPred(int path_pred_bb_index) const {
    int total_child_freq = 0;
    for (const auto &[child_bb_index, child_bb_path_node] : children()) {
      auto it = child_bb_path_node->path_pred_info_.find(path_pred_bb_index);
      if (it == child_bb_path_node->path_pred_info_.end()) continue;
      total_child_freq += it->second.freq;
    }
    return total_child_freq;
  }

  // Implementation of the `AbslStringify` interface for logging the subtree
  // rooted at a path node. Do not rely on exact format.
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const PathNode &path_node);

 private:
  // Index of the basic block associated with this path node.
  int node_bb_index_;
  // Frequency information for each path predecessor block. Keyed by the bb
  // index of each path predecessor block.
  absl::flat_hash_map<int, PathPredInfo> path_pred_info_;
  // Children of this path node.
  absl::flat_hash_map<int, std::unique_ptr<PathNode>> children_ = {};
  // Parent path node of this tree (`nullptr` for root).
  const PathNode *parent_ = nullptr;
  // Length (number of basic blocks) of the paths represented by this path node
  // (including the path predecessor and the `node_bb_index_` block). This will
  // be `2` if this is the root.
  int path_length_ = 0;
};

template <typename Sink>
void AbslStringify(Sink &sink, const PathNode &path_node) {
  absl::Format(&sink, "\n");
  absl::Format(&sink, "{ path node for block #%d\n  path from root: %s\n",
               path_node.node_bb_index(),
               absl::StrJoin(path_node.path_from_root(), "->",
                             [](std::string *out, const PathNode *path_node) {
                               absl::StrAppend(out, path_node->node_bb_index());
                               if (path_node->children().size() > 1)
                                 absl::StrAppend(out, "*");
                             }));
  absl::Format(&sink, "  path predecessor info: {%s}\n",
               absl::StrJoin(path_node.path_pred_info(), ", ",
                             absl::PairFormatter(":")));
  absl::Format(&sink, "  children: {");
  for (const auto &[child_node_bb_index, child] : path_node.children())
    absl::Format(&sink, "%v", *child);
  absl::Format(&sink, "}}");
}

// This struct represents a unique path cloning decision in the function
// corresponding to `function_index'. It implies cloning the block associated
// with the root of `path_node` along the edge from `path_pred_bb_index` and
// then cloning the path to `path_node` (including `path_node` itself).
struct PathCloning {
  const PathNode *path_node;
  int function_index;
  int path_pred_bb_index;

  bool operator==(const PathCloning &other) const {
    return path_node == other.path_node &&
           function_index == other.function_index &&
           path_pred_bb_index == other.path_pred_bb_index;
  }
  bool operator!=(const PathCloning &other) const { return !(*this == other); }

  bool operator<(const PathCloning &other) const {
    return std::forward_as_tuple(function_index, *path_node,
                                 path_pred_bb_index) <
           std::forward_as_tuple(other.function_index, *other.path_node,
                                 other.path_pred_bb_index);
  }

  template <typename H>
  friend H AbslHashValue(H h, const PathCloning &cloning) {
    return H::combine(std::move(h), cloning.function_index, cloning.path_node,
                      cloning.path_pred_bb_index);
  }

  // Returns the path to `path_node` including `path_pred_bb_index`.
  std::vector<int> GetFullPath() const {
    std::vector<const PathNode *> path_from_root = path_node->path_from_root();
    std::vector<int> result;
    result.reserve(path_from_root.size() + 1);
    result.push_back(path_pred_bb_index);
    absl::c_transform(
        path_from_root, std::back_inserter(result),
        [](const PathNode *path_node) { return path_node->node_bb_index(); });
    return result;
  }

  // Implementation of the `AbslStringify` interface for logging path clonings.
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const PathCloning &path_cloning);
};

template <typename Sink>
void AbslStringify(Sink &sink, const PathCloning &path_cloning) {
  absl::Format(&sink, "[function: %d path: %s]", path_cloning.function_index,
               absl::StrJoin(path_cloning.GetFullPath(), "->"));
}

// Path profile for one function.
class FunctionPathProfile {
 public:
  explicit FunctionPathProfile(int function_index)
      : function_index_(function_index) {}

  explicit FunctionPathProfile(const FunctionPathProfileArg &arg)
      : function_index_(arg.function_index) {
    path_trees_by_root_bb_index_.reserve(arg.path_node_args.size());
    for (const auto &[bb_index, path_node_arg] : arg.path_node_args) {
      path_trees_by_root_bb_index_.emplace(
          path_node_arg.node_bb_index,
          std::make_unique<PathNode>(path_node_arg, /*parent=*/nullptr));
    }
  }

  // `path_trees_by_root_bb_index_` is a map to `std::unique_ptr`s. So
  // `FunctionPathProfile` is a move-only object.
  FunctionPathProfile(const FunctionPathProfile &) = delete;
  FunctionPathProfile &operator=(const FunctionPathProfile &) = delete;
  FunctionPathProfile(FunctionPathProfile &&) = default;
  FunctionPathProfile &operator=(FunctionPathProfile &&) = default;

  int function_index() const { return function_index_; }

  // Returns the path trees keyed by the bb_index of their root.
  const absl::flat_hash_map<int, std::unique_ptr<PathNode>> &
  path_trees_by_root_bb_index() const {
    return path_trees_by_root_bb_index_;
  }

  // Returns the path tree rooted at `bb_index`. Creates a single node path tree
  // if it doesn't exist.
  PathNode &GetOrInsertPathTree(int bb_index) {
    auto [it, inserted] =
        path_trees_by_root_bb_index_.try_emplace(bb_index, nullptr);
    if (inserted)
      it->second = std::make_unique<PathNode>(bb_index, /*parent=*/nullptr);
    return *it->second;
  }

  // Implementation of the `AbslStringify` interface for logging the function
  // path profile. Do not rely on exact format.
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const FunctionPathProfile &profile);

 private:
  // Index of the function.
  int function_index_;
  // Path trees for this function keyed by the bb_index of their root.
  absl::flat_hash_map<int, std::unique_ptr<PathNode>>
      path_trees_by_root_bb_index_;
};

template <typename Sink>
void AbslStringify(Sink &sink, const FunctionPathProfile &profile) {
  absl::Format(&sink, "\n{ function index: %d\n", profile.function_index());
  for (const auto &[root_bb_index, path_tree] :
       profile.path_trees_by_root_bb_index()) {
    absl::Format(&sink, "  path tree for root block #%d: %v\n", root_bb_index,
                 *path_tree);
  }
}

// Path profile for the whole program.
class ProgramPathProfile {
 public:
  ProgramPathProfile() = default;
  explicit ProgramPathProfile(const ProgramPathProfileArg &arg) {
    for (const auto &[function_index, function_arg] :
         arg.function_path_profile_args)
      path_profiles_by_function_index_.try_emplace(function_index,
                                                   function_arg);
  }

  // `ProgramPathProfile` is a move-only object, since `FunctionPathProfile` is
  // move-only.
  ProgramPathProfile(const ProgramPathProfile &) = delete;
  ProgramPathProfile &operator=(const ProgramPathProfile &) = delete;
  ProgramPathProfile(ProgramPathProfile &&) = default;
  ProgramPathProfile &operator=(ProgramPathProfile &&) = default;

  // Returns the function path profiles keyed by their function index.
  const absl::flat_hash_map<int, FunctionPathProfile> &
  path_profiles_by_function_index() const {
    return path_profiles_by_function_index_;
  }

  FunctionPathProfile &GetProfileForFunctionIndex(int function_index) {
    return path_profiles_by_function_index_
        .emplace(function_index, function_index)
        .first->second;
  }

 private:
  // Function path profiles keyed by their function index.
  absl::flat_hash_map<int, FunctionPathProfile>
      path_profiles_by_function_index_;
};
}  // namespace propeller
#endif  // PROPELLER_PATH_NODE_H_
