#ifndef LLD_ELF_PLO_ELF_VIEW_H
#define LLD_ELF_PLO_ELF_VIEW_H

#include <list>
#include <memory>

#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::ArrayRef;
using llvm::MemoryBufferRef;
using llvm::StringRef;

namespace lld {
namespace plo {

template <class ELFT>
class ELFCfgBuilder;

class ELFBlock {
 public:
  enum class Ty {
    EHDR_BLK,
    PHDR_BLK,
    SECT_BLK,
    SHDR_BLK
  };

  ELFBlock(Ty TP, const StringRef &SR)
      : Type(TP), Data(SR), Writable(nullptr) {}

  ELFBlock(Ty TP, uint32_t BlockSize)
      : Type(TP), Data(),
        Writable(new char[BlockSize]), WritableSize(BlockSize) {}

  ~ELFBlock() {}

  Ty getType() const { return Type; }

  char *getWritableData() {
    assert(Writable.get());
    return Writable.get();
  }

  const char* getContent() const {
    if (isWritable()) return Writable.get();
    return Data.data();
  }

  uint64_t getSize() const {
    return isWritable() ? WritableSize : Data.size();
  }

  void resizeOnWrite(uint64_t NewSize) {
    if (getSize() == NewSize) return;
    assert (getSize() < NewSize);
    char *NewBuf = new char[NewSize];
    memcpy(NewBuf, getContent(), getSize());
    Writable.reset(NewBuf);
    WritableSize = NewSize;
  }

  void copyOnWrite() {
    if (isWritable()) return;
    Writable.reset(new char[Data.size()]);
    memcpy(Writable.get(), Data.data(), Data.size());
    WritableSize = Data.size();
  }

  bool isWritable() const {
    return Writable.get() != nullptr;
  }

 protected:
  Ty        Type;
  StringRef Data;
  std::unique_ptr<char> Writable;
  uint64_t              WritableSize;
};

class ELFView {
 public:
  static ELFView *Create(const MemoryBufferRef FR);

  using BlockIter = std::list<std::unique_ptr<ELFBlock>>::iterator;
  using ConstBlockIter = std::list<std::unique_ptr<ELFBlock>>::const_iterator;

  ELFView(const MemoryBufferRef &FR) : FileRef(FR) {}
  virtual ~ELFView() {}

  MemoryBufferRef GetFileRef() const { return FileRef; }

  virtual bool Init() = 0;

  virtual void AppendSection(StringRef Name,
                             uint32_t Type,
                             uint64_t Flags,
                             uint64_t Addr,
                             uint64_t &OutputOffset,
                             uint64_t Size,
                             uint32_t Link,
                             uint32_t Info,
                             uint64_t Align,
                             uint64_t EntSize,
                             char *Content) = 0;

  virtual void Diagnose() const = 0;
  virtual bool Write(const int OutFd) const = 0;
  virtual void SortShdrs() = 0;

  virtual void BuildCfgs() = 0;

  MemoryBufferRef FileRef;
  std::list<std::unique_ptr<ELFBlock>> Blocks;
  // These iterators are properly maintained before and after any modification.
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
  using ViewFile     = llvm::object::ELFFile<ELFT>;
  using ViewFileEhdr = typename ELFT::Ehdr;
  using ViewFileShdr = typename ELFT::Shdr;
  using ViewFileRela = typename ELFT::Rela;
  using ViewFileSym  = typename ELFT::Sym;

  ELFViewImpl(const MemoryBufferRef &FR) : ELFView(FR) {}
  virtual ~ELFViewImpl() {}

  bool Init() override;

  void Diagnose() const override;

  bool Write(const int OutFd) const override;

  void AppendSection(StringRef SecName,
                     uint32_t  SecType,
                     uint64_t  SecFlags,
                     uint64_t  SecAddr,
                     uint64_t &OutputSecOffset,
                     uint64_t  SecSize,
                     uint32_t  SecLink,
                     uint32_t  SecInfo,
                     uint64_t  SecAlign,
                     uint64_t  SecEntSize,
                     char     *SecContent) override;

