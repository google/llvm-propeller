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

#ifndef PROPELLER_BINARY_ADDRESS_MAPPER_H_
#define PROPELLER_BINARY_ADDRESS_MAPPER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_content.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

enum class BranchDirection { kFrom, kTo };

// Represents either a branch from `from_bb` to `to_bb`, or a callsite calling
// one or more functions from `from_bb` and returning back to `to_bb`.
// For instance, in the example code below, we can have BbHandleBranch instances
// `BbhandleBranch{.from_bb = foo.if, .to_bb = foo.call}`
// and `BbHandleBranch{.from_bb = foo.call, .to_bb=foo.other,
// .call_rets: {{bar, bar.ret}}}`
//
// void foo {
//   if (condition) // foo.if
//     bar(); // foo.call
//   // do other work (foo.other)
// }
// void bar {
// do work
// return; // bar.ret
// }
struct FlatBbHandleBranch {
  // `from_bb` or `to_bb` can be null when they point to unknown code (code
  // blocks not mapped by the underlying `BinaryAddressMapper`.
  std::optional<FlatBbHandle> from_bb = std::nullopt;
  int from_bb_flat_index = -1;
  std::optional<FlatBbHandle> to_bb = std::nullopt;
  int to_bb_flat_index = -1;
  // All callee functions called from `from_bb` and returning to `to_bb` in the
  // order in which they are called. Callees are represented by their function
  // index, or `std::nullopt` if the function is unknown.
  std::vector<CallRetInfo> call_rets = {};

  bool operator==(const FlatBbHandleBranch &other) const {
    return from_bb == other.from_bb && to_bb == other.to_bb &&
           call_rets == other.call_rets;
  }

  bool operator!=(const FlatBbHandleBranch &other) const {
    return !(*this == other);
  }

  bool is_callsite() const { return !call_rets.empty(); }

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const FlatBbHandleBranch &branch) {
    absl::Format(&sink, "%v -> %v", branch.from_bb, branch.to_bb);
    if (!branch.is_callsite()) return;
    absl::Format(&sink, "(CALLSITES: %s)",
                 absl::StrJoin(branch.call_rets, ","));
  }
};

struct FlatBbHandleBranchPath {
  int64_t pid;
  absl::Time sample_time;
  std::vector<FlatBbHandleBranch> branches;
  // The block that this path returns to after the last branch.
  std::optional<FlatBbHandle> returns_to;

  bool operator==(const FlatBbHandleBranchPath &other) const {
    return pid == other.pid && branches == other.branches &&
           returns_to == other.returns_to;
  }

  bool operator!=(const FlatBbHandleBranchPath &other) const {
    return !(*this == other);
  }

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const FlatBbHandleBranchPath &path) {
    absl::Format(
        &sink, "FlatBbHandleBranchPath[pid:%lld, sample_time:%v, branches:%s",
        path.pid, path.sample_time, absl::StrJoin(path.branches, ", "));
    if (path.returns_to.has_value()) {
      absl::Format(&sink, ", returns_to:%v", *path.returns_to);
    }
    absl::Format(&sink, "]");
  }
};

// Finds basic block entries from binary addresses.
class BinaryAddressMapper {
 public:
  BinaryAddressMapper(
      absl::btree_set<int> selected_functions,
      std::vector<llvm::object::BBAddrMap> bb_addr_map,
      std::vector<BbHandle> bb_handles,
      absl::flat_hash_map<int, FunctionSymbolInfo> symbol_info_map);

  BinaryAddressMapper(const BinaryAddressMapper &) = delete;
  BinaryAddressMapper &operator=(const BinaryAddressMapper &) = delete;
  BinaryAddressMapper(BinaryAddressMapper &&) = default;
  BinaryAddressMapper &operator=(BinaryAddressMapper &&) = default;

  const std::vector<llvm::object::BBAddrMap> &bb_addr_map() const {
    return bb_addr_map_;
  }

