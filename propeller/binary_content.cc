#include "propeller/binary_content.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "propeller/addr2cu.h"
#include "propeller/status_macros.h"

namespace {
using ::propeller::BinaryContent;
using ::propeller::CreateDWARFContext;

// Convert binary data stored in data[...] into text representation.
std::string BinaryDataToAscii(absl::string_view data) {
  std::string ascii(data.size() * 2, 0);
  const char heximal[] = "0123456789abcdef";
  for (int i = 0; i < data.size(); ++i) {
    uint8_t d(data[i]);
    ascii[i * 2] = heximal[((d >> 4) & 0xf)];
    ascii[i * 2 + 1] = heximal[(d & 0xf)];
  }
  return ascii;
}

// Find relocatable ko file's text section index and store it in
// BinaryContent::kernel_module::text_section_index. We only care the first
// section that have SHF_EXECINSTR and SHF_ALLOC flags. This is similar to
// https://source.corp.google.com/h/prodkernel/kernel/release/11xx/+/next:kernel/module.c;l=2514;bpv=1;bpt=0;drc=1e08480468356a0d443f12e97798fbfdc906e76b
//
// (In addition, we require this section to be named ".text". If ".text" is
// not the first such section, we need to do extra alignment calculations for
// the layout, which we choose not to implement here.)
//
// We also create a segment using the section's (offset, address, size).
template <class ELFT>
absl::Status FindRelocatableTextSectionToFillSegment(
    const llvm::object::ELFFile<ELFT> &elf_file,
    BinaryContent &binary_content) {
  llvm::Expected<typename ELFT::ShdrRange> sections = elf_file.sections();
  if (!sections) {
    return absl::FailedPreconditionError(
        "failed to read section list from elf object file");
  }
  for (const typename ELFT::Shdr &shdr : *sections) {
    llvm::Expected<llvm::StringRef> section_name =
        elf_file.getSectionName(shdr);
    if (!section_name) continue;
    uint32_t mask = llvm::ELF::SHF_EXECINSTR | llvm::ELF::SHF_ALLOC;
    if ((shdr.sh_flags & mask) != mask) continue;
    // Relocatable objects do not have "segments", so we use section's
    // address/size/offset fields to create segment data.
    if (*section_name == ".text") {
      // sh_offset, sh_addr and sh_size are of type
      // packed_endian_specific_integral, must use conversion operator method to
      // access its value.
      binary_content.segments.push_back(BinaryContent::Segment{
          .offset = typename ELFT::Off::value_type(shdr.sh_offset),
          .vaddr = typename ELFT::Addr::value_type(shdr.sh_addr),
          .memsz = typename ELFT::Word::value_type(shdr.sh_size)});
      binary_content.kernel_module->text_section_index =
          std::distance(sections->begin(), &shdr);
      return absl::OkStatus();
    }
  }
  return absl::NotFoundError(
      "\".text\" section with EXECINSTR | ALLOC flags not found: ");
}

template <class ELFT>
class ELFFileUtil : public ::propeller::ELFFileUtilBase {
 public:
  explicit ELFFileUtil(llvm::object::ObjectFile *object) {
    llvm::object::ELFObjectFile<ELFT> *elf_object =
        llvm::dyn_cast<llvm::object::ELFObjectFile<ELFT>,
                       llvm::object::ObjectFile>(object);
    if (elf_object) elf_file_ = &elf_object->getELFFile();
  }

  std::string GetBuildId() override;

  absl::Status ReadLoadableSegments(BinaryContent &binary_content) override;

  absl::Status InitializeKernelModule(BinaryContent &binary_content) override;

 private:
  const llvm::object::ELFFile<ELFT> *elf_file_ = nullptr;

