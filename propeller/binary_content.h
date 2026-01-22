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

#ifndef PROPELLER_BINARY_CONTENT_H_
#define PROPELLER_BINARY_CONTENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/ObjectFile.h"

namespace propeller {
// This struct stores the function name aliases and the output section name
// associated with a function.
struct FunctionSymbolInfo {
  // All names associated with the function.
  llvm::SmallVector<llvm::StringRef> aliases;
  // Section name of the function in the binary. All .text and .text.*
  // sections are represented by ".text".
  llvm::StringRef section_name;
};

// A container for the `BbAddrMap` and `PGOAnalysisMap` data read from the
// binary's SHT_LLVM_BB_ADDR_MAP section.
struct BbAddrMapData {
  std::vector<llvm::object::BBAddrMap> bb_addr_maps;
  std::optional<std::vector<llvm::object::PGOAnalysisMap>> pgo_analyses;
};

// Options for reading the `BbAddrMapData` from the binary.
struct BbAddrMapReadOptions {
  bool read_pgo_analyses = false;
};

// BinaryContent represents information for an ELF executable or a shared
// object, the data contained include (loadable) segments, file name, file
// content and DYN tag (is_pie).
struct BinaryContent {
  struct Segment {
    uint64_t offset;
    uint64_t vaddr;
    uint64_t memsz;
  };

  struct KernelModule {
    // The section index of the first section which has EXEC and ALLOC flags set
    // and has name ".text". This field is only meant for
    // ELFObjectFile::readBBAddrMap.
    int text_section_index;
    // The module's metadata stored as (key, value) pairs in ".modinfo" section.
    // The "name" and "description" will be printed out via LOG statements.
    // Ideally we shall read
    // ".gnu.linkonce.this_module" section, which has a more thorough
    // information for the module, however, that would need to build this tool
    // again kernel headers.
    // https://source.corp.google.com/h/prodkernel/kernel/release/11xx/+/next:include/linux/module.h;l=365;bpv=1;bpt=0;drc=f2a96a349893fdff944b710ee86d1106de088a40
    llvm::DenseMap<llvm::StringRef, llvm::StringRef> modinfo;
  };

  std::string file_name;
  // If not empty, it is the existing dwp file for the binary.
  std::string dwp_file_name;
  std::unique_ptr<llvm::MemoryBuffer> file_content = nullptr;
  std::unique_ptr<llvm::object::ObjectFile> object_file = nullptr;
  std::unique_ptr<llvm::DWARFContext> dwarf_context = nullptr;
  bool is_pie = false;
  // Propeller accepts relocatable object files as input only if it is a kernel
  // module.
  bool is_relocatable = false;
  std::vector<Segment> segments;
  std::string build_id;
  // Only not-null when input is *.ko and `ELFFileUtil::InitializeKernelModule`
  // returns ok status.
  std::optional<KernelModule> kernel_module = std::nullopt;
};

// Utility class that wraps utility functions that need templated
// ELFFile<ELFT> support.
class ELFFileUtilBase {
 protected:
  ELFFileUtilBase() = default;

 public:
  virtual ~ELFFileUtilBase() = default;

  virtual std::string GetBuildId() = 0;

  // Reads loadable and executable segment information into
  // BinaryContent::segments.
  virtual absl::Status ReadLoadableSegments(BinaryContent& binary_content) = 0;

  // Initializes BinaryContent::KernelModule::modinfo from the .modinfo section,
  // if binary_content does not contain a valid kernel module, return error
  // status.
  virtual absl::Status InitializeKernelModule(
      BinaryContent& binary_content) = 0;

  // Parses (key, value) pairs in `section_content` and store them in `modinfo`.
  static absl::StatusOr<llvm::DenseMap<llvm::StringRef, llvm::StringRef>>
  ParseModInfoSectionContent(llvm::StringRef section_content);

 protected:
  static constexpr llvm::StringRef kModInfoSectionName = ".modinfo";
  static constexpr llvm::StringRef kLinkOnceSectionName =
      ".gnu.linkonce.this_module";
  static constexpr llvm::StringRef kBuildIDSectionName = ".note.gnu.build-id";
  // Kernel images built via gbuild use section name ".notes" for buildid.
  static constexpr llvm::StringRef kKernelBuildIDSectionName = ".notes";
  static constexpr llvm::StringRef kBuildIdNoteName = "GNU";

  friend std::unique_ptr<ELFFileUtilBase> CreateELFFileUtil(
      const llvm::object::ObjectFile* object_file);
};

std::unique_ptr<ELFFileUtilBase> CreateELFFileUtil(
    const llvm::object::ObjectFile* object_file);

absl::StatusOr<std::unique_ptr<BinaryContent>> GetBinaryContent(
    llvm::StringRef binary_file_name);

// Returns the binary address of the symbol named `symbol_name`, or
// `absl::NotFoundError` if the symbol is not found.
absl::StatusOr<int64_t> GetSymbolAddress(
    const llvm::object::ObjectFile& object_file, llvm::StringRef symbol_name);

// Returns the binary's function symbols by reading from its symbol table.
llvm::DenseMap<uint64_t, llvm::SmallVector<llvm::object::ELFSymbolRef>>
ReadSymbolTable(const BinaryContent& binary_content);

// Returns the binary's thunk symbols by reading from its symbol table.
// These are returned as a map from the thunk's address to the thunk symbol.
// Returns an empty map if the architecture does not support thunks.
absl::btree_map<uint64_t, llvm::object::ELFSymbolRef> ReadThunkSymbols(
    const BinaryContent& binary_content);

// Returns a map from function addresses to their symbol info.
llvm::DenseMap<uint64_t, FunctionSymbolInfo> GetSymbolInfoMap(
    const BinaryContent& binary_content);

// Returns the binary's `BbAddrMapData`s by calling LLVM-side decoding function
// `ELFObjectFileBase::readBBAddrMap`. Returns error if the call fails or if the
// result is empty. If `options.read_pgo_analyses` is true, the function will
// also read the PGO analysis map and store it in the returned `BbAddrMapData`.
absl::StatusOr<BbAddrMapData> ReadBbAddrMap(
    const BinaryContent& binary_content,
    const BbAddrMapReadOptions& options = {});

}  // namespace propeller

#endif  // PROPELLER_BINARY_CONTENT_H_
