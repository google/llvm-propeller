#ifndef LLD_ELF_PLO_ELF_VIEW_H
#define LLD_ELF_PLO_ELF_VIEW_H

#include <list>
#include <map>
#include <memory>

#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::ArrayRef;
using llvm::MemoryBufferRef;
using llvm::object::ELFFile;
using llvm::object::ELFObjectFile;
using llvm::object::ELFSectionRef;
using llvm::object::section_iterator;
using llvm::StringRef;

using std::list;
using std::map;
using std::unique_ptr;

namespace lld {
namespace plo {

class ELFCfg;

class ELFBlock {
 public:
  enum class Ty {
    EHDR_BLK,
    PHDR_BLK,
    SECT_BLK,
    SHDR_BLK
  };

  ELFBlock(Ty TP, const StringRef &SR)
      : Type(TP), Data(SR) {}
  ~ELFBlock() {}

  Ty getType() const { return Type; }

  const char* getContent() const {
    return Data.data();
  }

  uint64_t getSize() const {
    return Data.size();
  }

 protected:
  Ty        Type;
  StringRef Data;
};

class ELFView {
 public:
  static ELFView *Create(const StringRef &VN,
			 const uint32_t O, const MemoryBufferRef FR);

  using BlockIter = list<unique_ptr<ELFBlock>>::iterator;
  using ConstBlockIter = list<unique_ptr<ELFBlock>>::const_iterator;

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

  // Minimal data maintained for reading into ELF file.
  std::list<std::unique_ptr<ELFBlock>> Blocks;
  BlockIter EhdrPos;           // ELF header block.
  BlockIter FirstSectPos;      // First sect.
  BlockIter FirstShdrPos;      // First section header.
  BlockIter ShStrSectPos;      // Section strtab sect.
  BlockIter ShStrShdrPos;      // Section strtab sect header.
  BlockIter SymTabSectPos;     // Symbol table section.
  BlockIter SymTabShdrPos;     // Symbol table section header.
  BlockIter SymTabStrSectPos;  // Symbol table string table section.
  BlockIter SymTabStrShdrPos;  // Symbol table string table section header.

};

template <class ELFT>
class ELFViewImpl : public ELFView {
public:
  // ELFTUInt is uint64_t when ELFT::Is64 is true, otherwise uint32_t.
  using ELFTUInt     = typename ELFT::uint;

  using ViewFile     = ELFFile<ELFT>;
  using ViewFileEhdr = typename ELFT::Ehdr;
  using ViewFileShdr = typename ELFT::Shdr;
  using ViewFileRela = typename ELFT::Rela;
  using ViewFileSym  = typename ELFT::Sym;

  unique_ptr<ELFObjectFile<ELFT>> FilePtr;

  ELFViewImpl(const StringRef &VN,
	      const uint32_t Ordinal,
	      const MemoryBufferRef &FR)
    : ELFView(VN, Ordinal, FR) {}
  virtual ~ELFViewImpl() {}

  bool Init() override;

  void BuildCfgs() override;

  const ViewFileShdr *getShdr(const ELFBlock *VB) const {
    assert(VB->getType() == ELFBlock::Ty::SHDR_BLK);
    if (VB->getType() != ELFBlock::Ty::SHDR_BLK) return nullptr;
    return static_cast<const ViewFileShdr *>((const void *)(VB->getContent()));
  }

  const ViewFileShdr *getShdr(uint16_t shidx) const {
    BlockIter P = FirstShdrPos;
    for (int I = 0; I != shidx; ++I, ++P) {}
    return getShdr(P->get());
  }

  // Get Relas for section "shndx".
  ArrayRef<ViewFileRela> getRelasForSection(const uint16_t shndx) const {
    ConstBlockIter E = Blocks.cend();
    for (BlockIter P = FirstSectPos, S = FirstShdrPos; S != E; ++P, ++S) {
      const ViewFileShdr *Shdr = getShdr(S->get());
      if (uint32_t(Shdr->sh_type) == llvm::ELF::SHT_RELA &&
          uint32_t(Shdr->sh_info) == shndx) {
        assert(ELFTUInt(Shdr->sh_size) % ELFTUInt(Shdr->sh_entsize) == 0);
        ArrayRef<ViewFileRela> RelaArray(
            reinterpret_cast<const ViewFileRela *>((*P)->getContent()),
            ELFTUInt(Shdr->sh_size) / ELFTUInt(Shdr->sh_entsize));
        return RelaArray;
      }
    }
    return ArrayRef<ViewFileRela>();
  }

  ELFSectionRef getELFSectionRef(const uint16_t shndx) const;

  section_iterator getRelaSectIter(const uint16_t shndx);

  ArrayRef<ViewFileSym> getSymbols() const {
    const ViewFileShdr *SymTabShdr = getShdr(SymTabShdrPos->get());
    const ViewFileSym* Sym0 = reinterpret_cast<const ViewFileSym *>(
        FileRef.getBufferStart() + ELFTUInt(SymTabShdr->sh_offset));
    int NumSym = SymTabShdr->getEntityCount();
    return ArrayRef<ViewFileSym>(Sym0, NumSym);
  }

  StringRef getSymbolName(const ViewFileSym *Sym) const {
    return StringRef((*SymTabStrSectPos)->getContent() +
                     uint32_t(Sym->st_name));
  }

  StringRef getSectionName(const ViewFileShdr *Shdr) const {
    uint32_t sh_name = uint32_t(Shdr->sh_name);
    if (sh_name >= uint32_t(getShdr(ShStrShdrPos->get())->sh_size))
      return StringRef();
    return StringRef((*ShStrSectPos)->getContent() + sh_name);
  }

  uint64_t getSectionSize(uint16_t Shndx) const {
    return ELFT::Is64Bits ? uint64_t(getShdr(Shndx)->sh_size) :
      uint32_t(getShdr(Shndx)->sh_size);
  }

  bool setupSymTabAndSymTabStrPos() {
    BlockIter A = FirstSectPos, B = FirstShdrPos, E = Blocks.end();
    for (; B != E; ++A, ++B) {
      if (uint32_t(getShdr(B->get())->sh_type) == llvm::ELF::SHT_SYMTAB) {
        SymTabSectPos = A;
        SymTabShdrPos = B;
        break;
      }
    }
    // No symtab section.
    if (B == E) return false;
    uint32_t SymTabStrShndx(getShdr(SymTabShdrPos->get())->sh_link);
    SymTabStrSectPos = FirstSectPos;
    SymTabStrShdrPos = FirstShdrPos;
    for (uint32_t I = 0; I != SymTabStrShndx;
         ++I, ++SymTabStrSectPos, ++SymTabStrShdrPos) {}
    return true;
  }

 private:
  bool initEhdr(const ViewFile &VF);
  bool initSections(const ViewFile &VF);
};

}  // namespace plo
}  // namespace llvm
#endif
