#ifndef PROPELLER_BINARY_CONTENT_H_
#define PROPELLER_BINARY_CONTENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/ObjectFile.h"

namespace propeller {

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
    absl::flat_hash_map<absl::string_view, absl::string_view> modinfo;
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
  virtual absl::Status ReadLoadableSegments(BinaryContent &binary_content) = 0;

  // Initializes BinaryContent::KernelModule::modinfo from the .modinfo section,
  // if binary_content does not contain a valid kernel module, return error
  // status.
  virtual absl::Status InitializeKernelModule(
      BinaryContent &binary_content) = 0;

  // Parses (key, value) pairs in `section_content` and store them in `modinfo`.
  static absl::StatusOr<
      absl::flat_hash_map<absl::string_view, absl::string_view>>
  ParseModInfoSectionContent(absl::string_view section_content);

 protected:
  static constexpr llvm::StringRef kModInfoSectionName = ".modinfo";
  static constexpr llvm::StringRef kLinkOnceSectionName =
      ".gnu.linkonce.this_module";
  static constexpr llvm::StringRef kBuildIDSectionName = ".note.gnu.build-id";
  // Kernel images built via gbuild use section name ".notes" for buildid.
  static constexpr llvm::StringRef kKernelBuildIDSectionName = ".notes";
  static constexpr llvm::StringRef kBuildIdNoteName = "GNU";

  friend std::unique_ptr<ELFFileUtilBase> CreateELFFileUtil(
      llvm::object::ObjectFile *object_file);
};

std::unique_ptr<ELFFileUtilBase> CreateELFFileUtil(
    llvm::object::ObjectFile *object_file);

absl::StatusOr<std::unique_ptr<BinaryContent>> GetBinaryContent(
    absl::string_view binary_file_name);

// Returns the binary address of the symbol named `symbol_name`, or
// `absl::NotFoundError` if the symbol is not found.
absl::StatusOr<int64_t> GetSymbolAddress(
    const llvm::object::ObjectFile &object_file, absl::string_view symbol_name);

// Returns the binary's function symbols by reading from its symbol table.
absl::flat_hash_map<uint64_t, llvm::SmallVector<llvm::object::ELFSymbolRef>>
ReadSymbolTable(const BinaryContent &binary_content);

// Returns an AArch64 binary's thunk symbols by reading from its symbol table.
absl::btree_map<uint64_t, llvm::object::ELFSymbolRef> ReadAArch64ThunkSymbols(
    const BinaryContent &binary_content);

// Returns the binary's thunk symbols by reading from its symbol table.
std::optional<absl::btree_map<uint64_t, llvm::object::ELFSymbolRef>>
ReadThunkSymbols(const BinaryContent &binary_content);

// Returns the binary's `BBAddrMap`s by calling LLVM-side decoding function
// `ELFObjectFileBase::readBBAddrMap`. Returns error if the call fails or if the
// result is empty.
absl::StatusOr<std::vector<llvm::object::BBAddrMap>> ReadBbAddrMap(
    const BinaryContent &binary_content);

}  // namespace propeller

#endif  // PROPELLER_BINARY_CONTENT_H_
