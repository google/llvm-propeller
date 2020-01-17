#ifndef LLVM_PLO_ELF_VIEW_H
#define LLVM_PLO_ELF_VIEW_H

#include <list>
#include <memory>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace plo {

template <class ELFT> class ELFCfgBuilder;

class ELFBlock {
public:
  enum class Ty { EHDR_BLK, PHDR_BLK, SECT_BLK, SHDR_BLK };

  ELFBlock(Ty TP, const StringRef &SR)
      : Type(TP), Data(SR), Writable(nullptr) {}

  ELFBlock(Ty TP, uint32_t BlockSize)
      : Type(TP), Data(), Writable(new char[BlockSize]),
        WritableSize(BlockSize) {}

  ~ELFBlock() {}

  Ty getType() const { return Type; }

  char *getWritableData() {
    assert(Writable.get());
    return Writable.get();
  }

  const char *getContent() const {
    if (isWritable())
      return Writable.get();
    return Data.data();
  }

  uint64_t getSize() const { return isWritable() ? WritableSize : Data.size(); }

  void resizeOnWrite(uint64_t NewSize) {
    if (getSize() == NewSize)
      return;
    assert(getSize() < NewSize);
    char *NewBuf = new char[NewSize];
    memcpy(NewBuf, getContent(), getSize());
    Writable.reset(NewBuf);
    WritableSize = NewSize;
  }

  void copyOnWrite() {
    if (isWritable())
      return;
    Writable.reset(new char[Data.size()]);
    memcpy(Writable.get(), Data.data(), Data.size());
    WritableSize = Data.size();
  }

  bool isWritable() const { return Writable.get() != nullptr; }

protected:
  Ty Type;
  StringRef Data;
  std::unique_ptr<char> Writable;
  uint64_t WritableSize;
};

struct ELFSizeInfo {
  uint64_t TextSize;
  uint64_t OtherAllocSize;
  uint64_t RelaSize;
  uint64_t EhFrameRelatedSize;
  uint64_t SymTabSize;
  uint64_t SymTabEntryNum;
  uint64_t StrTabSize;
  uint64_t FileSize;

  ELFSizeInfo()
      : TextSize(0), OtherAllocSize(0), RelaSize(0), EhFrameRelatedSize(0),
        SymTabSize(0), SymTabEntryNum(0), StrTabSize(0), FileSize(0) {}

  ELFSizeInfo &operator+=(const ELFSizeInfo &R) {
    TextSize += R.TextSize;
    OtherAllocSize += R.OtherAllocSize;
    RelaSize += R.RelaSize;
    EhFrameRelatedSize += R.EhFrameRelatedSize;
    SymTabSize += R.SymTabSize;
    SymTabEntryNum += R.SymTabEntryNum;
    StrTabSize += R.StrTabSize;
    FileSize += R.FileSize;
    return *this;
  }
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
  virtual bool GetELFSizeInfo(ELFSizeInfo *SizeInfo) = 0;

  MemoryBufferRef FileRef;
  std::list<std::unique_ptr<ELFBlock>> Blocks;
  int64_t RealSecNum;
  // These iterators are properly maintained before and after any modification.
  BlockIter EhdrPos;          // ELF header block.
  BlockIter FirstSectPos;     // First sect.
  BlockIter FirstShdrPos;     // First section header.
  BlockIter ShStrSectPos;     // Section strtab sect.
  BlockIter ShStrShdrPos;     // Section strtab sect header.
  BlockIter SymTabSectPos;    // Symbol table section.
  BlockIter SymTabShdrPos;    // Symbol table section header.
  BlockIter SymTabStrSectPos; // Symbol table string table section.
  BlockIter SymTabStrShdrPos; // Symbol table string table section header.
};

template <class ELFT> class ELFViewImpl : public ELFView {
public:
  // ELFTUInt is uint64_t when ELFT::Is64 is true, otherwise uint32_t.
  using ELFTUInt = typename ELFT::uint;
  using ViewFile = llvm::object::ELFFile<ELFT>;
  using ViewFileEhdr = typename ELFT::Ehdr;
  using ViewFileShdr = typename ELFT::Shdr;
  using ViewFileRela = typename ELFT::Rela;
  using ViewFileSym = typename ELFT::Sym;

  ELFViewImpl(const MemoryBufferRef &FR) : ELFView(FR) {}
  virtual ~ELFViewImpl() {}

  bool Init() override;
  bool GetELFSizeInfo(ELFSizeInfo *SizeInfo) override;

protected:
  bool check() const;

  const ViewFileEhdr *getEhdr(const ELFBlock *VB) const {
    assert(VB->getType() == ELFBlock::Ty::EHDR_BLK);
    if (VB->getType() != ELFBlock::Ty::EHDR_BLK)
      return nullptr;
    return static_cast<ViewFileEhdr *>((void *)(VB->getContent()));
  }

  const ViewFileShdr *getShdr(const ELFBlock *VB) const {
    assert(VB->getType() == ELFBlock::Ty::SHDR_BLK);
    if (VB->getType() != ELFBlock::Ty::SHDR_BLK)
      return nullptr;
    return static_cast<ViewFileShdr *>((void *)(VB->getContent()));
  }

  const ViewFileShdr *getShdr(uint16_t shidx) const {
    BlockIter P = FirstShdrPos;
    for (int I = 0; I != shidx; ++I, ++P) {
    }
    return getShdr(P->get());
  }

  const ELFBlock *getSect(const uint16_t SecIdx) const {
    auto A = FirstSectPos;
    for (uint16_t I = 0; I != SecIdx; ++I, ++A) {
    }
    return A->get();
  }

  llvm::iterator_range<BlockIter> getSectBlocks() {
    return llvm::iterator_range<BlockIter>(FirstSectPos, FirstShdrPos);
  }

  llvm::iterator_range<BlockIter> getShdrBlocks() {
    return llvm::iterator_range<BlockIter>(FirstShdrPos, Blocks.end());
  }

  StringRef getSectionName(const ViewFileShdr *Shdr) const {
    uint32_t sh_name = uint32_t(Shdr->sh_name);
    if (sh_name >= uint32_t(getShdr(ShStrShdrPos->get())->sh_size))
      return StringRef();
    return StringRef((*ShStrSectPos)->getContent() + sh_name);
  }

  bool setupSymTabAndSymTabStrPos();

private:
  bool initEhdr(const ViewFile &VF);
  bool initSections(const ViewFile &VF);
  ELFTUInt alignTo(ELFTUInt F) {
    uint32_t A = sizeof(ELFTUInt); // for ELF64LE, A = 8.
    if ((F & (A - 1)) != 0)
      F = (F & (~(A - 1))) + A;
    return F;
  }
};

} // namespace plo
} // namespace llvm
#endif // LLVM_PLO_ELFREWRITER_H
