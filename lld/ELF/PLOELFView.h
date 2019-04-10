#ifndef LLD_ELF_PLO_ELF_VIEW_H
#define LLD_ELF_PLO_ELF_VIEW_H

#include <map>
#include <memory>

#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::MemoryBufferRef;
using llvm::object::ELFObjectFile;
using llvm::object::ELFSectionRef;
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
			 const uint32_t O, const MemoryBufferRef &FR);

  ELFView(const StringRef &VN, const uint32_t O, const MemoryBufferRef &FR) :
    ViewName(VN), Ordinal(O), FileRef(FR) {}
  virtual ~ELFView();

  MemoryBufferRef GetFileRef() const { return FileRef; }

  virtual bool Init() = 0;

  virtual void BuildCfgs() = 0;

  StringRef       ViewName;
  const uint32_t  Ordinal;
  MemoryBufferRef FileRef;

  map<StringRef, unique_ptr<ELFCfg>> Cfgs;
  void EraseCfg(ELFCfg *&CfgPtr);
};

template <class ELFT>
class ELFViewImpl : public ELFView {
public:
  unique_ptr<ELFObjectFile<ELFT>> ViewFile;

  ELFViewImpl(const StringRef &VN,
	      const uint32_t Ordinal,
	      const MemoryBufferRef &FR)
    : ELFView(VN, Ordinal, FR) {}
  virtual ~ELFViewImpl() {}

  bool Init() override;

  void BuildCfgs() override;

  section_iterator GetRelaSectIter(const uint16_t shndx);
private:
  // Section "Idx" -> Section that relocates "Idx",
  map<uint16_t, section_iterator> RelocationSectionMap;

  void InitRelocationSectionMap();

};

}  // namespace plo
}  // namespace llvm
#endif