  absl::StatusOr<const typename ELFT::Shdr *> FindSection(
      llvm::StringRef section_name) const;
};

// TODO(shenhan): remove the following code once it is upstreamed.
template <class ELFT>
std::string ELFFileUtil<ELFT>::GetBuildId() {
  if (!elf_file_) return "";
  auto hex_to_char = [](uint8_t v) -> char {
    if (v < 10) return '0' + v;
    return 'a' + (v - 10);
  };
  std::vector<std::string> build_ids;
  for (const typename ELFT::Shdr &shdr :
       llvm::cantFail(elf_file_->sections())) {
    llvm::Expected<llvm::StringRef> section_name =
        elf_file_->getSectionName(shdr);
    if (!section_name || shdr.sh_type != llvm::ELF::SHT_NOTE ||
        (*section_name != kBuildIDSectionName &&
         *section_name != kKernelBuildIDSectionName))
      continue;
    llvm::Error err = llvm::Error::success();
    for (const typename ELFT::Note &note : elf_file_->notes(shdr, err)) {
      llvm::StringRef r = note.getName();
      if (r == kBuildIdNoteName) {
        llvm::ArrayRef<uint8_t> build_id = note.getDesc(shdr.sh_addralign);
        std::string build_id_str(build_id.size() * 2, '0');
        int k = 0;
        for (uint8_t t : build_id) {
          build_id_str[k++] = hex_to_char((t >> 4) & 0xf);
          build_id_str[k++] = hex_to_char(t & 0xf);
        }
        build_ids.push_back(std::move(build_id_str));
      }
    }
    if (errorToBool(std::move(err)))
      LOG(WARNING) << "error happened iterating note entries in '"
                   << section_name->str() << "'";
  }
  if (build_ids.empty()) return "";
  if (build_ids.size() > 1) {
    LOG(WARNING) << "more than 1 build id entries found in the binary, only "
                    "the first one will be returned";
  }
  return build_ids.front();
}

template <class ELFT>
absl::Status ELFFileUtil<ELFT>::ReadLoadableSegments(
    BinaryContent &binary_content) {
  CHECK(elf_file_);
  if (binary_content.is_relocatable &&
      binary_content.kernel_module.has_value()) {
    RETURN_IF_ERROR(FindRelocatableTextSectionToFillSegment<ELFT>(
        *elf_file_, binary_content));
    return absl::OkStatus();
  }
  auto program_headers = elf_file_->program_headers();
  if (!program_headers) {
    return absl::FailedPreconditionError(absl::StrCat(
        binary_content.file_name, " does not have program headers"));
  }

  for (const typename ELFT::Phdr &phdr : *program_headers) {
    if (phdr.p_type != llvm::ELF::PT_LOAD ||
        ((phdr.p_flags & llvm::ELF::PF_X) == 0))
      continue;

    binary_content.segments.push_back(
        {phdr.p_offset, phdr.p_vaddr, phdr.p_memsz});
  }
  if (binary_content.segments.empty()) {
    return absl::FailedPreconditionError(
        absl::StrCat("No loadable and executable segments found in '",
                     binary_content.file_name, "'"));
  }
  return absl::OkStatus();
}

template <class ELFT>
absl::StatusOr<const typename ELFT::Shdr *> ELFFileUtil<ELFT>::FindSection(
    llvm::StringRef section_name) const {
  llvm::Expected<llvm::ArrayRef<typename ELFT::Shdr>> sections =
      elf_file_->sections();
  if (!sections) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to get sections from the ELF file: ",
                     llvm::toString(sections.takeError())));
  }
  for (const typename ELFT::Shdr &shdr : *sections) {
    llvm::Expected<llvm::StringRef> sn = elf_file_->getSectionName(shdr);
    if (!sn) continue;
    if (*sn == section_name) return &shdr;
  }
  return absl::NotFoundError(absl::StrCat(
      "Section not found: ",
      absl::string_view(section_name.data(), section_name.size())));
}

template <class ELFT>
absl::Status ELFFileUtil<ELFT>::InitializeKernelModule(
    BinaryContent &binary_content) {
  absl::StatusOr<const typename ELFT::Shdr *> this_module_section =
      FindSection(kLinkOnceSectionName);
  if (!this_module_section.ok()) return this_module_section.status();

  ASSIGN_OR_RETURN(const auto &modinfo_section,
                   FindSection(kModInfoSectionName));
  int modinfo_section_index = std::distance(
      llvm::cantFail(elf_file_->sections()).begin(), modinfo_section);

  const typename ELFT::Shdr &modinfo =
      llvm::cantFail(elf_file_->sections())[modinfo_section_index];
  llvm::Expected<llvm::ArrayRef<uint8_t>> modinfo_data =
      elf_file_->getSectionContents(modinfo);
  if (!modinfo_data) {
    return absl::FailedPreconditionError(
        "failed to get data for .modinfo section");
  }

  binary_content.kernel_module = BinaryContent::KernelModule{};
  absl::string_view section_content(
      reinterpret_cast<const char *>(modinfo_data->data()),
      modinfo_data->size());
  ASSIGN_OR_RETURN(binary_content.kernel_module->modinfo,
                   ParseModInfoSectionContent(section_content));
  if (auto name = binary_content.kernel_module->modinfo.find("name");
      name != binary_content.kernel_module->modinfo.end()) {
    LOG(INFO) << "Found kernel module name: " << name->second;
  }
  if (auto desc = binary_content.kernel_module->modinfo.find("description");
      desc != binary_content.kernel_module->modinfo.end())
    LOG(INFO) << "Found kernel module description: " << desc->second;
  return absl::OkStatus();
}
}  // namespace

