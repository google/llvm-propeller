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

#include "propeller/binary_address_mapper.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
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

static absl::flat_hash_map<llvm::StringRef, BBAddrMap>
GetBBAddrMapByFunctionName(const BinaryAddressMapper& binary_address_mapper) {
  absl::flat_hash_map<llvm::StringRef, BBAddrMap> bb_addr_map_by_func_name;
  for (const auto& [function_index, symbol_info] :
       binary_address_mapper.symbol_info_map()) {
    for (llvm::StringRef alias : symbol_info.aliases)
      bb_addr_map_by_func_name.insert(
          {alias, binary_address_mapper.bb_addr_map()[function_index]});
  }
  return bb_addr_map_by_func_name;
}

// Returns a `BinaryContent` for the ELF binary described by `yaml` (in the
// yaml2obj format).
absl::StatusOr<std::unique_ptr<BinaryContent>> GetBinaryContentFromYaml(
    llvm::StringRef yaml) {
  llvm::SmallString<0> elf_content;
  llvm::raw_svector_ostream elf_content_os(elf_content);
  llvm::yaml::Input yaml_input(yaml);
  std::string error;
  if (!llvm::yaml::convertYAML(
          yaml_input, elf_content_os,
          [&](const llvm::Twine& message) { error += message.str(); })) {
    return absl::InvalidArgumentError(error);
  }
  auto binary_content = std::make_unique<BinaryContent>();
  binary_content->file_name = "binary_from_yaml";
  binary_content->file_content =
      llvm::MemoryBuffer::getMemBufferCopy(elf_content);
  llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> object_file =
      llvm::object::ObjectFile::createObjectFile(
          binary_content->file_content->getMemBufferRef());
  if (!object_file) {
    return absl::InvalidArgumentError(llvm::toString(object_file.takeError()));
  }
  binary_content->object_file = std::move(*object_file);
  return binary_content;
}

// The ELF binaries below are described in the yaml2obj format and only include
// the parts relevant to `BinaryAddressMapper`: the executable sections, the
// `.llvm_bb_addr_map` section, and the function symbols with BB address map
// entries. Notes on the `.llvm_bb_addr_map` encoding:
//   * `AddressOffset` of a BB entry is relative to the end of the previous BB
//     entry in the same BB range (or to the range's base address for the first
//     entry).
//   * `Metadata` is a bitmask of (HasReturn = 0x1, HasTailCall = 0x2,
//     IsEHPad = 0x4, CanFallThrough = 0x8, HasIndirectBranch = 0x10).
//   * Duplicate symbol names are suffixed with ` (N)` to make them unique in
//     the YAML; yaml2obj strips these suffixes when emitting the symbol table.
// Use `obj2yaml <binary>` to regenerate the YAML from an ELF binary.

// The ELF binary built from testdata/sample.c, with the `main` symbol
// optionally excluded from the symbol table.
std::string GetSampleYaml(bool include_main_symbol) {
  constexpr absl::string_view kElfWithoutMainSymbol = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x16A0
    Size:    0x284
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1730
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x50, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1780
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x2B, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x17B0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x17C0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x29, Metadata: 0x0 }
              - { ID: 5, AddressOffset: 0x7, Size: 0xE, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x11, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x2B, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x0, Size: 0x2A, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x24, Metadata: 0x0 }
              - { ID: 6, AddressOffset: 0x0, Size: 0x9, Metadata: 0x1 }
Symbols:
  - Name:    this_is_very_code
    Type:    STT_FUNC
    Section: .text
    Value:   0x1730
    Size:    0x50
  - Name:    compute_flag
    Type:    STT_FUNC
    Section: .text
    Value:   0x1780
    Size:    0x2B
  - Name:    sample1_func
    Type:    STT_FUNC
    Section: .text
    Value:   0x17B0
    Size:    0x6
)yaml";
  constexpr absl::string_view kMainSymbol = R"yaml(
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x17C0
    Size:    0xD1
)yaml";
  return absl::StrCat(kElfWithoutMainSymbol,
                      include_main_symbol ? kMainSymbol : "");
}

// The ELF binary built from testdata/bimodal_sample.c with machine function
// splitting (MFS), which splits `compute` into two BB ranges.
constexpr llvm::StringLiteral kBimodalSampleMfsYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x1700
    Size:    0x2A4
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1700
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x54, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1760
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x14, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0xE, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x9, Metadata: 0x1 }
      - Version: 2
        Feature: 0x8
        BBRanges:
          - BaseAddress: 0x1790
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x1D, Metadata: 0x0 }
              - { ID: 3, AddressOffset: 0x3, Size: 0x3B, Metadata: 0x0 }
          - BaseAddress: 0x18C8
            BBEntries:
              - { ID: 1, AddressOffset: 0x0, Size: 0xE, Metadata: 0x0 }
              - { ID: 5, AddressOffset: 0x0, Size: 0x7, Metadata: 0x1 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x9, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x33, Metadata: 0x0 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x17F0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x50, Metadata: 0x1 }
