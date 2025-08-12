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

#include "propeller/binary_address_mapper.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <stack>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_branch.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_content.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_macros.h"  // Included for macros.

namespace propeller {

namespace {
using ::llvm::StringRef;
using ::llvm::object::BBAddrMap;

// Handle for a BB range.
struct BbRangeHandle {
  int function_index = 0;
  int range_index = 0;
};

// Returns the BB range handles for BB ranges in `bb_addr_map`, sorted by
// their base address. If `selected_functions != nullptr` only returns the
// BB range handles for the functions in the set. Otherwise, returns all BB
// range handles.
std::vector<BbRangeHandle> GetBbRangeHandles(
    absl::Span<const BBAddrMap> bb_addr_map,
    const absl::btree_set<int>* absl_nullable selected_functions = nullptr) {
  std::vector<BbRangeHandle> bb_range_handles;
  auto add_bb_range_handles = [&](int function_index) {
    for (int j = 0; j != bb_addr_map[function_index].getBBRanges().size();
         ++j) {
      bb_range_handles.push_back(
          {.function_index = function_index, .range_index = j});
    }
  };
  if (selected_functions != nullptr) {
    for (int function_index : *selected_functions)
      add_bb_range_handles(function_index);
  } else {
    for (int i = 0; i != bb_addr_map.size(); ++i) add_bb_range_handles(i);
  }
  absl::c_sort(bb_range_handles,
               [&](const BbRangeHandle& a, const BbRangeHandle& b) {
                 return bb_addr_map[a.function_index]
                            .getBBRanges()[a.range_index]
                            .BaseAddress < bb_addr_map[b.function_index]
                                               .getBBRanges()[b.range_index]
                                               .BaseAddress;
               });
  return bb_range_handles;
}

// Returns the BB handles for BBs in `selected_functions` in `bb_addr_map`,
// sorted by their address.
std::vector<BbHandle> GetBbHandles(
    absl::Span<const BBAddrMap> bb_addr_map,
    const absl::btree_set<int>& selected_functions) {
  std::vector<BbHandle> bb_handles;
  std::vector<BbRangeHandle> selected_bb_range_handles =
      GetBbRangeHandles(bb_addr_map, &selected_functions);
  std::optional<uint64_t> last_bb_range_address;
  for (const BbRangeHandle& bb_range_handle : selected_bb_range_handles) {
    const BBAddrMap::BBRangeEntry& bb_range =
        bb_addr_map[bb_range_handle.function_index]
            .getBBRanges()[bb_range_handle.range_index];
    if (last_bb_range_address.has_value())
      CHECK_GE(bb_range.BaseAddress, *last_bb_range_address);
    for (int i = 0; i != bb_range.BBEntries.size(); ++i) {
      bb_handles.push_back(
          BbHandle{.function_index = bb_range_handle.function_index,
                   .range_index = bb_range_handle.range_index,
                   .bb_index = i});
    }
    last_bb_range_address = bb_range.BaseAddress;
  }
  return bb_handles;
}

// Returns a map from function indexes to their symbol info, given a map from
// function addresses to their symbol info and a list of BBAddrMap for all
// functions. It takes the symbol info map by value so that its values can be
// moved into the returned map.
absl::flat_hash_map<int, FunctionSymbolInfo> GetSymbolInfoMapByFunctionIndex(
    absl::flat_hash_map<uint64_t, FunctionSymbolInfo> symbol_info_map,
    absl::Span<const BBAddrMap> bb_addr_map) {
  absl::flat_hash_map<int, FunctionSymbolInfo>
      symbol_info_map_by_function_index;
  for (int function_index = 0; function_index != bb_addr_map.size();
       ++function_index) {
    auto iter =
        symbol_info_map.find(bb_addr_map[function_index].getFunctionAddress());
    if (iter == symbol_info_map.end()) {
      LOG(WARNING) << "BB address map for function at "
                   << absl::StrCat(absl::Hex(
                          bb_addr_map[function_index].getFunctionAddress()))
                   << " has no associated symbol table entry!";
      continue;
    }
    symbol_info_map_by_function_index.emplace(function_index,
                                              std::move(iter->second));
  }
  return symbol_info_map_by_function_index;
}

// Builds `BinaryAddressMapper` for a binary and its profile.
class BinaryAddressMapperBuilder {
 public:
  BinaryAddressMapperBuilder(
      absl::flat_hash_map<uint64_t, FunctionSymbolInfo> symbol_info_map,
      std::vector<llvm::object::BBAddrMap> bb_addr_map, PropellerStats& stats,
      const PropellerOptions* absl_nonnull options
          ABSL_ATTRIBUTE_LIFETIME_BOUND);

