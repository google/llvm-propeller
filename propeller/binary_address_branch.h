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

#ifndef PROPELLER_BINARY_ADDRESS_BRANCH_H_
#define PROPELLER_BINARY_ADDRESS_BRANCH_H_

#include <cstdint>
#include <tuple>
#include <utility>

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/FormatVariadic.h"

namespace propeller {
// `BinaryAddressBranch` represents a taken branch with endpoints specified as
// addresses in a program binary.
struct BinaryAddressBranch {
  uint64_t from, to;
  friend llvm::hash_code hash_value(const BinaryAddressBranch& b) {
    return llvm::hash_combine(b.from, b.to);
  }
  // TODO(b/546154457): Remove once all callers are migrated to LLVM containers.
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
    sink.Append(llvm::formatv("{0:x16}->{1:x16}", b.from, b.to).str());
  }
};

// `BinaryAddressNotTakenBranch` represents a not-taken branch with address
// specified as a binary address.
struct BinaryAddressNotTakenBranch {
  uint64_t address;
  friend llvm::hash_code hash_value(const BinaryAddressNotTakenBranch& b) {
    return llvm::hash_value(b.address);
  }
  // TODO(b/546154457): Remove once all callers are migrated to LLVM containers.
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
    sink.Append(llvm::formatv("{0:x16}", b.address).str());
  }
};

// `BinaryAddressFallthrough` represents an address range of
// sequentially-executed instruction with endpoints specified as addresses in a
// program binary.
struct BinaryAddressFallthrough {
  uint64_t from, to;
  friend llvm::hash_code hash_value(const BinaryAddressFallthrough& f) {
    return llvm::hash_combine(f.from, f.to);
  }
  // TODO(b/546154457): Remove once all callers are migrated to LLVM containers.
  template <typename H>
  friend H AbslHashValue(H h, const BinaryAddressFallthrough& f) {
    return H::combine(std::move(h), f.from, f.to);
  }
  bool operator==(const BinaryAddressFallthrough& f) const {
    return from == f.from && to == f.to;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BinaryAddressFallthrough& f) {
    sink.Append(llvm::formatv("{0:x16}->{1:x16}", f.from, f.to).str());
  }
};

// The sentinel value for an invalid binary address.
inline constexpr uint64_t kInvalidBinaryAddress = static_cast<uint64_t>(-1);

// `DenseMapInfo` for binary addresses (`uint64_t`).
//
// Older LLVM versions (such as the one used by the open-source build) reserve
// an empty key and a tombstone key in `llvm::DenseMap`. The default
// `llvm::DenseMapInfo<uint64_t>` uses `~0ULL` as the empty key, which is the
// same value as `kInvalidBinaryAddress`. `kInvalidBinaryAddress` can be a
// valid key (for example, as a branch endpoint), so use this `DenseMapInfo`
// for containers of binary addresses. It reserves `kInvalidBinaryAddress - 1`
// and `kInvalidBinaryAddress - 2` instead, which are never valid addresses.
// Newer LLVM versions do not reserve keys and ignore these two functions.
struct BinaryAddressDenseMapInfo {
  static constexpr uint64_t getEmptyKey() { return kInvalidBinaryAddress - 1; }
  static constexpr uint64_t getTombstoneKey() {
    return kInvalidBinaryAddress - 2;
  }
  static unsigned getHashValue(uint64_t address) {
    return static_cast<unsigned>(llvm::hash_value(address));
  }
  static bool isEqual(uint64_t lhs, uint64_t rhs) { return lhs == rhs; }
};

}  // namespace propeller

namespace llvm {
// The empty and tombstone keys use the `BinaryAddressDenseMapInfo` sentinels,
// so that keys that contain `kInvalidBinaryAddress` stay valid.
template <>
struct DenseMapInfo<propeller::BinaryAddressBranch> {
  static propeller::BinaryAddressBranch getEmptyKey() {
    return {propeller::BinaryAddressDenseMapInfo::getEmptyKey(),
            propeller::BinaryAddressDenseMapInfo::getEmptyKey()};
  }
  static propeller::BinaryAddressBranch getTombstoneKey() {
    return {propeller::BinaryAddressDenseMapInfo::getTombstoneKey(),
            propeller::BinaryAddressDenseMapInfo::getTombstoneKey()};
  }
  static unsigned getHashValue(const propeller::BinaryAddressBranch& b) {
    return static_cast<unsigned>(hash_value(b));
  }
  static bool isEqual(const propeller::BinaryAddressBranch& lhs,
                      const propeller::BinaryAddressBranch& rhs) {
    return lhs == rhs;
  }
};

template <>
struct DenseMapInfo<propeller::BinaryAddressNotTakenBranch> {
  static propeller::BinaryAddressNotTakenBranch getEmptyKey() {
    return {propeller::BinaryAddressDenseMapInfo::getEmptyKey()};
  }
  static propeller::BinaryAddressNotTakenBranch getTombstoneKey() {
    return {propeller::BinaryAddressDenseMapInfo::getTombstoneKey()};
  }
  static unsigned getHashValue(
      const propeller::BinaryAddressNotTakenBranch& b) {
    return static_cast<unsigned>(hash_value(b));
  }
  static bool isEqual(const propeller::BinaryAddressNotTakenBranch& lhs,
                      const propeller::BinaryAddressNotTakenBranch& rhs) {
    return lhs == rhs;
  }
};

template <>
struct DenseMapInfo<propeller::BinaryAddressFallthrough> {
  static propeller::BinaryAddressFallthrough getEmptyKey() {
    return {propeller::BinaryAddressDenseMapInfo::getEmptyKey(),
            propeller::BinaryAddressDenseMapInfo::getEmptyKey()};
  }
  static propeller::BinaryAddressFallthrough getTombstoneKey() {
    return {propeller::BinaryAddressDenseMapInfo::getTombstoneKey(),
            propeller::BinaryAddressDenseMapInfo::getTombstoneKey()};
  }
  static unsigned getHashValue(const propeller::BinaryAddressFallthrough& f) {
    return static_cast<unsigned>(hash_value(f));
  }
  static bool isEqual(const propeller::BinaryAddressFallthrough& lhs,
                      const propeller::BinaryAddressFallthrough& rhs) {
    return lhs == rhs;
  }
};
}  // namespace llvm
#endif  // PROPELLER_BINARY_ADDRESS_BRANCH_H_
