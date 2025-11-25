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

#ifndef PROPELLER_BINARY_ADDRESS_BRANCH_H_
#define PROPELLER_BINARY_ADDRESS_BRANCH_H_

#include <cstdint>
#include <tuple>
#include <utility>

#include "absl/strings/str_format.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"

namespace propeller {
// `BinaryAddressBranch` represents a taken branch with endpoints specified as
// addresses in a program binary.
struct BinaryAddressBranch {
  uint64_t from, to;
  template <typename H>
  friend H AbslHashValue(H h, const BinaryAddressBranch& b) {
    return H::combine(std::move(h), b.from, b.to);
  }
  bool operator==(const BinaryAddressBranch& b) const {
    return from == b.from && to == b.to;
  }
  bool operator<(const BinaryAddressBranch& b) const {
    return std::forward_as_tuple(from, to) <
           std::forward_as_tuple(b.from, b.to);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BinaryAddressBranch& b) {
    absl::Format(&sink, "0x%016x->0x%016x", b.from, b.to);
  }
};

// `BinaryAddressNotTakenBranch` represents a not-taken branch with address
// specified as a binary address.
struct BinaryAddressNotTakenBranch {
  uint64_t address;
  template <typename H>
  friend H AbslHashValue(H h, const BinaryAddressNotTakenBranch& b) {
    return H::combine(std::move(h), b.address);
  }
  bool operator==(const BinaryAddressNotTakenBranch& b) const {
    return address == b.address;
  }
  bool operator<(const BinaryAddressNotTakenBranch& b) const {
    return address < b.address;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BinaryAddressNotTakenBranch& b) {
    absl::Format(&sink, "0x%016x", b.address);
  }
};

// `BinaryAddressFallthrough` represents an address range of
// sequentially-executed instruction with endpoints specified as addresses in a
// program binary.
struct BinaryAddressFallthrough {
  uint64_t from, to;
  template <typename H>
  friend H AbslHashValue(H h, const BinaryAddressFallthrough& f) {
    return H::combine(std::move(h), f.from, f.to);
  }
  bool operator==(const BinaryAddressFallthrough& f) const {
    return from == f.from && to == f.to;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BinaryAddressFallthrough& f) {
    absl::Format(&sink, "0x%016x->0x%016x", f.from, f.to);
  }
};

// The sentinel value for an invalid binary address.
inline constexpr uint64_t kInvalidBinaryAddress = static_cast<uint64_t>(-1);

}  // namespace propeller

namespace llvm {

template <>
struct DenseMapInfo<propeller::BinaryAddressBranch> {
  static inline propeller::BinaryAddressBranch getEmptyKey() {
    return {propeller::kInvalidBinaryAddress - 2,
            propeller::kInvalidBinaryAddress - 2};
  }
  static inline propeller::BinaryAddressBranch getTombstoneKey() {
    return {propeller::kInvalidBinaryAddress - 3,
            propeller::kInvalidBinaryAddress - 3};
  }
  static unsigned getHashValue(const propeller::BinaryAddressBranch& Val) {
    return llvm::hash_combine(Val.from, Val.to);
  }
  static bool isEqual(const propeller::BinaryAddressBranch& LHS,
                      const propeller::BinaryAddressBranch& RHS) {
    return LHS == RHS;
  }
};

template <>
struct DenseMapInfo<propeller::BinaryAddressNotTakenBranch> {
  static inline propeller::BinaryAddressNotTakenBranch getEmptyKey() {
    return {propeller::kInvalidBinaryAddress - 2};
  }
  static inline propeller::BinaryAddressNotTakenBranch getTombstoneKey() {
    return {propeller::kInvalidBinaryAddress - 3};
  }
  static unsigned getHashValue(
      const propeller::BinaryAddressNotTakenBranch& Val) {
    return llvm::hash_value(Val.address);
  }
  static bool isEqual(const propeller::BinaryAddressNotTakenBranch& LHS,
                      const propeller::BinaryAddressNotTakenBranch& RHS) {
    return LHS == RHS;
  }
};

template <>
struct DenseMapInfo<propeller::BinaryAddressFallthrough> {
  static inline propeller::BinaryAddressFallthrough getEmptyKey() {
    return {propeller::kInvalidBinaryAddress - 2,
            propeller::kInvalidBinaryAddress - 2};
  }
  static inline propeller::BinaryAddressFallthrough getTombstoneKey() {
    return {propeller::kInvalidBinaryAddress - 3,
            propeller::kInvalidBinaryAddress - 3};
  }
  static unsigned getHashValue(const propeller::BinaryAddressFallthrough& Val) {
    return llvm::hash_combine(Val.from, Val.to);
  }
  static bool isEqual(const propeller::BinaryAddressFallthrough& LHS,
                      const propeller::BinaryAddressFallthrough& RHS) {
    return LHS == RHS;
  }
};

}  // namespace llvm

#endif  // PROPELLER_BINARY_ADDRESS_BRANCH_H_
