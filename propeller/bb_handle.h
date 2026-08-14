// Copyright 2026 The Propeller Authors.
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

#ifndef PROPELLER_BB_HANDLE_H_
#define PROPELLER_BB_HANDLE_H_

#include <optional>
#include <ostream>
#include <utility>

#include "absl/strings/str_format.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/raw_ostream.h"

namespace propeller {

// A struct representing a basic block entry in the flattened basic block list
// of all ranges of a function.
struct FlatBbHandle {
  int function_index = -1;
  // Index of the basic block in the flattened basic block list of all ranges.
  int flat_bb_index = -1;

  bool operator==(const FlatBbHandle& other) const {
    return function_index == other.function_index &&
           flat_bb_index == other.flat_bb_index;
  }

  bool operator!=(const FlatBbHandle& other) const { return !(*this == other); }

  friend llvm::hash_code hash_value(const FlatBbHandle& bb_handle) {
    return llvm::hash_combine(bb_handle.function_index,
                              bb_handle.flat_bb_index);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename H>
  friend H AbslHashValue(H h, const FlatBbHandle& bb_handle) {
    return H::combine(std::move(h), bb_handle.function_index,
                      bb_handle.flat_bb_index);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FlatBbHandle& bb_handle) {
    absl::Format(&sink, "%d#%d", bb_handle.function_index,
                 bb_handle.flat_bb_index);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink,
                            const std::optional<FlatBbHandle>& bb_handle) {
    if (bb_handle.has_value()) {
      absl::Format(&sink, "%v", *bb_handle);
    } else {
      absl::Format(&sink, "%s", "unknown");
    }
  }

  template <typename Stream>
  void print(Stream& os) const {
    os << function_index << "#" << flat_bb_index;
  }

  // Overload for standard C++ streams and GoogleTest printing.
  friend std::ostream& operator<<(std::ostream& os,
                                  const FlatBbHandle& bb_handle) {
    bb_handle.print(os);
    return os;
  }

  // Overload for LLVM-native stream printing (e.g., llvm::outs, llvm::dbgs).
  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                       const FlatBbHandle& bb_handle) {
    bb_handle.print(os);
    return os;
  }
};

// A struct representing one basic block entry in the BB address map.
struct BbHandle {
  // Indexes into BB address map for a basic block, which would access the BB
  // at `bb_addr_map[function_index].BBRanges[range_index].BBEntries[bb_index]`.
  int function_index = -1, range_index = 0, bb_index = -1;

  bool operator==(const BbHandle& other) const {
    return function_index == other.function_index &&
           range_index == other.range_index && bb_index == other.bb_index;
  }

  bool operator!=(const BbHandle& other) const { return !(*this == other); }

  friend llvm::hash_code hash_value(const BbHandle& bb_handle) {
    return llvm::hash_combine(bb_handle.function_index, bb_handle.range_index,
                              bb_handle.bb_index);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename H>
  friend H AbslHashValue(H h, const BbHandle& bb_handle) {
    return H::combine(std::move(h), bb_handle.function_index,
                      bb_handle.range_index, bb_handle.bb_index);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BbHandle& bb_handle) {
    absl::Format(&sink, "%d#%d#%d", bb_handle.function_index,
                 bb_handle.range_index, bb_handle.bb_index);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink,
                            const std::optional<BbHandle>& bb_handle) {
    if (bb_handle.has_value()) {
      absl::Format(&sink, "%v", *bb_handle);
    } else {
      absl::Format(&sink, "%s", "unknown");
    }
  }

  template <typename Stream>
  void print(Stream& os) const {
    os << function_index << "#" << range_index << "#" << bb_index;
  }

  // Overload for standard C++ streams and GoogleTest printing.
  friend std::ostream& operator<<(std::ostream& os, const BbHandle& bb_handle) {
    bb_handle.print(os);
    return os;
  }

  // Overload for LLVM-native stream printing (e.g., llvm::outs, llvm::dbgs).
  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                       const BbHandle& bb_handle) {
    bb_handle.print(os);
    return os;
  }
};

// This struct captures the call and return information about a single callsite.
// Specifically, the function that is called and the basic block which returns
// back to that callsite. Note that the return block may be in a different
// function than the callee (which may happen if the callee has a tail call
// itself).
struct CallRetInfo {
  // Index of the callee function (or `std::nullopt` if unknown).
  std::optional<int> callee;
  // Return block (or `std::nullopt` if unknown).
  std::optional<FlatBbHandle> return_bb;

  friend llvm::hash_code hash_value(const CallRetInfo& call_ret) {
    return llvm::hash_combine(call_ret.callee, call_ret.return_bb);
  }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename H>
  friend H AbslHashValue(H h, const CallRetInfo& call_ret) {
    return H::combine(std::move(h), call_ret.callee, call_ret.return_bb);
  }

  bool operator==(const CallRetInfo& other) const {
    return callee == other.callee && return_bb == other.return_bb;
  }

  bool operator!=(const CallRetInfo& other) const { return !(*this == other); }

  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const CallRetInfo& call_ret) {
    absl::Format(&sink, "call:");
    if (call_ret.callee.has_value()) {
      absl::Format(&sink, "%d", *call_ret.callee);
    } else {
      absl::Format(&sink, "unknown");
    }
    absl::Format(&sink, "#ret:%v", call_ret.return_bb);
  }

  template <typename Stream>
  void print(Stream& os) const {
    os << "call:";
    if (callee.has_value()) {
      os << *callee;
    } else {
      os << "unknown";
    }
    os << "#ret:";
    if (return_bb.has_value()) {
      os << *return_bb;
    } else {
      os << "unknown";
    }
  }

  // Overload for standard C++ streams and GoogleTest printing.
  friend std::ostream& operator<<(std::ostream& os,
                                  const CallRetInfo& call_ret) {
    call_ret.print(os);
    return os;
  }

  // Overload for LLVM-native stream printing (e.g., llvm::outs, llvm::dbgs).
  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                       const CallRetInfo& call_ret) {
    call_ret.print(os);
    return os;
  }
};
}  // namespace propeller

namespace llvm {
template <>
struct DenseMapInfo<propeller::FlatBbHandle> {
  static propeller::FlatBbHandle getEmptyKey() {
    return propeller::FlatBbHandle{-1, -1};
  }
  static propeller::FlatBbHandle getTombstoneKey() {
    return propeller::FlatBbHandle{-2, -2};
  }
  static unsigned getHashValue(const propeller::FlatBbHandle& val) {
    return static_cast<unsigned>(hash_value(val));
  }
  static bool isEqual(const propeller::FlatBbHandle& lhs,
                      const propeller::FlatBbHandle& rhs) {
    return lhs == rhs;
  }
};

template <>
struct DenseMapInfo<propeller::BbHandle> {
  static propeller::BbHandle getEmptyKey() {
    return propeller::BbHandle{-1, -1, -1};
  }
  static propeller::BbHandle getTombstoneKey() {
    return propeller::BbHandle{-2, -2, -2};
  }
  static unsigned getHashValue(const propeller::BbHandle& val) {
    return static_cast<unsigned>(hash_value(val));
  }
  static bool isEqual(const propeller::BbHandle& lhs,
                      const propeller::BbHandle& rhs) {
    return lhs == rhs;
  }
};
}  // namespace llvm

#endif  // PROPELLER_BB_HANDLE_H_
