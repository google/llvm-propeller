#include "propeller/binary_address_mapper.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "propeller/binary_address_branch.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregation.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

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

MATCHER_P4(BbEntryIs, id, offset, size, metadata, "") {
  return arg.ID == id && arg.Offset == offset && arg.Size == size &&
         arg.MD == metadata;
}

std::string GetPropellerTestDataFilePath(absl::string_view filename) {
  const std::string testdata_filepath =
      absl::StrCat(::testing::SrcDir(),
                   "_main/propeller/testdata/", filename);
  return testdata_filepath;
}

static absl::flat_hash_map<llvm::StringRef, BBAddrMap>
GetBBAddrMapByFunctionName(const BinaryAddressMapper &binary_address_mapper) {
  absl::flat_hash_map<llvm::StringRef, BBAddrMap> bb_addr_map_by_func_name;
  for (const auto &[function_index, symbol_info] :
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
                   0x1820,
                   ElementsAre(BbRangeIs(
                       0x1820,
                       ElementsAre(BbEntryIs(0, 0x0, 0x30,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(1, 0x30, 0xD,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(2, 0x3D, 0x24,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(3, 0x61, 0x2E,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(4, 0x8F, 0x1A,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(5, 0xA9, 0x34,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(6, 0xDD, 0x5,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(7, 0xE2, 0xE,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false}),
                                   BbEntryIs(8, 0xF0, 0x8,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = true,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false})))))),
          Pair("sample1_func",
               BbAddrMapIs(0x1810,
                           ElementsAre(BbRangeIs(
                               0x1810, ElementsAre(BbEntryIs(
                                           0, 0x0, 0x6,
                                           BBAddrMap::BBEntry::Metadata{
                                               .HasReturn = true,
                                               .HasTailCall = false,
                                               .IsEHPad = false,
                                               .CanFallThrough = false})))))),
          Pair("compute_flag",
               BbAddrMapIs(
                   0x17D0,
                   ElementsAre(BbRangeIs(
                       0x17D0,
                       ElementsAre(BbEntryIs(0, 0x0, 0x19,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(1, 0x19, 0x10,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false}),
                                   BbEntryIs(2, 0x29, 0x8,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = false,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = true}),
                                   BbEntryIs(3, 0x31, 0x5,
                                             BBAddrMap::BBEntry::Metadata{
                                                 .HasReturn = true,
                                                 .HasTailCall = false,
                                                 .IsEHPad = false,
                                                 .CanFallThrough = false})))))),
          Pair("this_is_very_code",
               BbAddrMapIs(0x1770,
                           ElementsAre(BbRangeIs(
                               0x1770, ElementsAre(BbEntryIs(
                                           0, 0x0, 0x5D,
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
  absl::flat_hash_set<uint64_t> hot_addresses = {
      // call from main to compute_flag.
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
      GetBinaryContent(GetPropellerTestDataFilePath("clang_v0_labels.binary")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  // At address 0x000001b3d0a8, we have the following symbols all of size zero.
  //   BB.447 BB.448 BB.449 BB.450 BB.451 BB.452 BB.453 BB.454 BB.455
  //   BB.456 BB.457 BB.458 BB.459 BB.460

  auto bb_index_from_handle_index = [&](int index) {
    return binary_address_mapper->bb_handles()[index].bb_index;
  };
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x1b3d0a8, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 447)));
  // At address 0x000001b3f5b0: we have the following symbols:
  //   Func<_ZN5clang18CompilerInvocation14CreateFromArgs...> BB.0 {size: 0x9a}
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x1b3f5b0, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 0)));
  // At address 0x1e63500: we have the following symbols:
  //   Func<_ZN4llvm22FoldingSetIteratorImplC2EPPv> BB.0 {size: 0}
  //                                                BB.1 {size: 0x8}
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x1e63500, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 0)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x1e63500, BranchDirection::kFrom),
              Optional(ResultOf(bb_index_from_handle_index, 1)));
  // At address 0x45399d0, we have a call instruction followed by nops. The
  // return from the callee will branch to 0x45399d5 (the address of the nopw).
  // So with BranchDirection::kTo 0x45399d5 should be mapped to BB21 and with
  // BranchDirection::kFrom it should be mapped to std::nullopt (rejected).
  //
  // <BB21>:
  //  ...
  //  45399d0:   callq   <_ZN4llvm22report_bad_alloc_errorEPKcb>
  //  45399d5:   nopw    %cs:(%rax,%rax)
  //  45399df:   nop
  // <BB22>:
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x45399d5, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 21)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x45399d5, BranchDirection::kFrom),
              Eq(std::nullopt));
}