  void SortShdrs() override;

  void BuildCfgs() override;

 protected:
  bool check() const;

 private:
  const ViewFileEhdr *getEhdr(const ELFBlock *VB) const {
    assert(VB->getType() == ELFBlock::Ty::EHDR_BLK);
    if (VB->getType() != ELFBlock::Ty::EHDR_BLK) return nullptr;
    return static_cast<ViewFileEhdr *>((void *)(VB->getContent()));
  }

  ViewFileEhdr *getWritableEhdr(ELFBlock *VB) {
    assert(VB->getType() == ELFBlock::Ty::EHDR_BLK);
    VB->copyOnWrite();
    if (VB->getType() != ELFBlock::Ty::EHDR_BLK) return nullptr;
    return static_cast<ViewFileEhdr *>((void *)(VB->getWritableData()));
  }

  const ViewFileShdr *getShdr(const ELFBlock *VB) const {
    assert(VB->getType() == ELFBlock::Ty::SHDR_BLK);
    if (VB->getType() != ELFBlock::Ty::SHDR_BLK) return nullptr;
    return static_cast<ViewFileShdr *>((void *)(VB->getContent()));
  }

  const ViewFileShdr *getShdr(uint16_t shidx) const {
    BlockIter P = FirstShdrPos;
    for (int I = 0; I != shidx; ++I, ++P) {}
    return getShdr(P->get());
  }

  ViewFileShdr *getWritableShdr(ELFBlock *VB) {
    assert(VB->getType() == ELFBlock::Ty::SHDR_BLK);
    VB->copyOnWrite();
    if (VB->getType() != ELFBlock::Ty::SHDR_BLK) return nullptr;
    return static_cast<ViewFileShdr *>((void *)(VB->getWritableData()));
  }

  const ELFBlock *getSect(const uint16_t SecIdx) const {
    auto A = FirstSectPos;
    for (uint16_t I = 0; I != SecIdx; ++I, ++A) {}
    return A->get();
  }

  llvm::iterator_range<BlockIter> getSectBlocks() {
    return llvm::iterator_range<BlockIter>(FirstSectPos, FirstShdrPos);
  }

  llvm::iterator_range<BlockIter> getShdrBlocks() {
    return llvm::iterator_range<BlockIter>(FirstShdrPos, Blocks.end());
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

  ELFBlock *createShdrBlock(uint32_t SecName,
                            uint32_t SecType,
                            ELFTUInt SecFlags,
                            ELFTUInt SecAddr,
                            ELFTUInt SecOffset,
                            ELFTUInt SecSize,
                            uint32_t SecLink,
                            uint32_t SecInfo,
                            ELFTUInt SecAlign,
                            ELFTUInt SecEntSize) {
    ELFBlock *ShdrBlk = new ELFBlock(
        ELFBlock::Ty::SHDR_BLK, sizeof(ViewFileShdr));
    ViewFileShdr *Shdr = getWritableShdr(ShdrBlk);
    Shdr->sh_name = SecName;
    Shdr->sh_type = SecType;
    Shdr->sh_flags = SecFlags;
    Shdr->sh_addr = SecAddr;
    Shdr->sh_offset = SecOffset;
    Shdr->sh_size = SecSize;
    Shdr->sh_link = SecLink;
    Shdr->sh_info = SecInfo;
    Shdr->sh_addralign = SecAlign;
    Shdr->sh_entsize = SecEntSize;
    return ShdrBlk;
  }

  const ELFBlock *getSecBlock(uint16_t shidx) const {
    BlockIter P = FirstSectPos;
    for (int I = 0; I != shidx; ++I, ++P) {}
    return P->get();
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
  ELFTUInt alignTo(ELFTUInt F) {
    uint32_t A = sizeof(ELFTUInt);  // for ELF64LE, A = 8.
    if ((F & (A - 1)) != 0)
      F = (F & (~(A - 1))) + A;
    return F;
  }

  friend class ELFCfgBuilder<ELFT>;
};

}  // namespace plo
}  // namespace llvm
#endif // LLVM_PLO_ELFREWRITER_H