Symbols:
  - Name:    bar
    Type:    STT_FUNC
    Section: .text
    Value:   0x1700
    Size:    0x54
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x1760
    Size:    0x2B
  - Name:    compute
    Type:    STT_FUNC
    Section: .text
    Value:   0x1790
    Size:    0x5B
  - Name:    foo
    Type:    STT_FUNC
    Section: .text
    Value:   0x17F0
    Size:    0x50
)yaml";

// The ELF binary built from testdata/duplicate_symbol_{1,2}.c, which has two
// `sample1_func` functions with different structures.
constexpr llvm::StringLiteral kDuplicateSymbolsYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x16C0
    Size:    0x304
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 5
        BBRanges:
          - BaseAddress: 0x1740
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x5D, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x17A0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x15, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0xD, Metadata: 0x0 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x8, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x0, Size: 0x5, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x17D0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x30, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0xD, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x18, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x0, Size: 0x2E, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x16, Metadata: 0x8 }
              - { ID: 5, AddressOffset: 0x0, Size: 0x1B, Metadata: 0x8 }
              - { ID: 6, AddressOffset: 0x0, Size: 0x2, Metadata: 0x8 }
              - { ID: 7, AddressOffset: 0x0, Size: 0xE, Metadata: 0x0 }
              - { ID: 8, AddressOffset: 0x0, Size: 0x8, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x18C0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x18D0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x9, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x18F0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x10, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x18, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0xC, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x0, Size: 0xD, Metadata: 0x0 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x5, Metadata: 0x1 }
Symbols:
  - Name:    this_is_very_code
    Type:    STT_FUNC
    Section: .text
    Value:   0x1740
    Size:    0x5D
  - Name:    compute_flag
    Type:    STT_FUNC
    Section: .text
    Value:   0x17A0
    Size:    0x2F
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x17D0
    Size:    0xED
  - Name:    'sample1_func (1)'
    Type:    STT_FUNC
    Section: .text
    Value:   0x18C0
    Size:    0x6
  - Name:    kunfu
    Type:    STT_FUNC
    Section: .text
    Value:   0x18D0
    Size:    0x19
  - Name:    'sample1_func (2)'
    Type:    STT_FUNC
    Section: .text
    Value:   0x18F0
    Size:    0x46
)yaml";

// The ELF binary built from testdata/sample_section.c, which has functions in
// non-".text" sections.
constexpr llvm::StringLiteral kSampleSectionYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x2016C0
    Size:    0x304
  - Name:    .anycall.anysection
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x2019F0
    Size:    0x1D
  - Name:    .othercall.othersection
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x201A10
    Size:    0x23
  - Name:    .text.unlikely
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x201A40
    Size:    0x6
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 2
        BBRanges:
          - BaseAddress: 0x201770
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x53, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x2017D0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x2B, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x201800
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x201810
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x2D, Metadata: 0x0 }
              - { ID: 5, AddressOffset: 0x3, Size: 0xE, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x25, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x2E, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x0, Size: 0x2E, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x2F, Metadata: 0x0 }
              - { ID: 6, AddressOffset: 0x0, Size: 0xD, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x2019F0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x1D, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x201A10
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x16, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x1, Metadata: 0x1 }
              - { ID: 1, AddressOffset: 0x0, Size: 0xC, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x201A40
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
Symbols:
  - Name:    this_is_very_code
    Type:    STT_FUNC
    Section: .text
    Value:   0x201770
    Size:    0x53
  - Name:    compute_flag
    Type:    STT_FUNC
    Section: .text
    Value:   0x2017D0
    Size:    0x2B
  - Name:    sample1_func
    Type:    STT_FUNC
    Section: .text
    Value:   0x201800
    Size:    0x6
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x201810
    Size:    0xFB
  - Name:    anycall
    Type:    STT_FUNC
    Section: .anycall.anysection
    Value:   0x2019F0
    Size:    0x1D
  - Name:    othercall
    Type:    STT_FUNC
    Section: .othercall.othersection
    Value:   0x201A10
    Size:    0x23
  - Name:    unlikelycall
    Type:    STT_FUNC
    Section: .text.unlikely
    Value:   0x201A40
    Size:    0x6
)yaml";

