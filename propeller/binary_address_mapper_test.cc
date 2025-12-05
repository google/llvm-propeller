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

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/bb_handle.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_content.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::ResultOf;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

using ::llvm::object::BBAddrMap;

MATCHER_P2(BbAddrMapIs, function_address_matcher, bb_ranges_matcher, "") {
  return ExplainMatchResult(function_address_matcher, arg.getFunctionAddress(),
                            result_listener) &&
         ExplainMatchResult(bb_ranges_matcher, arg.getBBRanges(),
                            result_listener);
}

MATCHER_P2(BbRangeIs, base_address_matcher, bb_entries_matcher, "") {
  // TODO(rahmanl): Introduce accessor functions for BBRangeEntry upstream.
  return ExplainMatchResult(base_address_matcher, arg.BaseAddress,
                            result_listener) &&
         ExplainMatchResult(bb_entries_matcher, arg.BBEntries, result_listener);
}

MATCHER_P4(BbEntryIs, id_matcher, offset_matcher, size_matcher,
           metadata_matcher, "") {
  return ExplainMatchResult(id_matcher, arg.ID, result_listener) &&
         ExplainMatchResult(offset_matcher, arg.Offset, result_listener) &&
         ExplainMatchResult(size_matcher, arg.Size, result_listener) &&
         ExplainMatchResult(metadata_matcher, arg.MD, result_listener);
}

std::string GetPropellerTestDataFilePath(absl::string_view filename) {
  const std::string testdata_filepath =
      absl::StrCat(::testing::SrcDir(), "_main/propeller/testdata/", filename);
  return testdata_filepath;
}

static llvm::DenseMap<llvm::StringRef, BBAddrMap> GetBBAddrMapByFunctionName(
    const BinaryAddressMapper& binary_address_mapper) {
  llvm::DenseMap<llvm::StringRef, BBAddrMap> bb_addr_map_by_func_name;
  for (const auto& [function_index, symbol_info] :
       binary_address_mapper.symbol_info_map()) {
    for (llvm::StringRef alias : symbol_info.aliases)
      bb_addr_map_by_func_name.insert(
          {alias, binary_address_mapper.bb_addr_map()[function_index]});
  }
  return bb_addr_map_by_func_name;
}

TEST(BinaryAddressMapper, BbAddrMapExist) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->bb_addr_map(), Not(IsEmpty()));
}

TEST(BinaryAddressMapper, BbAddrMapReadSymbolTable) {
  ASSERT_OK_AND_ASSIGN(
      auto binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(
      binary_address_mapper->symbol_info_map(),
      Contains(Pair(_, FieldsAre(ElementsAre("sample1_func"), ".text"))));
}

TEST(BinaryAddressMapper, SkipEntryIfSymbolNotInSymtab) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(GetPropellerTestDataFilePath(
                           "sample_with_dropped_symbol.bin")));
  PropellerStats stats;
  PropellerOptions options;

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  EXPECT_EQ(stats.bbaddrmap_stats.bbaddrmap_function_does_not_have_symtab_entry,
            1);
}

// Tests reading the BBAddrMap from a binary built with MFS which has basic
// block sections.
TEST(BinaryAddressMapper, ReadsMfsBbAddrMap) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample_mfs.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  EXPECT_THAT(
      GetBBAddrMapByFunctionName(*binary_address_mapper),
      Contains(Pair(
          "compute",
          BbAddrMapIs(
              0x1790,
              ElementsAre(
                  BbRangeIs(0x1790, ElementsAre(BbEntryIs(0, 0x0, 0x1D, _),
                                                BbEntryIs(3, 0x20, 0x3B, _))),
                  BbRangeIs(0x18c8,
                            ElementsAre(BbEntryIs(1, 0x0, 0xE, _),
                                        BbEntryIs(5, 0xE, 0x7, _),
                                        BbEntryIs(2, 0x15, 0x9, _),
                                        BbEntryIs(4, 0x1E, 0x33, _))))))));
}