  BinaryAddressMapperBuilder(const BinaryAddressMapperBuilder&) = delete;
  BinaryAddressMapperBuilder& operator=(const BinaryAddressMapper&) = delete;
  BinaryAddressMapperBuilder(BinaryAddressMapperBuilder&&) = delete;
  BinaryAddressMapperBuilder& operator=(BinaryAddressMapperBuilder&&) = delete;

  // Builds and returns a `BinaryAddressMapper`. When
  // `hot_addresses != nullptr` only selects functions with addresses in
  // `*hot_addresses`. Otherwise, all functions are included. Does not take
  // ownership of `*hot_addresses`, which must outlive this call.
  std::unique_ptr<BinaryAddressMapper> Build(
      const absl::flat_hash_set<uint64_t>* hot_addresses) &&;

 private:
  // Returns a list of hot functions based on addresses `hot_addresses`.
  // The returned `btree_set` specifies the hot functions by their index in
  // `bb_addr_map()`.
  absl::btree_set<int> CalculateHotFunctions(
      const absl::flat_hash_set<uint64_t>& hot_addresses);

  // Removes unwanted functions from the BB address map and symbol table, and
  // returns the remaining functions by their indexes in `bb_addr_map()`.
  // This function removes all non-text functions, functions without associated
  // names, and those with duplicate names. Selects all functions when
  // `hot_addresses == nullptr`.
  absl::btree_set<int> SelectFunctions(
      const absl::flat_hash_set<uint64_t>* hot_addresses);

  // Removes all functions that are not included (selected) in the
  // `selected_functions` set. Clears their associated BB entries from
  // `bb_addr_map_` and also removes their associated entries from `symtab_`.
  void DropNonSelectedFunctions(const absl::btree_set<int>& selected_functions);

  // Removes all functions without associated symbol names from the given
  // function indices.
  void FilterNoNameFunctions(absl::btree_set<int>& selected_functions) const;

  // Removes all functions in non-text sections from the specified set of
  // function indices.
  void FilterNonTextFunctions(absl::btree_set<int>& selected_functions) const;

  // Removes all functions with duplicate names from the specified function
  // indices. Must be called after `FilterNoNameFunctions`.
  int FilterDuplicateNameFunctions(
      absl::btree_set<int>& selected_functions) const;

  // BB address map of functions.
  std::vector<llvm::object::BBAddrMap> bb_addr_map_;

  // Handles for BB ranges in `bb_addr_map_`, sorted by their base address.
  std::vector<BbRangeHandle> bb_range_handles_;

  // Map from every function index (in `bb_addr_map_`) to its symbol info.
  absl::flat_hash_map<int, FunctionSymbolInfo> symbol_info_map_;

  PropellerStats* stats_;
  const PropellerOptions* options_;
};

// Helper class for extracting intra-function paths from binary-address paths.
// Example usage:
//   IntraFunctionPathsExtractor(&binary_address_mapper).Extract();
class IntraFunctionPathsExtractor {
 public:
  // Does not take ownership of `address_mapper` which should point to a valid
  // object which outlives the constructed `IntraFunctionPathsExtractor`.
  explicit IntraFunctionPathsExtractor(
      const BinaryAddressMapper* address_mapper)
      : address_mapper_(address_mapper) {}

  IntraFunctionPathsExtractor(const IntraFunctionPathsExtractor&) = delete;
  IntraFunctionPathsExtractor& operator=(const IntraFunctionPathsExtractor&) =
      delete;
  IntraFunctionPathsExtractor(IntraFunctionPathsExtractor&&) = default;
  IntraFunctionPathsExtractor& operator=(IntraFunctionPathsExtractor&&) =
      default;

  // Merges adjacent callsite branches by merging all of their calls into the
  // first one, while keeping the order.
  void MergeCallsites(std::vector<FlatBbHandleBranchPath>& paths) {
    for (auto& path : paths) {
      FlatBbHandleBranch* prev_branch = &*path.branches.begin();
      path.branches.erase(
          std::remove_if(
              path.branches.begin() + 1, path.branches.end(),
              [&](FlatBbHandleBranch& branch) {
                if (prev_branch->is_callsite() && branch.is_callsite() &&
                    prev_branch->from_bb == branch.from_bb) {
                  CHECK(prev_branch->from_bb == prev_branch->to_bb)
                      << prev_branch
                      << " is not a callsite in a single "
                         "block.";
                  absl::c_move(branch.call_rets,
                               std::back_inserter(prev_branch->call_rets));
                  return true;
                }
                prev_branch = &branch;
                return false;
              }),
          path.branches.end());
    }
  }