// The ELF binary built from testdata/unique_names.cc with
// `-funique-internal-linkage-names`, which has three identical copies of the
// uniq-named `foo.__uniq.1` function.
constexpr llvm::StringLiteral kDuplicateUniqueNamesYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x16A0
    Size:    0x1A4
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 5
        BBRanges:
          - BaseAddress: 0x1720
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x2, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x1730
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x1740
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x5, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x1750
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x1760
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x19, Metadata: 0x1 }
      - Version: 5
        BBRanges:
          - BaseAddress: 0x17A0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x1 }
Symbols:
  - Name:    goo
    Type:    STT_FUNC
    Section: .text
    Value:   0x1720
    Size:    0xB
  - Name:    'foo.__uniq.1 (1)'
    Type:    STT_FUNC
    Section: .text
    Value:   0x1730
    Size:    0x6
  - Name:    goo2
    Type:    STT_FUNC
    Section: .text
    Value:   0x1740
    Size:    0xE
  - Name:    'foo.__uniq.1 (2)'
    Type:    STT_FUNC
    Section: .text
    Value:   0x1750
    Size:    0x6
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x1760
    Size:    0x3C
  - Name:    'foo.__uniq.1 (3)'
    Type:    STT_FUNC
    Section: .text
    Value:   0x17A0
    Size:    0x6
)yaml";

// The ELF binary built from testdata/special_case.c, which has an empty basic
// block (`foo`'s BB 3) followed by a non-empty one at the same address.
constexpr llvm::StringLiteral kSpecialCaseYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x201560
    Size:    0xF4
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 2
        BBRanges:
          - BaseAddress: 0x2015F0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x6, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0xA, Size: 0x2, Metadata: 0x0 }
              - { ID: 1, AddressOffset: 0xE, Size: 0x8, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x8, Size: 0x0, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x1, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x201630
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x9, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x7, Size: 0xE, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x2, Size: 0x4, Metadata: 0x1 }
Symbols:
  - Name:    foo
    Type:    STT_FUNC
    Section: .text
    Value:   0x2015F0
    Size:    0x31
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x201630
    Size:    0x24
)yaml";

// The ELF binary built from testdata/bimodal_sample.c.
constexpr llvm::StringLiteral kBimodalSampleYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x16E0
    Size:    0x2C4
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1770
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x50, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x17C0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x54, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1820
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x14, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x9, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x3, Size: 0x38, Metadata: 0x0 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x9, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0xF, Size: 0x36, Metadata: 0x8 }
              - { ID: 5, AddressOffset: 0x0, Size: 0x7, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x18D0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x14, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0xC, Size: 0xE, Metadata: 0x8 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x9, Metadata: 0x1 }
Symbols:
  - Name:    foo
    Type:    STT_FUNC
    Section: .text
    Value:   0x1770
    Size:    0x50
  - Name:    bar
    Type:    STT_FUNC
    Section: .text
    Value:   0x17C0
    Size:    0x54
  - Name:    compute
    Type:    STT_FUNC
    Section: .text
    Value:   0x1820
    Size:    0xAD
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x18D0
    Size:    0x37
)yaml";

// The ELF binary built from testdata/bimodal_sample.x.c.
constexpr llvm::StringLiteral kBimodalSampleXYaml = R"yaml(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_EXEC
  Machine: EM_X86_64
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Address: 0x1700
    Size:    0x3E4
  - Name: .llvm_bb_addr_map
    Type: SHT_LLVM_BB_ADDR_MAP
    Link: .text
    Entries:
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1790
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x50, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x17E0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x54, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x1840
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x16, Metadata: 0x8 }
              - { ID: 7, AddressOffset: 0x0, Size: 0xE, Metadata: 0x8 }
              - { ID: 4, AddressOffset: 0x0, Size: 0x14, Metadata: 0x8 }
              - { ID: 5, AddressOffset: 0x0, Size: 0x116, Metadata: 0x0 }
              - { ID: 1, AddressOffset: 0x0, Size: 0x12, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0xA, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x6, Size: 0x2C, Metadata: 0x8 }
              - { ID: 6, AddressOffset: 0x0, Size: 0xD, Metadata: 0x1 }
      - Version: 2
        BBRanges:
          - BaseAddress: 0x19F0
            BBEntries:
              - { ID: 0, AddressOffset: 0x0, Size: 0x1E, Metadata: 0x0 }
              - { ID: 4, AddressOffset: 0x2, Size: 0x5, Metadata: 0x8 }
              - { ID: 2, AddressOffset: 0x0, Size: 0x15, Metadata: 0x8 }
              - { ID: 3, AddressOffset: 0x0, Size: 0x15, Metadata: 0x0 }
              - { ID: 1, AddressOffset: 0x0, Size: 0xB, Metadata: 0x1 }