TEST(BinaryAddressMapper, ExtractsIntraFunctionPaths) {
  BinaryAddressBranchPath path({.pid = 2080799,
                                .sample_time = absl::FromUnixSeconds(123456),
                                .branches = {{0x186a, 0x1730},
                                             {0x1782, 0x186f},
                                             {0x1897, 0x1860},
                                             {0x186a, 0x1730},
                                             {0x1782, 0x186f},
                                             {0x189f, 0x18ca},
                                             {0x18cc, 0x18c0},
                                             {0x18c5, 0x17f0},
                                             {0x1802, 0x184b},
                                             {0x186a, 0x1730}}});
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
          BbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches = {{.from_bb = {{.function_index = 2, .bb_index = 4}},
                             .to_bb = {{.function_index = 2, .bb_index = 4}},
                             .call_rets = {{.callee = 0,
                                            .return_bb = {{.function_index = 0,
                                                           .bb_index = 0}}}}},
                            {.from_bb = {{.function_index = 2, .bb_index = 4}},
                             .to_bb = {{.function_index = 2, .bb_index = 4}}},
                            {.from_bb = {{.function_index = 2, .bb_index = 4}},
                             .to_bb = {{.function_index = 2, .bb_index = 4}},
                             .call_rets = {{.callee = 0,
                                            .return_bb = {{.function_index = 0,
                                                           .bb_index = 0}}}}},
                            {.from_bb = {{.function_index = 2,
                                          .bb_index = 5}}}},
               .returns_to = {{.function_index = 3, .bb_index = 1}}}),
          BbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                            {.from_bb = {{.function_index = 0,
                                          .bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .bb_index = 4}}}),
          BbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                            {.from_bb = {{.function_index = 0,
                                          .bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .bb_index = 4}}}),
          BbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches = {{.to_bb = {{.function_index = 3, .bb_index = 1}},
                             .call_rets = {{.callee = std::nullopt,
                                            .return_bb = {{.function_index = 2,
                                                           .bb_index = 5}}}}},
                            {.from_bb = {{.function_index = 3, .bb_index = 1}},
                             .to_bb = {{.function_index = 3, .bb_index = 1}}},
                            {.from_bb = {{.function_index = 3, .bb_index = 1}},
                             .call_rets = {{.callee = 2,
                                            .return_bb = std::nullopt}}}}}),
          BbHandleBranchPath(
              {.pid = 2080799,
               .sample_time = absl::FromUnixSeconds(123456),
               .branches = {{.to_bb = {{.function_index = 2, .bb_index = 0}}},
                            {.from_bb = {{.function_index = 2, .bb_index = 0}},
                             .to_bb = {{.function_index = 2, .bb_index = 3}}},
                            {.from_bb = {{.function_index = 2, .bb_index = 4}},
                             .call_rets = {{.callee = 0,
                                            .return_bb = std::nullopt}}}}}),
          BbHandleBranchPath({.pid = 2080799,
                              .sample_time = absl::FromUnixSeconds(123456),
                              .branches = {{.to_bb = {{.function_index = 0,
                                                       .bb_index = 0}}}}})));
}

TEST(BinaryAddressMapper, ExtractsPathsWithReturnsFromUnknown) {
  BinaryAddressBranchPath path(
      {.pid = 123456, .branches = {{0x186a, 0xFFFFF0}, {0xFFFFFF, 0x186f}}});
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
      ElementsAre(BbHandleBranchPath(
          {.pid = 123456,
           .branches = {{.from_bb = {{.function_index = 2, .bb_index = 4}},
                         .to_bb = {{.function_index = 2, .bb_index = 4}},
                         .call_rets = {{}}}}})));
}

TEST(BinaryAddressMapper, ExtractsPathsWithReturnsToBasicBlockAddress) {
  BinaryAddressBranchPath path(
      {.pid = 123456, .branches = {{0x189f, 0x18ce}, {0x18d6, 0xFFFFFF}}});
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
          BbHandleBranchPath(
              {.pid = 123456,
               .branches = {{.from_bb = {{.function_index = 2,
                                          .bb_index = 5}}}},
               .returns_to = {{.function_index = 3, .bb_index = 1}}}),
          BbHandleBranchPath(
              {.pid = 123456,
               .branches = {
                   {.from_bb = {{.function_index = 3, .bb_index = 1}},
                    .to_bb = {{.function_index = 3, .bb_index = 2}},
                    .call_rets = {{.return_bb = {{.function_index = 2,
                                                  .bb_index = 5}}}}},
                   {.from_bb = {{.function_index = 3, .bb_index = 2}}}}})));
}