  const absl::flat_hash_map<int, FunctionSymbolInfo> &symbol_info_map() const {
    return symbol_info_map_;
  }

  const std::vector<BbHandle> &bb_handles() const { return bb_handles_; }

  const absl::btree_set<int> &selected_functions() const {
    return selected_functions_;
  }

  // Returns the `bb_handles_` index associated with the binary address
  // `address` given a branch from/to this address based on `direction`.
  // It returns nullopt if the no `bb_handles_` index can be mapped.
  // When zero-sized blocks exist, multiple blocks could be mapped to the
  // address. We make this decision based the given branch `direction` for the
  // address. For example, consider the following range of blocks from two
  // functions foo and bar.
  // ...
  // 0x10:  <foo.5> [size: 0x6]
  // 0x16:  <foo.6> [size: 0x4]
  // 0x1a:  <foo.7> [size: 0x0]
  // 0x1a:  <foo.8> [size: 0x0]
  // 0x1a:  <foo.9> [size: 0x6]
  // 0x20:  <foo.10> [size: 0x0]
  // 0x20:  <bar.0> [size: 0x10]
  // ...
  // 1- address=0x12, direction=kFrom/kTo -> returns foo.5
  //    This is the simple case where address falls within the block.
  // 2- address=0x16, direction=kFrom/kTo -> returns <foo.6>
  //    Address falls at the beginning of <foo.6> and there are no empty blocks
  //    at the same address.
  // 3- address=0x1a, direction=kTo -> returns <foo.7>
  //    <foo.7>, <foo.8>, and <foo.9> all start at this address. We return the
  //    first empty block, which falls through to the rest. In this case <foo.7>
  // 4- address=0x1a, direction=kFrom -> returns <foo.9>.
  //    We cannot have a branch "from" an empty block. So we return the single
  //    non-empty block at this address.
  // 5- address=0x20, direction=kTo/kFrom -> returns <bar.0>
  //    Even though <foo.10> is an empty block at the same address as <bar.0>,
  //    it won't be considered because it's from a different function.
  std::optional<int> FindBbHandleIndexUsingBinaryAddress(
      uint64_t address, BranchDirection direction) const;

  // Returns the `bb_handles_` element associated with the binary address
  // `address` given a branch from/to this address based on `direction`. It
  // returns nullopt if the no `bb_handles_` element can be mapped.
  std::optional<BbHandle> GetBbHandleUsingBinaryAddress(
      uint64_t address, BranchDirection direction) const;

  // Returns whether in basic block with `from` can fall through to basic block
  // `to`.
  bool CanFallThrough(const BbHandle &from, const BbHandle &to) const;

  // Returns the full function's BB address map associated with the given
  // `bb_handle`.
  const llvm::object::BBAddrMap &GetFunctionEntry(
      const BbHandle &bb_handle) const {
    return bb_addr_map_.at(bb_handle.function_index);
  }

  const llvm::object::BBAddrMap::BBRangeEntry &GetBBRangeEntry(
      const BbHandle &bb_handle) const {
    return bb_addr_map_.at(bb_handle.function_index)
        .getBBRanges()[bb_handle.range_index];
  }

  // Returns the BbHandle associated with the basic block with flat BB handle
  // `flat_bb_handle`. Returns nullopt if no such BB exists.
  std::optional<BbHandle> GetBbHandle(const FlatBbHandle &flat_bb_handle) const;

  // Returns the flat BB handle of BB associated with `bb_handle` in its
  // function, if all BB ranges were flattened. Returns nullopt if no BB with
  // `bb_handle` exists.
  std::optional<FlatBbHandle> GetFlatBbHandle(const BbHandle &bb_handle) const;

  std::optional<FlatBbHandle> GetFlatBbHandle(
      const std::optional<BbHandle> &bb_handle) const {
    if (!bb_handle.has_value()) return std::nullopt;
    return GetFlatBbHandle(*bb_handle);
  }