Symbols:
  - Name:    foo
    Type:    STT_FUNC
    Section: .text
    Value:   0x1790
    Size:    0x50
  - Name:    bar
    Type:    STT_FUNC
    Section: .text
    Value:   0x17E0
    Size:    0x54
  - Name:    compute
    Type:    STT_FUNC
    Section: .text
    Value:   0x1840
    Size:    0x1A9
  - Name:    main
    Type:    STT_FUNC
    Section: .text
    Value:   0x19F0
    Size:    0x5A
)yaml";

TEST(BinaryAddressMapper, BbAddrMapExist) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContentFromYaml(GetSampleYaml(/*include_main_symbol=*/true)));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->bb_addr_map(), Not(IsEmpty()));
}

TEST(BinaryAddressMapper, BbAddrMapReadSymbolTable) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContentFromYaml(GetSampleYaml(/*include_main_symbol=*/true)));
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
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryContent> binary_content,
      GetBinaryContentFromYaml(GetSampleYaml(/*include_main_symbol=*/false)));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleMfsYaml));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleMfsYaml));
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
      GetBinaryContentFromYaml(GetSampleYaml(/*include_main_symbol=*/true)));
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
  // Note that the BB entry offsets are relative to the function's address
  // (unlike the relative `AddressOffset` fields in the YAML).
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kDuplicateSymbolsYaml));
  PropellerStats stats;
  PropellerOptions options;

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));
  EXPECT_THAT(binary_address_mapper->selected_functions(), Not(IsEmpty()));
  // Multiple symbols have the "sample1_func" name hence none of them will be
  // kept. Other functions are not affected.
  EXPECT_THAT(
      GetBBAddrMapByFunctionName(*binary_address_mapper),
      AllOf(Not(Contains(Key("sample1_func"))),
            Contains(Pair("compute_flag", BbAddrMapIs(_, Not(IsEmpty()))))));
  EXPECT_EQ(stats.bbaddrmap_stats.duplicate_symbols, 2);
}

TEST(BinaryAddressMapper, NoneDotTextSymbolsDropped) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kSampleSectionYaml));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kSampleSectionYaml));
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
                       GetBinaryContentFromYaml(kDuplicateUniqueNamesYaml));
  PropellerStats stats;
  PropellerOptions options;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<BinaryAddressMapper> binary_address_mapper,
      BuildBinaryAddressMapper(options, *binary_content, stats));

  // The three identical copies of the uniq-named function are dropped and
  // counted as duplicate symbols.
  EXPECT_THAT(binary_address_mapper->selected_functions(),
              ElementsAre(0, 2, 4));
  EXPECT_EQ(stats.bbaddrmap_stats.duplicate_symbols, 3);
}

TEST(BinaryAddressMapper, CheckNoHotFunctions) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kSampleSectionYaml));
  // Both addresses are inside `main` ([0x201810, 0x20190B)).
  absl::flat_hash_set<uint64_t> hot_addresses = {0x201900, 0x201870};

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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kSpecialCaseYaml));
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

  // `foo`'s basic blocks (in layout order) span the following address ranges:
  //   BB0: [0x2015F0, 0x2015F6)
  //   BB2: [0x201600, 0x201602)
  //   BB1: [0x201610, 0x201618)
  //   BB3: [0x201620, 0x201620) (empty)
  //   BB4: [0x201620, 0x201621)
  //
  // At address 0x201620 we have an empty block followed by a non-empty block.
  // With BranchDirection::kTo, the address should be mapped to BB3.
  // With BranchDirection::kFrom, the address should be mapped to BB4.
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201620, BranchDirection::kTo),
              Optional(ResultOf(bb_index_from_handle_index, 3)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201620, BranchDirection::kFrom),
              Optional(ResultOf(bb_index_from_handle_index, 4)));

  // With BranchDirection::kFrom, 0x201616 should be mapped to BB1 and 0x201618
  // should be rejected because it is outside of the basic block (in the padding
  // between BB1 and BB3).
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201616, BranchDirection::kFrom),
              Optional(ResultOf(bb_index_from_handle_index, 2)));
  EXPECT_THAT(binary_address_mapper->FindBbHandleIndexUsingBinaryAddress(
                  0x201618, BranchDirection::kFrom),
              Eq(std::nullopt));

  // `main`'s basic blocks span the following address ranges:
  //   BB0: [0x201630, 0x201639)
  //   BB1: [0x201640, 0x20164E)
  //   BB2: [0x201650, 0x201654)
  //
  // 0x20164E is the end address of BB1 (e.g., the return address of a call
  // which is the last instruction of BB1, when followed by padding). So with
  // BranchDirection::kTo 0x20164E should be mapped to BB1 and with
  // BranchDirection::kFrom it should be mapped to std::nullopt (rejected).
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleYaml));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleYaml));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleYaml));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleYaml));
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContentFromYaml(kBimodalSampleXYaml));
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
