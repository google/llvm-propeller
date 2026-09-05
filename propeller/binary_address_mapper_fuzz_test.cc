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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "llvm/Object/BBAddrMap.h"
#include "propeller/binary_address_branch.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_address_mapper.h"

// FuzzTest must be included after LLVM headers if they conflict with system
// headers.
#include "testing/fuzzing/fuzztest.h"

namespace propeller {
namespace {

struct FuzzedBBEntry {
  uint32_t id;
  uint32_t offset;
  uint32_t size;
  bool has_return;
  bool has_tail_call;
  bool can_fallthrough;
};

struct FuzzedBBRange {
  uint64_t base_address;
  std::vector<FuzzedBBEntry> entries;
};

struct FuzzedFunction {
  std::vector<FuzzedBBRange> ranges;
  std::string name;
};

void BinaryAddressMapperFuzzTest(std::vector<FuzzedFunction> functions,
                                 uint64_t query_address,
                                 BranchDirection direction) {
  if (functions.empty()) return;

  absl::btree_set<int> selected_functions;
  std::vector<llvm::object::BBAddrMap> bb_addr_maps;
  std::vector<BbHandle> bb_handles;
  absl::flat_hash_map<int, FunctionSymbolInfo> symbol_info_map;

  for (int i = 0; i < functions.size(); ++i) {
    selected_functions.insert(i);
    llvm::object::BBAddrMap addr_map;
    for (const auto& f_range : functions[i].ranges) {
      llvm::object::BBAddrMap::BBRangeEntry range_entry;
      range_entry.BaseAddress = f_range.base_address;
      for (const auto& f_entry : f_range.entries) {
        llvm::object::BBAddrMap::BBEntry::Metadata md = {
            .HasReturn = f_entry.has_return,
            .HasTailCall = f_entry.has_tail_call,
            .IsEHPad = false,
            .CanFallThrough = f_entry.can_fallthrough,
            .HasIndirectBranch = false};
        range_entry.BBEntries.emplace_back(f_entry.id, f_entry.offset,
                                           f_entry.size, md,
                                           llvm::SmallVector<uint32_t, 1>{}, 0);
        bb_handles.push_back(
            {.function_index = i,
             .range_index = static_cast<int>(addr_map.BBRanges.size()),
             .bb_index = static_cast<int>(range_entry.BBEntries.size() - 1)});
      }
      addr_map.BBRanges.push_back(std::move(range_entry));
    }
    bb_addr_maps.push_back(std::move(addr_map));
    symbol_info_map[i] = {.aliases = {functions[i].name}};
  }

  // Ensure bb_handles are sorted by address as required by BinaryAddressMapper.
  auto get_address = [&](const BbHandle& h) {
    return bb_addr_maps[h.function_index].BBRanges[h.range_index].BaseAddress +
           bb_addr_maps[h.function_index]
               .BBRanges[h.range_index]
               .BBEntries[h.bb_index]
               .Offset;
  };

  std::sort(bb_handles.begin(), bb_handles.end(),
            [&](const BbHandle& a, const BbHandle& b) {
              uint64_t addr_a = get_address(a);
              uint64_t addr_b = get_address(b);
              if (addr_a != addr_b) return addr_a < addr_b;
              // For zero-sized blocks at the same address, we need a stable
              // sort.
              if (a.function_index != b.function_index)
                return a.function_index < b.function_index;
              if (a.range_index != b.range_index)
                return a.range_index < b.range_index;
              return a.bb_index < b.bb_index;
            });

  BinaryAddressMapper mapper(std::move(selected_functions),
                             std::move(bb_addr_maps), std::move(bb_handles),
                             std::move(symbol_info_map));

  mapper.FindBbHandleIndexUsingBinaryAddress(query_address, direction);
}

// IntraFunctionPathsExtractor Fuzz Test
void IntraFunctionPathsExtractorFuzzTest(
    std::vector<FuzzedFunction> functions,
    std::vector<std::pair<uint64_t, uint64_t>> branches) {
  if (functions.empty()) return;

  absl::btree_set<int> selected_functions;
  std::vector<llvm::object::BBAddrMap> bb_addr_maps;
  std::vector<BbHandle> bb_handles;
  absl::flat_hash_map<int, FunctionSymbolInfo> symbol_info_map;

  for (int i = 0; i < functions.size(); ++i) {
    selected_functions.insert(i);
    llvm::object::BBAddrMap addr_map;
    for (const auto& f_range : functions[i].ranges) {
      llvm::object::BBAddrMap::BBRangeEntry range_entry;
      range_entry.BaseAddress = f_range.base_address;
      for (const auto& f_entry : f_range.entries) {
        llvm::object::BBAddrMap::BBEntry::Metadata md = {
            .HasReturn = f_entry.has_return,
            .HasTailCall = f_entry.has_tail_call,
            .IsEHPad = false,
            .CanFallThrough = f_entry.can_fallthrough,
            .HasIndirectBranch = false};
        range_entry.BBEntries.emplace_back(f_entry.id, f_entry.offset,
                                           f_entry.size, md,
                                           llvm::SmallVector<uint32_t, 1>{}, 0);
        bb_handles.push_back(
            {.function_index = i,
             .range_index = static_cast<int>(addr_map.BBRanges.size()),
             .bb_index = static_cast<int>(range_entry.BBEntries.size() - 1)});
      }
      addr_map.BBRanges.push_back(std::move(range_entry));
    }
    bb_addr_maps.push_back(std::move(addr_map));
    symbol_info_map[i] = {.aliases = {functions[i].name}};
  }

  auto get_address = [&](const BbHandle& h) {
    return bb_addr_maps[h.function_index].BBRanges[h.range_index].BaseAddress +
           bb_addr_maps[h.function_index]
               .BBRanges[h.range_index]
               .BBEntries[h.bb_index]
               .Offset;
  };

  std::sort(bb_handles.begin(), bb_handles.end(),
            [&](const BbHandle& a, const BbHandle& b) {
              uint64_t addr_a = get_address(a);
              uint64_t addr_b = get_address(b);
              if (addr_a != addr_b) return addr_a < addr_b;
              if (a.function_index != b.function_index)
                return a.function_index < b.function_index;
              if (a.range_index != b.range_index)
                return a.range_index < b.range_index;
              return a.bb_index < b.bb_index;
            });

  BinaryAddressMapper mapper(std::move(selected_functions),
                             std::move(bb_addr_maps), std::move(bb_handles),
                             std::move(symbol_info_map));

  BinaryAddressBranchPath path;
  path.pid = 1234;
  path.sample_time = absl::Now();
  for (const auto& branch : branches) {
    path.branches.push_back({branch.first, branch.second});
  }

  mapper.ExtractIntraFunctionPaths(path);
}

auto AnyBBEntry() {
  return fuzztest::Map(
      [](uint32_t id, uint32_t offset, uint32_t size, bool has_return,
         bool has_tail_call, bool can_fallthrough) {
        return FuzzedBBEntry{id,         offset,        size,
                             has_return, has_tail_call, can_fallthrough};
      },
      fuzztest::Arbitrary<uint32_t>(), fuzztest::Arbitrary<uint32_t>(),
      fuzztest::Arbitrary<uint32_t>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>());
}

auto AnyBBRange() {
  return fuzztest::Map(
      [](uint64_t base_address, std::vector<FuzzedBBEntry> entries) {
        return FuzzedBBRange{base_address, entries};
      },
      fuzztest::Arbitrary<uint64_t>(), fuzztest::VectorOf(AnyBBEntry()));
}

auto AnyFunction() {
  return fuzztest::Map(
      [](std::vector<FuzzedBBRange> ranges, std::string name) {
        return FuzzedFunction{ranges, name};
      },
      fuzztest::VectorOf(AnyBBRange()).WithMinSize(1),
      fuzztest::Arbitrary<std::string>());
}

FUZZ_TEST(BinaryAddressMapperFuzz, BinaryAddressMapperFuzzTest)
    .WithDomains(fuzztest::VectorOf(AnyFunction()),
                 fuzztest::Arbitrary<uint64_t>(),
                 fuzztest::ElementOf({BranchDirection::kFrom,
                                      BranchDirection::kTo}));

FUZZ_TEST(BinaryAddressMapperFuzz, IntraFunctionPathsExtractorFuzzTest)
    .WithDomains(
        fuzztest::VectorOf(AnyFunction()),
        fuzztest::VectorOf(fuzztest::PairOf(fuzztest::Arbitrary<uint64_t>(),
                                            fuzztest::Arbitrary<uint64_t>())));

}  // namespace
}  // namespace propeller