  // Extracts and returns the intra-function paths in `address_path`.
  std::vector<FlatBbHandleBranchPath> Extract(
      const BinaryAddressBranchPath& address_path) && {
    pid_ = address_path.pid;
    sample_time_ = address_path.sample_time;

    // Helper function to get the BB handle associated with an index, or
    // nullopt if the index is nullopt.
    auto GetBbHandleByIndex =
        [&](std::optional<int> index) -> std::optional<BbHandle> {
      if (!index.has_value()) return std::nullopt;
      return address_mapper_->bb_handles().at(*index);
    };

    for (const BinaryAddressBranch& branch : address_path.branches) {
      std::optional<BbHandle> from_bb_handle = GetBbHandleByIndex(
          address_mapper_->FindBbHandleIndexUsingBinaryAddress(
              branch.from, BranchDirection::kFrom));
      std::optional<BbHandle> to_bb_handle = GetBbHandleByIndex(
          address_mapper_->FindBbHandleIndexUsingBinaryAddress(
              branch.to, BranchDirection::kTo));
      std::optional<FlatBbHandle> from_flat_bb_handle =
          address_mapper_->GetFlatBbHandle(from_bb_handle);
      std::optional<FlatBbHandle> to_flat_bb_handle =
          address_mapper_->GetFlatBbHandle(to_bb_handle);

      if (from_bb_handle.has_value()) {
        // Augment the current path if the current path is from the same
        // function and ends at a known address. Otherwise switch to a new path.
        if (from_bb_handle->function_index == current_function_index_ &&
            GetCurrentLastBranch().to_bb.has_value()) {
          AugmentCurrentPath({.from_bb = from_flat_bb_handle});
        } else {
          AddNewPath({.from_bb = from_flat_bb_handle});
        }
      }
      if (!to_bb_handle.has_value()) continue;
      if (address_mapper_->IsCall(*to_bb_handle, branch.to)) {
        HandleCall(from_bb_handle, *to_bb_handle);
        continue;
      }
      if (address_mapper_->IsReturn(from_bb_handle, *to_bb_handle, branch.to)) {
        HandleReturn(from_bb_handle, *to_bb_handle, branch.to);
        continue;
      }
      if (from_bb_handle->function_index != to_bb_handle->function_index) {
        LOG(WARNING) << "Inter-function edge from: " << *from_bb_handle
                     << " to: " << *to_bb_handle
                     << "is not a return or a call.";
        AddNewPath({.to_bb = to_flat_bb_handle});
        continue;
      }
      // Not a call or a return. It must be a normal branch within the same
      // function.
      CHECK(from_bb_handle.has_value());
      HandleRegularBranch(*from_flat_bb_handle, *to_flat_bb_handle);
    }
    MergeCallsites(paths_);
    return std::move(paths_);
  }

 private:
  // Extends the current path by adding a regular branch `from_bb_handle` to
  // `to_bb_handle`, which is intra-function and not call or return. Assumes and
  // verifies that `GetCurrentLastBranch()` already has its source assigned as
  // `from_bb_handle` and then assigns its sink to `to_bb_handle`.
  void HandleRegularBranch(FlatBbHandle from_bb_handle,
                           FlatBbHandle to_bb_handle) {
    CHECK_EQ(from_bb_handle.function_index, to_bb_handle.function_index)
        << " from: " << from_bb_handle << " to: " << to_bb_handle;
    auto& last_branch = GetCurrentLastBranch();
    CHECK(last_branch.from_bb.has_value());
    CHECK(*last_branch.from_bb == from_bb_handle);
    last_branch.to_bb = to_bb_handle;
  }

  // Handles a call from `from_bb_handle` to `to_bb_handle`. Stores the current
  // path in the stack and inserts and switches to a new path starting with
  // `to_bb_handle`.
  void HandleCall(std::optional<BbHandle> from_bb_handle,
                  BbHandle to_bb_handle) {
    std::optional<FlatBbHandle> to_flat_bb_handle =
        address_mapper_->GetFlatBbHandle(to_bb_handle);
    if (from_bb_handle.has_value()) {
      // Pop the current path off the call stack if the from bb has a tail call.
      // Note that this may incorrectly pop off the call stack for a regular
      // call located in a block ending with a tail call. However, popping off
      // the stack will make the paths shorter, but won't affect correctness.
      if (address_mapper_->GetBBEntry(*from_bb_handle).hasTailCall())
        call_stack_[current_function_index_].pop();
      GetCurrentLastBranch().call_rets.push_back(
          {.callee = to_bb_handle.function_index});
    }
    AddNewPath({.to_bb = to_flat_bb_handle});
  }

