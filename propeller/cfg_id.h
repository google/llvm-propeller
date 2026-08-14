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

#ifndef PROPELLER_CFG_ID_H_
#define PROPELLER_CFG_ID_H_

#include <algorithm>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>

#include "absl/strings/str_format.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

namespace propeller {
// CFGNode Id unique with a single CFG.
struct IntraCfgId {
  // Index of the basic block in the original function.
  int bb_index = 0;
  // Clone number of the basic block (zero for an original block).
  int clone_number = 0;

  bool operator==(const IntraCfgId& other) const {
    return bb_index == other.bb_index && clone_number == other.clone_number;
  }
  bool operator!=(const IntraCfgId& other) const { return !(*this == other); }
  friend llvm::hash_code hash_value(const IntraCfgId& id) {
    return llvm::hash_combine(id.bb_index, id.clone_number);
  }
  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename H>
  friend H AbslHashValue(H h, const IntraCfgId& id) {
    return H::combine(std::move(h), id.bb_index, id.clone_number);
  }
  bool operator<(const IntraCfgId& other) const {
    return std::forward_as_tuple(bb_index, clone_number) <
           std::forward_as_tuple(other.bb_index, other.clone_number);
  }
  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const IntraCfgId& id) {
    absl::Format(&sink, "[BB index: %d, clone number: %v]", id.bb_index,
                 id.clone_number);
  }
  template <typename Stream>
  void print(Stream& os) const {
    os << "[BB index: " << bb_index << ", clone number: " << clone_number
       << "]";
  }
  // Overload for standard C++ streams and GoogleTest printing.
  friend std::ostream& operator<<(std::ostream& os, const IntraCfgId& id) {
    id.print(os);
    return os;
  }
  // Overload for LLVM-native stream printing (e.g., llvm::outs, llvm::dbgs).
  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                       const IntraCfgId& id) {
    id.print(os);
    return os;
  }
};

// This struct represents a full intra-cfg identifier for a basic block,
// combining the fixed bb_id and intra_cfg_id (consisting of bb_index and
// clone number) of the associated cfg node.
struct FullIntraCfgId {
  int bb_id = 0;
  IntraCfgId intra_cfg_id;

  bool operator==(const FullIntraCfgId& other) const {
    return bb_id == other.bb_id && intra_cfg_id == other.intra_cfg_id;
  }
  bool operator!=(const FullIntraCfgId& other) const {
    return !(*this == other);
  }
  // Returns a string representation of the basic block id, including the clone
  // number if not zero. This is used to identify a basic block in the propeller
  // profile.
  std::string profile_bb_id() const {
    if (intra_cfg_id.clone_number == 0) return std::to_string(bb_id);
    return llvm::formatv("{0}.{1}", bb_id, intra_cfg_id.clone_number).str();
  }
};

// CFGNode Id unique across the program.
struct InterCfgId {
  int function_index = 0;

  IntraCfgId intra_cfg_id;

  bool operator==(const InterCfgId& other) const {
    return function_index == other.function_index &&
           intra_cfg_id == other.intra_cfg_id;
  }
  bool operator!=(const InterCfgId& other) const { return !(*this == other); }
  friend llvm::hash_code hash_value(const InterCfgId& id) {
    return llvm::hash_combine(id.function_index, id.intra_cfg_id);
  }
  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename H>
  friend H AbslHashValue(H h, const InterCfgId& id) {
    return H::combine(std::move(h), id.function_index, id.intra_cfg_id);
  }
  bool operator<(const InterCfgId& other) const {
    return std::forward_as_tuple(function_index, intra_cfg_id) <
           std::forward_as_tuple(other.function_index, other.intra_cfg_id);
  }
  // TODO(b/545770511): Remove once all callers are migrated to LLVM utilities.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const InterCfgId& id) {
    absl::Format(&sink, "[function index: %d, %v]", id.function_index,
                 id.intra_cfg_id);
  }
  template <typename Stream>
  void print(Stream& os) const {
    os << "[function index: " << function_index << ", " << intra_cfg_id << "]";
  }
  // Overload for standard C++ streams and GoogleTest printing.
  friend std::ostream& operator<<(std::ostream& os, const InterCfgId& id) {
    id.print(os);
    return os;
  }
  // Overload for LLVM-native stream printing (e.g., llvm::outs, llvm::dbgs).
  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                       const InterCfgId& id) {
    id.print(os);
    return os;
  }
};

}  // namespace propeller

namespace llvm {
template <>
struct DenseMapInfo<propeller::IntraCfgId> {
  static propeller::IntraCfgId getEmptyKey() {
    return propeller::IntraCfgId{-1, -1};
  }
  static propeller::IntraCfgId getTombstoneKey() {
    return propeller::IntraCfgId{-2, -2};
  }
  static unsigned getHashValue(const propeller::IntraCfgId& val) {
    return static_cast<unsigned>(hash_value(val));
  }
  static bool isEqual(const propeller::IntraCfgId& lhs,
                      const propeller::IntraCfgId& rhs) {
    return lhs == rhs;
  }
};

template <>
struct DenseMapInfo<propeller::InterCfgId> {
  static propeller::InterCfgId getEmptyKey() {
    return propeller::InterCfgId{-1, {-1, -1}};
  }
  static propeller::InterCfgId getTombstoneKey() {
    return propeller::InterCfgId{-2, {-2, -2}};
  }
  static unsigned getHashValue(const propeller::InterCfgId& val) {
    return static_cast<unsigned>(hash_value(val));
  }
  static bool isEqual(const propeller::InterCfgId& lhs,
                      const propeller::InterCfgId& rhs) {
    return lhs == rhs;
  }
};
}  // namespace llvm

#endif  // PROPELLER_CFG_ID_H_