// Tests computing the flat bb index in the entire function from a bb handle and
// vice versa.
TEST(BinaryAddressMapper, HandlesFlatBbIndex) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample_mfs.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  ASSERT_THAT(
      binary_address_mapper->bb_addr_map(),
      ElementsAre(_, BbAddrMapIs(_, ElementsAre(BbRangeIs(_, SizeIs(3)))),
                  BbAddrMapIs(_, ElementsAre(BbRangeIs(_, SizeIs(2)),
                                             BbRangeIs(_, SizeIs(4)))),
                  _));
  EXPECT_THAT(
      binary_address_mapper->GetBbHandle(
          {.function_index = 2, .flat_bb_index = 1}),
      Optional(BbHandle{.function_index = 2, .range_index = 0, .bb_index = 1}));
  EXPECT_THAT(
      binary_address_mapper->GetBbHandle(
          {.function_index = 2, .flat_bb_index = 2}),
      Optional(BbHandle{.function_index = 2, .range_index = 1, .bb_index = 0}));
  EXPECT_THAT(binary_address_mapper->GetBbHandle(
                  {.function_index = 2, .flat_bb_index = 6}),
              Eq(std::nullopt));
  EXPECT_THAT(
      binary_address_mapper->GetBbHandle(
          {.function_index = 1, .flat_bb_index = 2}),
      Optional(BbHandle{.function_index = 1, .range_index = 0, .bb_index = 2}));
  EXPECT_THAT(binary_address_mapper->GetBbHandle(
                  {.function_index = 1, .flat_bb_index = 3}),
              Eq(std::nullopt));
  EXPECT_THAT(binary_address_mapper->GetFlatBbHandle(BbHandle{
                  .function_index = 2, .range_index = 0, .bb_index = 1}),
              Optional(FlatBbHandle{.function_index = 2, .flat_bb_index = 1}));
  EXPECT_THAT(binary_address_mapper->GetFlatBbHandle(BbHandle{
                  .function_index = 2, .range_index = 1, .bb_index = 0}),
              Optional(FlatBbHandle{.function_index = 2, .flat_bb_index = 2}));
  EXPECT_EQ(binary_address_mapper->GetFlatBbHandle(
                BbHandle{.function_index = 2, .range_index = 1, .bb_index = 4}),
            std::nullopt);
  EXPECT_THAT(binary_address_mapper->GetFlatBbHandle(BbHandle{
                  .function_index = 1, .range_index = 0, .bb_index = 2}),
              Optional(FlatBbHandle{.function_index = 1, .flat_bb_index = 2}));
  EXPECT_EQ(binary_address_mapper->GetFlatBbHandle(
                BbHandle{.function_index = 1, .range_index = 0, .bb_index = 3}),
            std::nullopt);
  EXPECT_EQ(binary_address_mapper->GetFlatBbHandle(
                BbHandle{.function_index = 5, .range_index = 0, .bb_index = 0}),
            std::nullopt);
}

TEST(BinaryAddressMapper, ReadBbAddrMap) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  auto bb_addr_map_by_func_name =
      GetBBAddrMapByFunctionName(*binary_address_mapper);
  EXPECT_THAT(bb_addr_map_by_func_name,
              Contains(Pair("compute_flag", BbAddrMapIs(_, Not(IsEmpty())))));
  // Regenerating sample.bin may trigger a change here.
  // Use `llvm-readobj --bb-addr-map sample.bin` to capture the
  // expected data.
  EXPECT_THAT(
      bb_addr_map_by_func_name,
      UnorderedElementsAre(
          Pair("main",
               BbAddrMapIs(
                   0x17C0,
                   ElementsAre(BbRangeIs(
                       0x17C0,
                       ElementsAre(BbEntryIs(0, 0x0, 0x29,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false}),
                                   BbEntryIs(5, 0x30, 0xE,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(1, 0x3E, 0x11,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(2, 0x4F, 0x2B,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(3, 0x7A, 0x2A,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(4, 0xA4, 0x24,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false}),
                                   BbEntryIs(6, 0xC8, 0x9,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = true,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false})))))),
          Pair("sample1_func",
               BbAddrMapIs(0x17B0,
                           ElementsAre(BbRangeIs(
                               0x17B0, ElementsAre(BbEntryIs(
                                           0, 0x0, 0x6,
                                           BBAddrMap::BBEntry::Metadata{
                                               .HasReturn = true,
                                               .HasTailCall = false,
                                               .IsEHPad = false,
                                               .CanFallThrough = false})))))),
          Pair("compute_flag",
               BbAddrMapIs(0x1780,
                           ElementsAre(BbRangeIs(
                               0x1780, ElementsAre(BbEntryIs(
                                           0, 0x0, 0x2B,
                                           BBAddrMap::BBEntry::Metadata{
                                               .HasReturn = true,
                                               .HasTailCall = false,
                                               .IsEHPad = false,
                                               .CanFallThrough = false})))))),
          Pair("this_is_very_code",
               BbAddrMapIs(0x1730,
                           ElementsAre(BbRangeIs(
                               0x1730, ElementsAre(BbEntryIs(
                                           0, 0x0, 0x50,
                                           BBAddrMap::BBEntry::Metadata{
                                               .HasReturn = true,
                                               .HasTailCall = false,
                                               .IsEHPad = false,
                                               .CanFallThrough = false}))))))));
}