  // Handles a return from `from_bb_handle` to `to_bb_handle` which returns to
  // address `return_address`. Terminates the path corresponding to the callee.
  // Then tries to find and switch to the path corresponding to the callsite of
  // this return. Starts a new path if the caller path was not found.
  void HandleReturn(std::optional<BbHandle> from_bb_handle,
                    BbHandle to_bb_handle, uint64_t return_address) {
    std::optional<FlatBbHandle> to_flat_bb_handle =
        address_mapper_->GetFlatBbHandle(to_bb_handle);
    std::optional<FlatBbHandle> from_flat_bb_handle =
        address_mapper_->GetFlatBbHandle(from_bb_handle);
    // If this is returning to the beginning of a basic block, the call
    // must have been the last instruction of the previous basic block and
    // we actually return to the end of that block.
    BbHandle return_to_bb = to_bb_handle;
    if (address_mapper_->GetAddress(to_bb_handle) == return_address &&
        to_bb_handle.bb_index != 0) {
      BbHandle prev_bb_handle = {.function_index = to_bb_handle.function_index,
                                 .range_index = to_bb_handle.range_index,
                                 .bb_index = to_bb_handle.bb_index - 1};
      if (address_mapper_->GetBBEntry(prev_bb_handle).canFallThrough()) {
        return_to_bb = prev_bb_handle;
      }
    }
    std::optional<FlatBbHandle> return_to_flat_bb =
        address_mapper_->GetFlatBbHandle(return_to_bb);
    // Set the returns_to block and pop off the call stack if the return is from
    // a known BB.
    if (from_bb_handle.has_value()) {
      paths_[current_path_index_].returns_to = return_to_flat_bb;
      call_stack_[current_function_index_].pop();
    }
    // Find the path corresponding to the callsite.
    auto it = call_stack_.find(to_bb_handle.function_index);
    if (it == call_stack_.end() || it->second.empty()) {
      // The callsite path doesn't exist in this trace.
      AddNewPath(
          {.from_bb =
               to_bb_handle == return_to_bb ? std::nullopt : return_to_flat_bb,
           .to_bb = to_flat_bb_handle,
           .call_rets = {CallRetInfo{.return_bb = from_flat_bb_handle}}});
      return;
    }
    current_path_index_ = it->second.top();
    FlatBbHandleBranch& callsite_branch = GetCurrentLastBranch();

    if (callsite_branch.to_bb.has_value()) {
      LOG_EVERY_N(INFO, 100)
          << "Found corrupt callsite path while assigning sink: "
          << to_bb_handle << " branched-to from: " << from_bb_handle
          << " (path's last branch already has a sink): "
          << paths_[current_path_index_];
      AddNewPath({.from_bb = to_bb_handle == return_to_bb ? std::nullopt
                                                          : return_to_flat_bb,
                  .to_bb = to_flat_bb_handle});
      return;
    }
    CHECK(callsite_branch.from_bb.has_value());
    std::optional<BbHandle> callsite_bb =
        address_mapper_->GetBbHandle(*callsite_branch.from_bb);
    CHECK(callsite_bb.has_value());
    CHECK_EQ(callsite_bb->function_index, to_bb_handle.function_index);
    // Check that the returned-to block is the same as the callsite block or
    // immediately after. Start a new path if found otherwise.
    if ((to_bb_handle.range_index != callsite_bb->range_index ||
         to_bb_handle.bb_index != callsite_bb->bb_index) &&
        address_mapper_->GetAddress(to_bb_handle) !=
            address_mapper_->GetEndAddress(*callsite_bb)) {
      LOG_EVERY_N(INFO, 100)
          << "Found corrupt callsite path while assigning sink: "
          << to_bb_handle << " branched-to from: " << from_bb_handle
          << " (return address does not fall immediately after the call): "
          << paths_[current_path_index_];
      AddNewPath({.from_bb = to_bb_handle == return_to_bb ? std::nullopt
                                                          : return_to_flat_bb,
                  .to_bb = to_flat_bb_handle});
      return;
    }
    // Insert a new `CallRetInfo` or assign `return_bb` of the last one.
    if (callsite_branch.call_rets.empty()) {
      callsite_branch.call_rets.push_back(
          CallRetInfo{.return_bb = from_flat_bb_handle});
    } else {
      if (callsite_branch.call_rets.back().return_bb.has_value()) {
        callsite_branch.call_rets.push_back(
            CallRetInfo{.return_bb = from_flat_bb_handle});
      } else {
        callsite_branch.call_rets.back().return_bb = from_flat_bb_handle;
      }
    }
    // Assign the sink of the last branch. This can be a return back to the
    // same block or the next (when the call instruction is the last
    // instruction of the block).
    callsite_branch.to_bb = to_flat_bb_handle;
    current_function_index_ = to_bb_handle.function_index;
  }

