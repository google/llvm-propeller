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

#include "propeller/binary_address_branch.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;

// `kInvalidBinaryAddress` (`UINT64_MAX`) legitimately appears as a branch
// endpoint, so it must be usable as a key in LLVM containers.
TEST(BinaryAddressDenseMapInfoTest, AcceptsInvalidBinaryAddressAsKey) {
  llvm::DenseSet<uint64_t> addresses;
  addresses.insert(kInvalidBinaryAddress);
  addresses.insert(0x1000);
  EXPECT_TRUE(addresses.contains(kInvalidBinaryAddress));
  EXPECT_TRUE(addresses.contains(0x1000));
  EXPECT_FALSE(addresses.contains(0x2000));
}

TEST(BinaryAddressBranchDenseMapInfoTest, AcceptsInvalidBinaryAddressInKey) {
  llvm::DenseMap<BinaryAddressBranch, int64_t> counters;
  counters[{.from = kInvalidBinaryAddress, .to = kInvalidBinaryAddress}] += 1;
  counters[{.from = kInvalidBinaryAddress, .to = 0x10}] += 2;
  counters[{.from = 0x20, .to = kInvalidBinaryAddress}] += 3;
  counters[{.from = 0x20, .to = kInvalidBinaryAddress}] += 4;
  EXPECT_THAT(
      counters,
      UnorderedElementsAre(
          Pair(BinaryAddressBranch{.from = kInvalidBinaryAddress,
                                   .to = kInvalidBinaryAddress},
               1),
          Pair(BinaryAddressBranch{.from = kInvalidBinaryAddress, .to = 0x10},
               2),
          Pair(BinaryAddressBranch{.from = 0x20, .to = kInvalidBinaryAddress},
               7)));
}

TEST(BinaryAddressNotTakenBranchDenseMapInfoTest,
     AcceptsInvalidBinaryAddressInKey) {
  llvm::DenseMap<BinaryAddressNotTakenBranch, int64_t> counters;
  counters[{.address = kInvalidBinaryAddress}] += 1;
  counters[{.address = 0x30}] += 2;
  EXPECT_EQ(counters.lookup({.address = kInvalidBinaryAddress}), 1);
  EXPECT_EQ(counters.lookup({.address = 0x30}), 2);
}

TEST(BinaryAddressFallthroughDenseMapInfoTest,
     AcceptsInvalidBinaryAddressInKey) {
  llvm::DenseMap<BinaryAddressFallthrough, int64_t> counters;
  counters[{.from = kInvalidBinaryAddress, .to = kInvalidBinaryAddress}] += 1;
  counters[{.from = 0x40, .to = 0x50}] += 2;
  EXPECT_EQ(counters.lookup(
                {.from = kInvalidBinaryAddress, .to = kInvalidBinaryAddress}),
            1);
  EXPECT_EQ(counters.lookup({.from = 0x40, .to = 0x50}), 2);
}

}  // namespace
}  // namespace propeller