  // Returns the basic block's address map entry associated with the given
  // `bb_handle`.
  const llvm::object::BBAddrMap::BBEntry &GetBBEntry(BbHandle bb_handle) const {
    return GetFunctionEntry(bb_handle)
        .getBBRanges()
        .at(bb_handle.range_index)
        .BBEntries.at(bb_handle.bb_index);
  }

  uint64_t GetAddress(BbHandle bb_handle) const {
    return GetBBRangeEntry(bb_handle).BaseAddress +
           GetBBEntry(bb_handle).Offset;
  }

  uint64_t GetEndAddress(BbHandle bb_handle) const {
    return GetAddress(bb_handle) + GetBBEntry(bb_handle).Size;
  }

  // Returns the name associated with the given `bb_handle`.
  std::string GetName(BbHandle bb_handle) const {
    const auto &aliases = symbol_info_map_.at(bb_handle.function_index).aliases;
    std::string func_name =
        aliases.empty()
            ? absl::StrCat(
                  "0x",
                  absl::Hex(GetFunctionEntry(bb_handle).getFunctionAddress()))
            : aliases.front().str();
    return absl::StrCat(func_name, ":", bb_handle.range_index, ":",
                        bb_handle.bb_index);
  }

  // Returns whether a branch to `to_bb_handle` landing at address `to_address`
  // is a call.
  bool IsCall(BbHandle to_bb_handle, uint64_t to_address) const {
    return to_bb_handle.range_index == 0 && to_bb_handle.bb_index == 0 &&
           to_address == GetAddress(to_bb_handle);
  }

  // Returns whether a branch from `from_bb_handle` to `to_bb_handle` landing at
  // address `to_address` is a return.
  bool IsReturn(std::optional<BbHandle> from_bb_handle, BbHandle to_bb_handle,
                uint64_t to_address) const {
    return GetAddress(to_bb_handle) != to_address ||
           ((to_bb_handle.range_index != 0 || to_bb_handle.bb_index != 0) &&
            (!from_bb_handle.has_value() ||
             GetBBEntry(*from_bb_handle).hasReturn()));
  }

  // Returns whether the `from` basic block can fallthrough to the `to` basic
  // block. `from` and `to` should be indices into the `bb_handles()` vector.
  bool CanFallThrough(int from, int to) const;

  // Extracts and returns intra-function paths from `address_path`.
  // This will try to stitch call-and-return paths together to form
  // intra-function paths which bypass calls.
  std::vector<FlatBbHandleBranchPath> ExtractIntraFunctionPaths(
      const BinaryAddressBranchPath &address_path) const;

 private:
  absl::btree_set<int> selected_functions_;

  // BB handles for all basic blocks of the selected functions. BB handles are
  // ordered in increasing order of their addresses. Thus every function's
  // BB handles are consecutive and in the order of their addresses. e.g.,
  // <func_idx_1, 0>
  // <func_idx_1, 1>
  // ...
  // <func_idx_1, n_1>
  // <func_idx_2, 0>
  // ...
  // <func_idx_2, n_2>
  // ...
  std::vector<BbHandle> bb_handles_;

  // Handle to .llvm_bb_addr_map section.
  std::vector<llvm::object::BBAddrMap> bb_addr_map_;

  // A map from function indices to their symbol info (function names and
  // section name).
  absl::flat_hash_map<int, FunctionSymbolInfo> symbol_info_map_;
};

// Builds a `BinaryAddressMapper` for binary represented by `binary_content` and
// functions with addresses in `hot_addresses`. If `hot_addresses ==
// nullptr` all functions will be included. Does not take ownership of
// `hot_addresses`, which must outlive this call.
absl::StatusOr<std::unique_ptr<BinaryAddressMapper>> BuildBinaryAddressMapper(
    const PropellerOptions &options, const BinaryContent &binary_content,
    PropellerStats &stats,
    const absl::flat_hash_set<uint64_t> *hot_addresses = nullptr);

}  // namespace propeller

#endif  // PROPELLER_BINARY_ADDRESS_MAPPER_H_