namespace propeller {
absl::flat_hash_map<uint64_t, llvm::SmallVector<llvm::object::ELFSymbolRef>>
ReadSymbolTable(const BinaryContent &binary_content) {
  absl::flat_hash_map<uint64_t, llvm::SmallVector<llvm::object::ELFSymbolRef>>
      symtab;
  for (llvm::object::SymbolRef sr : binary_content.object_file->symbols()) {
    llvm::object::ELFSymbolRef symbol(sr);
    uint8_t stt = symbol.getELFType();
    if (stt != llvm::ELF::STT_FUNC) continue;
    llvm::Expected<uint64_t> address = sr.getAddress();
    if (!address || !*address) continue;
    llvm::Expected<llvm::StringRef> func_name = symbol.getName();
    if (!func_name) continue;
    const uint64_t func_size = symbol.getSize();
    if (func_size == 0) continue;

    auto &addr_sym_list = symtab[*address];
    // Check whether there are already symbols on the same address, if so make
    // sure they have the same size and thus they can be aliased.
    bool check_size_ok = true;
    for (auto &sym_ref : addr_sym_list) {
      uint64_t sym_size = llvm::object::ELFSymbolRef(sym_ref).getSize();
      if (func_size != sym_size) {
        LOG(WARNING) << "Multiple function symbols on the same address with "
                        "different size: "
                     << absl::StrCat(absl::Hex(*address)) << ": '"
                     << func_name->str() << "(" << func_size << ")' and '"
                     << llvm::cantFail(sym_ref.getName()).str() << "("
                     << sym_size << ")', the former will be dropped.";
        check_size_ok = false;
        break;
      }
    }
    if (check_size_ok) addr_sym_list.push_back(sr);
  }
  return symtab;
}

// Read thunks from the symbol table in sorted order.
absl::btree_map<uint64_t, llvm::object::ELFSymbolRef> ReadAArch64ThunkSymbols(
    const BinaryContent &binary_content) {
  absl::btree_map<uint64_t, llvm::object::ELFSymbolRef> thunk_map;
  for (llvm::object::SymbolRef sr : binary_content.object_file->symbols()) {
    llvm::object::ELFSymbolRef symbol(sr);
    uint8_t stt = symbol.getELFType();
    if (stt != llvm::ELF::STT_FUNC) continue;
    llvm::Expected<uint64_t> address = sr.getAddress();
    if (!address || !*address) continue;
    llvm::Expected<llvm::StringRef> func_name = symbol.getName();
    // TODO(tzussman): More explicit thunk name check.
    if (!func_name || !func_name->starts_with("__AArch64")) continue;
    const uint64_t func_size = symbol.getSize();
    if (func_size == 0) continue;

    thunk_map.insert({*address, sr});
  }
  return thunk_map;
}

std::optional<absl::btree_map<uint64_t, llvm::object::ELFSymbolRef>>
ReadThunkSymbols(const BinaryContent &binary_content) {
  if (binary_content.object_file->getArch() == llvm::Triple::aarch64)
    return ReadAArch64ThunkSymbols(binary_content);

  return std::nullopt;
}

absl::StatusOr<std::vector<llvm::object::BBAddrMap>> ReadBbAddrMap(
    const BinaryContent &binary_content) {
  auto *elf_object = llvm::dyn_cast<llvm::object::ELFObjectFileBase>(
      binary_content.object_file.get());
  CHECK_NE(elf_object, nullptr);
  llvm::Expected<std::vector<llvm::object::BBAddrMap>> bb_addr_map =
      elf_object->readBBAddrMap(
          binary_content.kernel_module.has_value()
              ? std::optional<unsigned>(
                    binary_content.kernel_module->text_section_index)
              : std::nullopt);
  if (!bb_addr_map) {
    return absl::InternalError(
        llvm::formatv(
            "Failed to read the LLVM_BB_ADDR_MAP section from {0}: {1}.",
            binary_content.file_name,
            llvm::fmt_consume(bb_addr_map.takeError()))
            .str());
  }
  if (bb_addr_map->empty()) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "'%s' does not have a non-empty LLVM_BB_ADDR_MAP section.",
        binary_content.file_name));
  }
  return std::move(*bb_addr_map);
}

