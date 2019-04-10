#ifndef LLD_ELF_PLO_ELF_VIEW_H
#define LLD_ELF_PLO_ELF_VIEW_H

#include <map>
#include <memory>

#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::MemoryBufferRef;
using llvm::object::ObjectFile;
using llvm::object::section_iterator;
using llvm::StringRef;

using std::map;
using std::unique_ptr;

namespace lld {
namespace plo {

class ELFCfg;

class ELFView {
 public:
  static ELFView *Create(const StringRef &VN,
                         const uint32_t O,
                         const MemoryBufferRef &FR);

  ELFView(unique_ptr<ObjectFile> &VF,
          const StringRef &VN,
          const uint32_t VO,
          const MemoryBufferRef &FR) :
    ViewFile(std::move(VF)), ViewName(VN), Ordinal(VO), FileRef(FR), Cfgs() {}
  ~ELFView() {}

  void BuildCfgs();
  void EraseCfg(ELFCfg *&CfgPtr);

  unique_ptr<ObjectFile> ViewFile;
  StringRef              ViewName;
  const uint32_t         Ordinal;
  MemoryBufferRef        FileRef;

  map<StringRef, unique_ptr<ELFCfg>> Cfgs;
};


}  // namespace plo
}  // namespace llvm
#endif