  // Inserts `bb_branch` at the end of the current path.
  void AugmentCurrentPath(const FlatBbHandleBranch& bb_branch) {
    paths_[current_path_index_].branches.push_back(bb_branch);
  }

  // Adds a new path with a single branch `bb_branch` and updates
  // `current_path_index_` and `call_stack_`.
  void AddNewPath(const FlatBbHandleBranch& bb_branch) {
    current_function_index_ = bb_branch.from_bb.has_value()
                                  ? bb_branch.from_bb->function_index
                                  : bb_branch.to_bb->function_index;
    paths_.push_back(
        {.pid = pid_, .sample_time = sample_time_, .branches = {bb_branch}});
    current_path_index_ = paths_.size() - 1;
    call_stack_[current_function_index_].push(current_path_index_);
  }

  FlatBbHandleBranch& GetCurrentLastBranch() {
    CHECK_GE(current_path_index_, 0);
    CHECK(!paths_[current_path_index_].branches.empty());
    return paths_[current_path_index_].branches.back();
  }

  const BinaryAddressMapper* address_mapper_ = nullptr;
  // Process id associated with the path.
  int64_t pid_ = -1;
  // Sample time associated with the path.
  absl::Time sample_time_ = absl::InfinitePast();
  // Index of the current function in address_mapper_->bb_addr_map().
  int current_function_index_ = -1;
  std::vector<FlatBbHandleBranchPath> paths_;
  // Index of the current path in `paths_`.
  int current_path_index_ = -1;
  // Call stack map indexed by function index, mapping to path indices in
  // `paths_` in the calling stack order.
  absl::flat_hash_map<int, std::stack<int>> call_stack_;
};
}  // namespace

std::optional<BbHandle> BinaryAddressMapper::GetBbHandleUsingBinaryAddress(
    uint64_t address, BranchDirection direction) const {
  std::optional<int> index =
      FindBbHandleIndexUsingBinaryAddress(address, direction);
  if (!index.has_value()) return std::nullopt;
  return bb_handles_.at(*index);
}

bool BinaryAddressMapper::CanFallThrough(const BbHandle& from,
                                         const BbHandle& to) const {
  if (from.function_index != to.function_index) return false;
  if (from.range_index != to.range_index) return false;
  if (from.bb_index > to.bb_index) return false;
  const auto& bb_range =
      bb_addr_map_.at(from.function_index).getBBRanges().at(from.range_index);
  for (int bb_index = from.bb_index; bb_index < to.bb_index; ++bb_index) {
    if (!bb_range.BBEntries[bb_index].canFallThrough()) return false;
  }
  return true;
}

std::optional<BbHandle> BinaryAddressMapper::GetBbHandle(
    const FlatBbHandle& flat_bb_handle) const {
  if (flat_bb_handle.function_index >= bb_addr_map_.size()) {
    return std::nullopt;
  }
  const auto& bb_ranges =
      bb_addr_map_[flat_bb_handle.function_index].getBBRanges();
  BbHandle bb_handle{.function_index = flat_bb_handle.function_index,
                     .range_index = 0,
                     .bb_index = flat_bb_handle.flat_bb_index};
  while (bb_handle.bb_index >=
         bb_ranges[bb_handle.range_index].BBEntries.size()) {
    bb_handle.bb_index -= bb_ranges[bb_handle.range_index].BBEntries.size();
    bb_handle.range_index++;
    if (bb_handle.range_index >= bb_ranges.size()) {
      return std::nullopt;
    }
  }
  return bb_handle;
}

std::optional<FlatBbHandle> BinaryAddressMapper::GetFlatBbHandle(
    const BbHandle& bb_handle) const {
  if (bb_addr_map_.size() <= bb_handle.function_index) return std::nullopt;
  const auto& bb_ranges =
      bb_addr_map_.at(bb_handle.function_index).getBBRanges();
  if (bb_handle.range_index >= bb_ranges.size()) return std::nullopt;
  if (bb_handle.bb_index >= bb_ranges[bb_handle.range_index].BBEntries.size()) {
    return std::nullopt;
  }
  return FlatBbHandle{
      .function_index = bb_handle.function_index,
      .flat_bb_index = absl::c_accumulate(
          absl::MakeConstSpan(bb_ranges).subspan(0, bb_handle.range_index),
          bb_handle.bb_index, [](int sum, const auto& range) {
            return sum + range.BBEntries.size();
          })};
}

std::optional<int> BinaryAddressMapper::FindBbHandleIndexUsingBinaryAddress(
    uint64_t address, BranchDirection direction) const {
  std::vector<BbHandle>::const_iterator it = absl::c_upper_bound(
      bb_handles_, address, [this](uint64_t addr, const BbHandle& bb_handle) {
        return addr < GetAddress(bb_handle);
      });
  if (it == bb_handles_.begin()) return std::nullopt;
  it = std::prev(it);
  if (address > GetAddress(*it)) {
    uint64_t bb_end_address = GetAddress(*it) + GetBBEntry(*it).Size;
    if (address < bb_end_address ||
        // We may have returns *to* the end of a block if the last instruction
        // of the block is a call and there is padding after the call, causing
        // the return address to be mapped to the callsite block.
        (address == bb_end_address && direction == BranchDirection::kTo)) {
      return it - bb_handles_.begin();
    } else {
      return std::nullopt;
    }
  }
  DCHECK_EQ(address, GetAddress(*it));
  // We might have multiple zero-sized BBs at the same address. If we are
  // branching to this address, we find and return the first zero-sized BB (from
  // the same function). If we are branching from this address, we return the
  // single non-zero sized BB.
  switch (direction) {
    case BranchDirection::kTo: {
      auto prev_it = it;
      while (prev_it != bb_handles_.begin() &&
             GetAddress(*--prev_it) == address &&
             prev_it->function_index == it->function_index) {
        it = prev_it;
      }
      return it - bb_handles_.begin();
    }
    case BranchDirection::kFrom: {
      DCHECK_NE(GetBBEntry(*it).Size, 0);
      return it - bb_handles_.begin();
    }
      LOG(FATAL) << "Invalid edge direction.";
  }
}

bool BinaryAddressMapper::CanFallThrough(int from, int to) const {
  if (from == to) return true;
  BbHandle from_bb = bb_handles_[from];
  BbHandle to_bb = bb_handles_[to];
  if (from_bb.function_index != to_bb.function_index) {
    LOG_EVERY_N(ERROR, 100)
        << "Skipping fallthrough path " << from_bb << "->" << to_bb
        << ": endpoints are in different functions.";
    return false;
  }
  if (from_bb.range_index != to_bb.range_index) {
    LOG_EVERY_N(ERROR, 100) << "Skipping fallthrough path " << from_bb << "->"
                            << to_bb << ": endpoints are in different ranges.";
    return false;
  }
  if (from_bb.bb_index > to_bb.bb_index) {
    LOG_EVERY_N(WARNING, 100) << "Skipping fallthrough path " << from_bb << "->"
                              << to_bb << ": start comes after end.";
    return false;
  }
  for (int i = from_bb.bb_index; i != to_bb.bb_index; ++i) {
    BbHandle bb_sym = {.function_index = from_bb.function_index,
                       .range_index = from_bb.range_index,
                       .bb_index = i};
    // (b/62827958) Sometimes LBR contains duplicate entries in the beginning
    // of the stack which may result in false fallthrough paths. We discard
    // the fallthrough path if any intermediate block (except the destination
    // block) does not fall through (source block is checked before entering
    // this loop).
    if (!GetBBEntry(bb_sym).canFallThrough()) {
      LOG_EVERY_N(WARNING, 100)
          << "Skipping fallthrough path " << from_bb << "->" << to_bb
          << ": covers non-fallthrough block " << bb_sym << ".";
      return false;
    }
  }
  // Warn about unusually-long fallthroughs.
  if (to - from >= 200) {
    LOG(WARNING) << "More than 200 BBs along fallthrough (" << GetName(from_bb)
                 << " -> " << GetName(to_bb) << "): " << to - from + 1
                 << " BBs.";
  }
  return true;
}

// For each lbr record addr1->addr2, find function1/2 that contain addr1/addr2
// and add function1/2's index into the returned set.
absl::btree_set<int> BinaryAddressMapperBuilder::CalculateHotFunctions(
    const absl::flat_hash_set<uint64_t>& hot_addresses) {
  absl::btree_set<int> hot_functions;
  auto add_to_hot_functions = [this, &hot_functions](uint64_t binary_address) {
    auto it = absl::c_upper_bound(
        bb_range_handles_, binary_address,
        [this](uint64_t addr, const BbRangeHandle& bb_range_handle) {
          return addr < bb_addr_map_[bb_range_handle.function_index]
                            .getBBRanges()[bb_range_handle.range_index]
                            .BaseAddress;
        });
    if (it == bb_range_handles_.begin()) return;
    it = std::prev(it);
    const auto& bb_range =
        bb_addr_map_[it->function_index].getBBRanges()[it->range_index];
    // We know the address is bigger than or equal to the function address.
    // Make sure that it doesn't point beyond the last basic block.
    if (binary_address >= bb_range.BaseAddress +
                              bb_range.BBEntries.back().Offset +
                              bb_range.BBEntries.back().Size)
      return;
    hot_functions.insert(it->function_index);
  };
  for (uint64_t address : hot_addresses) add_to_hot_functions(address);
  stats_->bbaddrmap_stats.hot_functions = hot_functions.size();
  return hot_functions;
}

void BinaryAddressMapperBuilder::DropNonSelectedFunctions(
    const absl::btree_set<int>& selected_functions) {
  for (int i = 0; i != bb_addr_map_.size(); ++i) {
    if (selected_functions.contains(i)) continue;
    symbol_info_map_.erase(i);
  }
}

void BinaryAddressMapperBuilder::FilterNoNameFunctions(
    absl::btree_set<int>& selected_functions) const {
  for (auto it = selected_functions.begin(); it != selected_functions.end();) {
    if (!symbol_info_map_.contains(*it)) {
      LOG(WARNING) << "Hot function at address: 0x"
                   << absl::StrCat(
                          absl::Hex(bb_addr_map_[*it].getFunctionAddress()))
                   << " does not have an associated symbol name.";
      it = selected_functions.erase(it);
    } else {
      ++it;
    }
  }
}

void BinaryAddressMapperBuilder::FilterNonTextFunctions(
    absl::btree_set<int>& selected_functions) const {
  for (auto func_it = selected_functions.begin();
       func_it != selected_functions.end();) {
    int function_index = *func_it;
    const auto& symbol_info = symbol_info_map_.at(function_index);
    if (!symbol_info.section_name.starts_with(".text.") &&
        symbol_info.section_name != ".text") {
      LOG_EVERY_N(WARNING, 1000) << "Skipped symbol in non-'.text.*' section '"
                                 << symbol_info.section_name.str()
                                 << "': " << symbol_info.aliases.front().str();
      func_it = selected_functions.erase(func_it);
    } else {
      ++func_it;
    }
  }
}

// Without '-funique-internal-linkage-names', if multiple functions have the
// same name, even though we can correctly map their profiles, we cannot apply
// those profiles back to their object files.
// This function removes all such functions which have the same name as other
// functions in the binary.
int BinaryAddressMapperBuilder::FilterDuplicateNameFunctions(
    absl::btree_set<int>& selected_functions) const {
  int duplicate_symbols = 0;
  absl::flat_hash_map<StringRef, std::vector<int>> name_to_function_index;
  for (int func_index : selected_functions) {
    for (StringRef name : symbol_info_map_.at(func_index).aliases)
      name_to_function_index[name].push_back(func_index);
  }

  for (auto [name, func_indices] : name_to_function_index) {
    if (func_indices.size() <= 1) continue;
    duplicate_symbols += func_indices.size() - 1;
    // Sometimes, duplicated uniq-named symbols are essentially identical
    // copies. In such cases, we can still keep one copy.
    // TODO(rahmanl): Why does this work? If we remove other copies, we cannot
    // map their profiles either.
    if (name.contains(".__uniq.")) {
      // duplicate uniq-named symbols found
      const BBAddrMap& func_addr_map = bb_addr_map_[func_indices.front()];
      // If the uniq-named functions have the same structure, we assume
      // they are the same and thus we keep one copy of them.
      // TODO(b/383334067): Make `getBBEntries` return all BB ranges.
      bool same_structure = absl::c_all_of(func_indices, [&](int i) {
        return absl::c_equal(
            func_addr_map.getBBEntries(), bb_addr_map_[i].getBBEntries(),
            [](const llvm::object::BBAddrMap::BBEntry& e1,
               const llvm::object::BBAddrMap::BBEntry& e2) {
              return e1.Offset == e2.Offset && e1.Size == e2.Size;
            });
      });
      if (same_structure) {
        LOG(WARNING) << func_indices.size()
                     << " duplicate uniq-named functions '" << name.str()
                     << "' with same size and structure found, keep one copy.";
        for (int i = 1; i < func_indices.size(); ++i)
          selected_functions.erase(func_indices[i]);
        continue;
      }
      LOG(WARNING) << "duplicate uniq-named functions '" << name.str()
                   << "' with different size or structure found , drop "
                      "all of them.";
    }
    for (auto func_idx : func_indices) selected_functions.erase(func_idx);
  }
  return duplicate_symbols;
}

absl::btree_set<int> BinaryAddressMapperBuilder::SelectFunctions(
    const absl::flat_hash_set<uint64_t>* hot_addresses) {
  absl::btree_set<int> selected_functions;
  if (hot_addresses != nullptr) {
    selected_functions = CalculateHotFunctions(*hot_addresses);
  } else {
    for (int i = 0; i != bb_addr_map_.size(); ++i) selected_functions.insert(i);
  }

  FilterNoNameFunctions(selected_functions);
  if (options_->filter_non_text_functions())
    FilterNonTextFunctions(selected_functions);
  stats_->bbaddrmap_stats.duplicate_symbols +=
      FilterDuplicateNameFunctions(selected_functions);
  return selected_functions;
}

std::vector<FlatBbHandleBranchPath>
BinaryAddressMapper::ExtractIntraFunctionPaths(
    const BinaryAddressBranchPath& address_path) const {
  return IntraFunctionPathsExtractor(this).Extract(address_path);
}

BinaryAddressMapperBuilder::BinaryAddressMapperBuilder(
    absl::flat_hash_map<uint64_t, FunctionSymbolInfo> symbol_info_map,
    std::vector<llvm::object::BBAddrMap> bb_addr_map, PropellerStats& stats,
    const PropellerOptions* absl_nonnull options)
    : bb_addr_map_(std::move(bb_addr_map)),
      bb_range_handles_(GetBbRangeHandles(bb_addr_map_)),
      symbol_info_map_(GetSymbolInfoMapByFunctionIndex(
          std::move(symbol_info_map), bb_addr_map_)),
      stats_(&stats),
      options_(options) {
  stats_->bbaddrmap_stats.bbaddrmap_function_does_not_have_symtab_entry +=
      bb_addr_map_.size() - symbol_info_map_.size();
}

BinaryAddressMapper::BinaryAddressMapper(
    absl::btree_set<int> selected_functions,
    std::vector<llvm::object::BBAddrMap> bb_addr_map,
    std::vector<BbHandle> bb_handles,
    absl::flat_hash_map<int, FunctionSymbolInfo> symbol_info_map)
    : selected_functions_(std::move(selected_functions)),
      bb_handles_(std::move(bb_handles)),
      bb_addr_map_(std::move(bb_addr_map)),
      symbol_info_map_(std::move(symbol_info_map)) {}

absl::StatusOr<std::unique_ptr<BinaryAddressMapper>> BuildBinaryAddressMapper(
    const PropellerOptions& options, const BinaryContent& binary_content,
    PropellerStats& stats, const absl::flat_hash_set<uint64_t>* hot_addresses) {
  LOG(INFO) << "Started reading the binary content from: "
            << binary_content.file_name;
  BbAddrMapData bb_addr_map;
  ASSIGN_OR_RETURN(bb_addr_map, ReadBbAddrMap(binary_content));

  return BinaryAddressMapperBuilder(GetSymbolInfoMap(binary_content),
                                    std::move(bb_addr_map.bb_addr_maps), stats,
                                    &options)
      .Build(hot_addresses);
}

std::unique_ptr<BinaryAddressMapper> BinaryAddressMapperBuilder::Build(
    const absl::flat_hash_set<uint64_t>* hot_addresses) && {
  absl::btree_set<int> selected_functions = SelectFunctions(hot_addresses);
  DropNonSelectedFunctions(selected_functions);
  std::vector<BbHandle> bb_handles =
      GetBbHandles(bb_addr_map_, selected_functions);
  return std::make_unique<BinaryAddressMapper>(
      std::move(selected_functions), std::move(bb_addr_map_),
      std::move(bb_handles), std::move(symbol_info_map_));
}

}  // namespace propeller