absl::StatusOr<absl::flat_hash_map<absl::string_view, absl::string_view>>
ELFFileUtilBase::ParseModInfoSectionContent(absl::string_view section_content) {
  // .modinfo section is arranged as <key>=<value> pairs, with \0 as separators,
  // the last <key>=<value> pair also ends with \0.
  if (section_content.empty())
    return absl::FailedPreconditionError("empty .modinfo section");
  absl::flat_hash_map<absl::string_view, absl::string_view> modinfo;
  const char *q, *eq, *p = section_content.data();
  const char *end = p + section_content.size();
  while (p < end) {
    q = p;
    eq = p;
    while (q != end && *q != '\0') ++q;
    if (p == q) {
      return absl::FailedPreconditionError(
          "malformed .modinfo entry: entry is empty");
    }
    if (q == end) {
      return absl::FailedPreconditionError(
          "malformed .modinfo entry: entry does not end properly");
    }
    while (eq != q && *eq != '=') ++eq;
    if (*eq != '=') {
      return absl::FailedPreconditionError(
          "malformed .modinfo entry: entry does not contain '='");
    }
    if (eq != p && eq != q) {
      CHECK(eq > p);
      CHECK(eq < q);
      modinfo.emplace(absl::string_view(p, eq - p), absl::string_view(eq + 1));
    } else {
      return absl::FailedPreconditionError(
          "malformed .modinfo entry: entry contains only key or value");
    }
    p = q + 1;
    // Some entries may have multiple \0s at the end, move 'p' over these
    // extraneous \0s.
    while (p != end && *p == '\0') ++p;
  }
  if (modinfo.empty()) {
    return absl::FailedPreconditionError(
        "nothing meaningful in .modinfo section");
  }
  return modinfo;
}

std::unique_ptr<ELFFileUtilBase> CreateELFFileUtil(
    llvm::object::ObjectFile *object_file) {
  if (!object_file) return nullptr;
  llvm::StringRef content = object_file->getData();
  const char *elf_start = content.data();

  if (content.size() <= strlen(llvm::ELF::ElfMagic) ||
      strncmp(elf_start, llvm::ELF::ElfMagic, strlen(llvm::ELF::ElfMagic))) {
    LOG(ERROR) << "Not a valid ELF file.";
    return nullptr;
  }
  const char elf_class = elf_start[llvm::ELF::EI_CLASS];
  const char elf_data = elf_start[llvm::ELF::EI_DATA];
  if (elf_class == llvm::ELF::ELFCLASS32 &&
      elf_data == llvm::ELF::ELFDATA2LSB) {
    return std::make_unique<ELFFileUtil<llvm::object::ELF32LE>>(object_file);
  } else if (elf_class == llvm::ELF::ELFCLASS32 &&
             elf_data == llvm::ELF::ELFDATA2MSB) {
    return std::make_unique<ELFFileUtil<llvm::object::ELF32BE>>(object_file);
  } else if (elf_class == llvm::ELF::ELFCLASS64 &&
             elf_data == llvm::ELF::ELFDATA2LSB) {
    return std::make_unique<ELFFileUtil<llvm::object::ELF64LE>>(object_file);
  } else if (elf_class == llvm::ELF::ELFCLASS64 &&
             elf_data == llvm::ELF::ELFDATA2MSB) {
    return std::make_unique<ELFFileUtil<llvm::object::ELF64BE>>(object_file);
  }
  LOG(ERROR) << "Unrecognized ELF file data.";
  return nullptr;
}