TEST(BinaryAddressMapper, DuplicateSymbolsDropped) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("duplicate_symbols.bin")));
  PropellerStats stats;
  PropellerOptions options;

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  // Multiple symbols have the "sample1_func1" name hence none of them will be
  // kept. Other functions are not affected.
  EXPECT_THAT(
      GetBBAddrMapByFunctionName(*binary_address_mapper),
      AllOf(Not(Contains(Key("sample1_func"))),
            Contains(Pair("compute_flag", BbAddrMapIs(_, Not(IsEmpty()))))));
  EXPECT_EQ(stats.bbaddrmap_stats.duplicate_symbols, 1);
}

TEST(BinaryAddressMapper, NoneDotTextSymbolsDropped) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("sample_section.bin")));
  PropellerStats stats;
  PropellerOptions options;

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  // "anycall" is inside ".anycall.anysection", so it should not be processed by
  // propeller. ".text.unlikely" function symbols are processed. Other functions
  // are not affected.
  EXPECT_THAT(
      GetBBAddrMapByFunctionName(*binary_address_mapper),
      AllOf(Not(Contains(Key("anycall"))),
            Contains(Pair("unlikelycall", BbAddrMapIs(_, Not(IsEmpty())))),
            Contains(Pair("compute_flag", BbAddrMapIs(_, Not(IsEmpty()))))));
}

TEST(BinaryAddressMapper, NonDotTextSymbolsKept) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("sample_section.bin")));
  PropellerStats stats;
  PropellerOptions options;
  options.set_filter_non_text_functions(false);

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  // Check that all functions are processed regardless of their section name.
  EXPECT_THAT(
      GetBBAddrMapByFunctionName(*binary_address_mapper),
      AllOf(Contains(Pair("anycall", BbAddrMapIs(_, Not(IsEmpty())))),
            Contains(Pair("unlikelycall", BbAddrMapIs(_, Not(IsEmpty())))),
            Contains(Pair("compute_flag", BbAddrMapIs(_, Not(IsEmpty()))))));
}

TEST(BinaryAddressMapper, DuplicateUniqNames) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(GetPropellerTestDataFilePath(
                           "duplicate_unique_names.out")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));

  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  // We have 3 duplicated symbols, the last 2 are marked as duplicate_symbols.
  // 11: 0000000000001880     6 FUNC    LOCAL  DEFAULT   14
  //                     _ZL3foov.__uniq.148988607218547176184555965669372770545
  // 13: 00000000000018a0     6 FUNC    LOCAL  DEFAULT   1
  //                     _ZL3foov.__uniq.148988607218547176184555965669372770545
  // 15: 00000000000018f0     6 FUNC    LOCAL  DEFAULT   14
  //                     _ZL3foov.__uniq.148988607218547176184555965669372770545
  EXPECT_EQ(stats.bbaddrmap_stats.duplicate_symbols, 2);
}

TEST(BinaryAddressMapper, CheckNoHotFunctions) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("sample_section.bin")));
  llvm::DenseSet<uint64_t> hot_addresses = {// call from main to compute_flag.
                                            0x201900, 0x201870};

  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               &hot_addresses));

  // main is hot and sample1_func is cold.
  EXPECT_THAT(GetBBAddrMapByFunctionName(*binary_address_mapper),
              AllOf(Contains(Pair("main", BbAddrMapIs(_, Not(IsEmpty())))),
                    Not(Contains(Key("sample1_func")))));
}