TEST(BinaryAddressMapper, ExtractPathsSeparatesPathsWithCorruptBranches) {
  BinaryAddressBranchPath path(
      {.pid = 123456, .branches = {{0x186a, 0xFFFFF0}, {0x1897, 0x1860}}});
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
          BbHandleBranchPath({.pid = 123456,
                              .branches = {{.from_bb = {{.function_index = 2,
                                                         .bb_index = 4}}}}}),
          BbHandleBranchPath(
              {.pid = 123456,
               .branches = {
                   {.from_bb = {{.function_index = 2, .bb_index = 4}},
                    .to_bb = {{.function_index = 2, .bb_index = 4}}}}})));
}

TEST(BinaryAddressMapper, ExtractPathsCoalescesCallees) {
  BinaryAddressBranchPath path = {.pid = 7654321,
                                  .branches = {{0x1832, 0xFFFFF0},
                                               {0xFFFFF2, 0x1834},
                                               {0x1836, 0x1770},
                                               {0x17c0, 0x1838},
                                               {0x1840, 0x17d0},
                                               {0x1820, 0x1842}}};
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
          BbHandleBranchPath(
              {.pid = 7654321,
               .branches = {{.from_bb =
                                 {
                                     {.function_index = 2, .bb_index = 0},
                                 },
                             .to_bb = {{.function_index = 2, .bb_index = 0}},
                             .call_rets = {{},
                                           {.callee = 0,
                                            .return_bb = {{.function_index = 0,
                                                           .bb_index = 0}}},
                                           {.callee = 1,
                                            .return_bb = {{.function_index = 1,
                                                           .bb_index =
                                                               0}}}}}}}),
          BbHandleBranchPath(
              {.pid = 7654321,
               .branches = {{.to_bb = {{.function_index = 0, .bb_index = 0}}},
                            {.from_bb = {{.function_index = 0,
                                          .bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .bb_index = 0}}}),
          BbHandleBranchPath(
              {.pid = 7654321,
               .branches = {{.to_bb = {{.function_index = 1, .bb_index = 0}}},
                            {.from_bb = {{.function_index = 1,
                                          .bb_index = 0}}}},
               .returns_to = {{.function_index = 2, .bb_index = 0}}})));
}

TEST(LlvmBinaryAddressMapper, GetThunkInfoUsingBinaryAddress) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("fake_thunks.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  // Match thunk address only
  EXPECT_THAT(binary_address_mapper->GetThunkInfoUsingBinaryAddress(0x107bc),
              Optional(FieldsAre(0x107bc, _, _)));
  EXPECT_THAT(binary_address_mapper->GetThunkInfoUsingBinaryAddress(0x107be),
              Optional(FieldsAre(0x107bc, _, _)));
  // Checks last byte of thunk is matched.
  EXPECT_THAT(binary_address_mapper->GetThunkInfoUsingBinaryAddress(0x107cb),
              Optional(FieldsAre(0x107bc, _, _)));

  EXPECT_THAT(binary_address_mapper->GetThunkInfoUsingBinaryAddress(0x107cc),
              Optional(FieldsAre(0x107cc, _, _)));
}

TEST(LlvmBinaryAddressMapper, FindThunkInfoIndexUsingBinaryAddress) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("fake_thunks.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));

  EXPECT_THAT(
      binary_address_mapper->FindThunkInfoIndexUsingBinaryAddress(0x107bc),
      Optional(0));
  EXPECT_THAT(
      binary_address_mapper->FindThunkInfoIndexUsingBinaryAddress(0x107be),
      Optional(0));

  EXPECT_THAT(
      binary_address_mapper->FindThunkInfoIndexUsingBinaryAddress(0x107cc),
      Optional(1));
}

TEST(LlvmBinaryAddressMapper, UpdateThunkTargets) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContent(GetPropellerTestDataFilePath("fake_thunks.bin")));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats,
                               /*hot_addresses=*/nullptr));
  // Construct fake branch aggregation.
  BranchAggregation branch_aggregation;
  branch_aggregation.branch_counters = {
      {BinaryAddressBranch{.from = 0x107bc, .to = 0xdeadbeef}, 100},
      {BinaryAddressBranch{.from = 0x107ba, .to = 0x100}, 100},
  };

  binary_address_mapper->UpdateThunkTargets(branch_aggregation);

  // Match thunk address only
  EXPECT_THAT(binary_address_mapper->GetThunkInfoUsingBinaryAddress(0x107bc),
              Optional(FieldsAre(0x107bc, 0xdeadbeef, _)));
  EXPECT_THAT(binary_address_mapper->GetThunkInfoUsingBinaryAddress(0x107cc),
              Optional(FieldsAre(0x107cc, 0, _)));
}
}  // namespace
}  // namespace propeller