// Initializes BinaryContent object:
//  - setup file content memory buffer
//  - setup object file pointer
//  - setup "PIE" bit
//  - read loadable and executable segments
absl::StatusOr<std::unique_ptr<BinaryContent>> GetBinaryContent(
    absl::string_view binary_file_name) {
  auto binary_content = std::make_unique<BinaryContent>();
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file =
      llvm::MemoryBuffer::getFile(binary_file_name);
  if (!file) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to read file '", binary_file_name,
                     "': ", file.getError().message()));
  }
  llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> obj =
      llvm::object::ObjectFile::createELFObjectFile(
          llvm::MemoryBufferRef(*(*file)));
  if (!obj) {
    std::string error_message;
    llvm::raw_string_ostream raw_string_ostream(error_message);
    raw_string_ostream << obj.takeError();
    return absl::FailedPreconditionError(
        absl::StrCat("Not a valid ELF file '", binary_file_name,
                     "': ", raw_string_ostream.str()));
  }
  llvm::object::ELFObjectFileBase *elf_obj =
      llvm::dyn_cast<llvm::object::ELFObjectFileBase, llvm::object::ObjectFile>(
          (*obj).get());
  if (!elf_obj) {
    return absl::FailedPreconditionError(
        absl::StrCat("Not a valid ELF file '", binary_file_name, "."));
  }
  binary_content->file_name = binary_file_name;
  binary_content->file_content = std::move(*file);
  binary_content->object_file = std::move(*obj);

  std::string dwp_file = absl::StrCat(binary_content->file_name, ".dwp");
  if (!llvm::sys::fs::exists(dwp_file)) dwp_file = "";
  binary_content->dwp_file_name = dwp_file;
  absl::StatusOr<std::unique_ptr<llvm::DWARFContext>> dwarf_context =
      CreateDWARFContext(*binary_content->object_file, dwp_file);
  if (dwarf_context.ok()) {
    binary_content->dwarf_context = std::move(*dwarf_context);
  } else {
    LOG(WARNING) << "Failed to create DWARF context: " << dwarf_context.status()
                 << "\nNo module names wil be available";
  }

  binary_content->is_pie = (elf_obj->getEType() == llvm::ELF::ET_DYN);
  LOG(INFO) << "'" << binary_file_name
            << "' is PIE: " << binary_content->is_pie;

  binary_content->is_relocatable = (elf_obj->getEType() == llvm::ELF::ET_REL);
  LOG(INFO) << "'" << binary_file_name
            << "' is relocatable: " << binary_content->is_relocatable;

  std::unique_ptr<ELFFileUtilBase> elf_file_util =
      CreateELFFileUtil(binary_content->object_file.get());
  CHECK(elf_file_util != nullptr);
  binary_content->build_id = elf_file_util->GetBuildId();
  if (!binary_content->build_id.empty())
    LOG(INFO) << "Build Id found in '" << binary_file_name
              << "': " << binary_content->build_id;

  if (binary_content->is_relocatable) {
    if (!absl::EndsWith(binary_content->file_name, ".ko")) {
      return absl::FailedPreconditionError(
          "Only support kernel module (.ko) relocatable objects");
    }
    RETURN_IF_ERROR(elf_file_util->InitializeKernelModule(*binary_content));
  }
  RETURN_IF_ERROR(elf_file_util->ReadLoadableSegments(*binary_content));
  return binary_content;
}

absl::StatusOr<int64_t> GetSymbolAddress(
    const llvm::object::ObjectFile &object_file,
    absl::string_view symbol_name) {
  const llvm::object::ELFObjectFileBase *elf_object =
      llvm::dyn_cast<const llvm::object::ELFObjectFileBase,
                     const llvm::object::ObjectFile>((&object_file));
  if (elf_object == nullptr) {
    return absl::InvalidArgumentError(
        "The provided object file is not an ELF object file.");
  }
  for (llvm::object::ELFSymbolRef symbol : elf_object->symbols()) {
    llvm::Expected<llvm::StringRef> current_symbol_name = symbol.getName();
    if (!current_symbol_name ||
        absl::string_view(*current_symbol_name) != symbol_name) {
      continue;
    }
    llvm::Expected<uint64_t> symbol_address = symbol.getAddress();
    if (!symbol_address) {
      LOG(ERROR) << symbol_name << " has no address: "
                 << llvm::toString(symbol_address.takeError());
      continue;
    }
    return *symbol_address;
  }
  return absl::NotFoundError(absl::StrCat(symbol_name, " not found"));
}

}  // namespace propeller