TEST(BinaryAddressMapper, FindBbHandleIndexUsingBinaryAddress) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("special_case.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  auto bb_index_from_handle_index = [&](int index) {
    return binary_address_mapper->bb_handles()[index].bb_index;
  };

  // At address 0x201620 we have an empty block followed by a non-empty block.
  // With BranchDirection::kTo, the address should be mapped to BB3.
  // With BranchDirection::kFrom, the address should be mapped to BB4.
  //
  // <BB1>:
  //   201610: 0f af c0                      imull   %eax, %eax
  //   201613: 83 f8 03                      cmpl    $0x3, %eax
  //   201616: 72 08                         jb       <BB3>
  //   201618: 0f 1f 84 00 00 00 00 00       nopl    (%rax,%rax)
  // <BB3>:
  // <BB4>:
  //   **201620**: c3                            retq
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201620, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 3)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201620, BranchDirection::kFrom),
              Optional(ResultOf(bb_index_from_handle_index, 4)));

  // With BranchDirection::kFrom, 0x201616 should be mapped to BB1 and 0x201618
  // should be rejected because it is outside of the basic block.
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201616, BranchDirection::kFrom),
              Optional(ResultOf(bb_index_from_handle_index, 2)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201618, BranchDirection::kFrom),
              Eq(std::nullopt));

  // At address 0x201649, we have a call instruction followed by nopl. The
  // return from the callee will branch to 0x20164e (the address of the nopl
  // instruction). So with BranchDirection::kTo 0x20164e should be mapped to BB2
  // and with BranchDirection::kFrom it should be mapped to std::nullopt
  // (rejected).
  //
  //   201649: e8 a2 ff ff ff                callq    <foo>
  //   20164e: 66 90                         nop
  // <BB2>:
  //   201650: 89 d8                         movl    %ebx, %eax
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x20164e, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 1)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x20164e, BranchDirection::kFrom),
              Eq(std::nullopt));
  // 0x201650 should be mapped to BB2 regardless of the direction.
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201650, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 2)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201650, BranchDirection::kFrom),
              Optional(ResultOf(bb_index_from_handle_index, 2)));
}

TEST(BinaryAddressMapper, ExtractsIntraFunctionPaths) {
  BinaryAddressBranchPath path({.pid = 2080799,
                                .sample_time = absl::FromUnixSeconds(123456),
                                .branches = {{0x189a, 0x1770},
                                             {0x17bf, 0x189f},
                                             {0x18c4, 0x1890},
                                             {0x189a, 0x1770},
                                             {0x17bf, 0x189f},
                                             {0x18cc, 0x18fa},
                                             {0x18fc, 0x18f0},
                                             {0x18f5, 0x1820},
                                             {0x1832, 0x1878},
                                             {0x189a, 0x1770}}});
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  EXPECT_THAT(
      binary_address_mapper->ExtractIntraFunctionPaths(path),
      ElementsAre(
          FlatBbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches =
                   {{.from_bb = {{.function_index = 2, .flat_bb_index = 4}},
                     .to_bb = {{.function_index = 2, .flat_bb_index = 4}},
                     .call_rets = {{.callee = 0,
                                    .return_bb = {{.function_index = 0,
                                                   .flat_bb_index = 0}}}}},
                    {.from_bb = {{.function_index = 2, .flat_bb_index = 4}},
                     .to_bb = {{.function_index = 2, .flat_bb_index = 4}}},
                    {.from_bb = {{.function_index = 2, .flat_bb_index = 4}},
                     .to_bb = {{.function_index = 2, .flat_bb_index = 4}},
                     .call_rets = {{.callee = 0,
                                    .return_bb = {{.function_index = 0,
                                                   .flat_bb_index = 0}}}}},
                    {.from_bb = {{.function_index = 2, .flat_bb_index = 5}}}},
               .returns_to = {{.function_index = 3, .flat_bb_index = 1}}}),
          FlatBbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches =
                   {{.to_bb = {{.function_index = 0, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 0, .flat_bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .flat_bb_index = 4}}}),
          FlatBbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches =
                   {{.to_bb = {{.function_index = 0, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 0, .flat_bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .flat_bb_index = 4}}}),
          FlatBbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches =
                   {{.to_bb = {{.function_index = 3, .flat_bb_index = 1}},
                     .call_rets = {{.callee = std::nullopt,
                                    .return_bb = {{.function_index = 2,
                                                   .flat_bb_index = 5}}}}},
                    {.from_bb = {{.function_index = 3, .flat_bb_index = 1}},
                     .to_bb = {{.function_index = 3, .flat_bb_index = 1}}},
                    {.from_bb = {{.function_index = 3, .flat_bb_index = 1}},
                     .call_rets = {{.callee = 2,
                                    .return_bb = std::nullopt}}}}}),
          FlatBbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches =
                   {{.to_bb = {{.function_index = 2, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 2, .flat_bb_index = 0}},
                     .to_bb = {{.function_index = 2, .flat_bb_index = 3}}},
                    {.from_bb = {{.function_index = 2, .flat_bb_index = 4}},
                     .call_rets = {{.callee = 0,
                                    .return_bb = std::nullopt}}}}}),
          FlatBbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches = {
                   {.to_bb = {{.function_index = 0, .flat_bb_index = 0}}}}})));
}

TEST(BinaryAddressMapper, ExtractsPathsWithReturnsFromUnknown) {
  BinaryAddressBranchPath path(
      {.pid = 123456, .branches = {{0x189a, 0xFFFFF0}, {0xFFFFFF, 0x189f}}});
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  EXPECT_THAT(
      binary_address_mapper->ExtractIntraFunctionPaths(path),
      ElementsAre(FlatBbHandleBranchPath(
          {.pid = 123456,
           .branches = {{.from_bb = {{.function_index = 2, .flat_bb_index = 4}},
                         .to_bb = {{.function_index = 2, .flat_bb_index = 4}},
                         .call_rets = {{}}}}})));
}

TEST(BinaryAddressMapper, ExtractsPathsWithReturnsToBasicBlockAddress) {
  BinaryAddressBranchPath path(
      {.pid = 123456, .branches = {{0x18cc, 0x18fa}, {0x1906, 0xFFFFFF}}});
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  EXPECT_THAT(
      binary_address_mapper->ExtractIntraFunctionPaths(path),
      ElementsAre(
          FlatBbHandleBranchPath(
              {.pid = 123456,
               .branches = {{.from_bb = {{.function_index = 2,
                                          .flat_bb_index = 5}}}},
               .returns_to = {{.function_index = 3, .flat_bb_index = 1}}}),
          FlatBbHandleBranchPath(
              {.pid = 123456,
               .branches = {
                   {.to_bb = {{.function_index = 3, .flat_bb_index = 1}},
                    .call_rets = {{.return_bb = {{.function_index = 2,
                                                  .flat_bb_index = 5}}}}},
                   {.from_bb = {
                        {.function_index = 3, .flat_bb_index = 2}}}}})));
}

TEST(BinaryAddressMapper, ExtractPathsSeparatesPathsWithCorruptBranches) {
  BinaryAddressBranchPath path(
      {.pid = 123456, .branches = {{0x189a, 0xFFFFF0}, {0x18c4, 0x1890}}});
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  EXPECT_THAT(
      binary_address_mapper->ExtractIntraFunctionPaths(path),
      ElementsAre(
          FlatBbHandleBranchPath(
              {.pid = 123456,
               .branches = {{.from_bb = {{.function_index = 2,
                                          .flat_bb_index = 4}}}}}),
          FlatBbHandleBranchPath(
              {.pid = 123456,
               .branches = {
                   {.from_bb = {{.function_index = 2, .flat_bb_index = 4}},
                    .to_bb = {{.function_index = 2, .flat_bb_index = 4}}}}})));
}

TEST(BinaryAddressMapper, ExtractPathsCoalescesCallees) {
  BinaryAddressBranchPath path = {.pid = 7654321,
                                  .branches = {{0x1840, 0xFFFFF0},
                                               {0xFFFFF2, 0x1844},
                                               {0x1845, 0x1790},
                                               {0x17df, 0x1849},
                                               {0x184a, 0x17e0},
                                               {0x1833, 0x184e}}};
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("bimodal_sample.x.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  EXPECT_THAT(
      binary_address_mapper->ExtractIntraFunctionPaths(path),
      ElementsAre(
          FlatBbHandleBranchPath(
              {.pid = 7654321,
               .branches =
                   {{.from_bb =
                         {
                             {.function_index = 2, .flat_bb_index = 0},
                         },
                     .to_bb = {{.function_index = 2, .flat_bb_index = 0}},
                     .call_rets = {{},
                                   {.callee = 0,
                                    .return_bb = {{.function_index = 0,
                                                   .flat_bb_index = 0}}},
                                   {.callee = 1,
                                    .return_bb = {{.function_index = 1,
                                                   .flat_bb_index = 0}}}}}}}),
          FlatBbHandleBranchPath(
              {.pid = 7654321,
               .branches =
                   {{.to_bb = {{.function_index = 0, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 0, .flat_bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .flat_bb_index = 0}}}),
          FlatBbHandleBranchPath(
              {.pid = 7654321,
               .branches =
                   {{.to_bb = {{.function_index = 1, .flat_bb_index = 0}}},
                    {.from_bb = {{.function_index = 1, .flat_bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .flat_bb_index = 0}}})));
}
}  // namespace
}  // namespace propeller
