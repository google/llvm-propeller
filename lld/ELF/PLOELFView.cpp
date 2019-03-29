#include "PLOELFView.h"

#include "PLO.h"

#include <unistd.h>

#include <iterator>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFTypes.h"

using llvm::StringRef;

namespace lld {
namespace plo {

ELFView *
ELFView::Create(const StringRef &VN, const MemoryBufferRef FR) {
  const char *FH = FR.getBufferStart();
  if (FR.getBufferSize() <= 6) return nullptr;
  if (FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' && FH[3] == 'F') {
    char EClass = FH[4];
    char EData = FH[5];
    if (0 < EClass && EClass <= 2 && 0 < EData && EData <= 2) {
      if (EClass == 1 && EData == 1)
        return new ELFViewImpl<llvm::object::ELF32LE>(VN, FR);
      if (EClass == 1 && EData == 2)
        return new ELFViewImpl<llvm::object::ELF32BE>(VN, FR);
      if (EClass == 2 && EData == 1)
        return new ELFViewImpl<llvm::object::ELF64LE>(VN, FR);
      if (EClass == 2 && EData == 2)
        return new ELFViewImpl<llvm::object::ELF64BE>(VN, FR);
    }
  }
  return nullptr;
}

template <class ELFT>
bool ELFViewImpl<ELFT>::Init() {
  // ViewFile::create is an extremely cheap op.
  auto EVF = ViewFile::create(FileRef.getBuffer());
  if (!EVF) return false;
  return initEhdr(*EVF) && initSections(*EVF);
}

template <class ELFT>
bool ELFViewImpl<ELFT>::initEhdr(const ViewFile &VF) {
  const ViewFileEhdr *EHDR = VF.getHeader();
  if (uint16_t(EHDR->e_phnum)) {
    // PHDR in object file is not supported yet.
    return false;
  }
  if (uint16_t(EHDR->e_ehsize) != sizeof(*EHDR))
    return false;
  EhdrPos = Blocks.emplace(Blocks.begin(), new ELFBlock(
      ELFBlock::Ty::EHDR_BLK,
      StringRef((const char *)EHDR, sizeof(*EHDR))));
  return true;
}

template <class ELFT>
bool ELFViewImpl<ELFT>::initSections(const ViewFile &VF) {
  uint16_t SecNum = uint16_t(VF.getHeader()->e_shnum);
  FirstSectPos = Blocks.end();
  FirstShdrPos = Blocks.end();
  uint16_t ShStrNdx = uint16_t(VF.getHeader()->e_shstrndx);
  for (int i = SecNum - 1; i >= 0; --i) {
    auto ErrOrShdr = VF.getSection(i);
    if (!ErrOrShdr) return false;
    const ViewFileShdr *Shdr = *ErrOrShdr;
    auto EContents = VF.template getSectionContentsAsArray<char>(Shdr);
    if (EContents) {
      const ArrayRef<char> Contents = *EContents;
      FirstSectPos = Blocks.emplace(
          FirstSectPos,
          new ELFBlock(ELFBlock::Ty::SECT_BLK,
                       StringRef(Contents.data(), Contents.size())));
      FirstShdrPos = Blocks.emplace(
          FirstShdrPos, new ELFBlock(
              ELFBlock::Ty::SHDR_BLK,
              StringRef((const char *)Shdr, sizeof(*Shdr))));
      if (i == ShStrNdx) {
        ShStrSectPos = FirstSectPos;
        ShStrShdrPos = FirstShdrPos;
      }
    } else {
      return false;
    }
  }
  return setupSymTabAndSymTabStrPos();
}

template <class ELFT>
void ELFViewImpl<ELFT>::Diagnose() const {
  const ViewFileEhdr *Ehdr = getEhdr(EhdrPos->get());
  fprintf(stderr, "ELF Header%s: shoff: %lu, shnum: %u, shstrndx: %u\n",
          (*EhdrPos)->isWritable() ? "*" : "",
          (uint64_t)ELFTUInt(Ehdr->e_shoff),
          uint16_t(Ehdr->e_shnum),
          uint16_t(Ehdr->e_shstrndx));
  int SectIdx = 0;
  int ShdrIdx = 0;
  for (auto &VB : Blocks) {
    switch (VB->getType()) {
      case ELFBlock::Ty::EHDR_BLK:
        break;
      case ELFBlock::Ty::SHDR_BLK:
        {
          const ViewFileShdr *Shdr = getShdr(VB.get());
          StringRef SecName = getSectionName(Shdr);
          fprintf(stderr,
                  "Shdr [%d]%s: %s, offset: %lu, size: %lu, "
                  "sh_link: %u, sh_info: %u\n",
                  ShdrIdx++,
                  VB->isWritable() ? "*" : "",
                  SecName.data(),
                  (uint64_t)ELFTUInt(Shdr->sh_offset),
                  (uint64_t)ELFTUInt(Shdr->sh_size),
                  uint32_t(Shdr->sh_link),
                  uint32_t(Shdr->sh_info));
          break;
        }
      case ELFBlock::Ty::SECT_BLK:
        fprintf(stderr, "Section content [%d]%s: size: %lu\n",
                SectIdx++,
                VB->isWritable() ? "*" : "",
                VB->getSize());
        break;
      default: {}
    }
  }
}

template <class ELFT>
void ELFViewImpl<ELFT>::SortShdrs() {
  assert(check());
  auto ShdrBlksIR = getShdrBlocks();
  int J = 0;
  std::unordered_map<ELFBlock *, int> Order;
  for (auto I = ++ShdrBlksIR.begin(); I != ShdrBlksIR.end(); ++I)
    Order.insert(std::pair<ELFBlock *, int>((*I).get(), ++J));

  std::list<std::pair<ELFBlock *, ELFBlock *>> SecTwin;
  for (BlockIter A = FirstSectPos, B = FirstShdrPos;
       B != Blocks.end(); ++A, ++B) {
    SecTwin.emplace_back(A->get(), B->get());
  }
  auto TwinLess = [this](const std::pair<ELFBlock *, ELFBlock *> &P1,
                         const std::pair<ELFBlock *, ELFBlock *> &P2) -> bool {
                    const ViewFileShdr *S1 = getShdr(P1.second);
                    const ViewFileShdr *S2 = getShdr(P2.second);
                    return ELFTUInt(S1->sh_offset) < ELFTUInt(S2->sh_offset);
                  };
  SecTwin.sort(TwinLess);
  std::map<uint32_t, uint32_t> IdxMap;
  J = 0;
  for (const auto &P : SecTwin) {
    int OldIdx = Order[P.second];
    fprintf(stderr, "(shenhan): %d->%d\n", OldIdx, J);
    IdxMap[OldIdx] = J++;
  }
  auto C = SecTwin.begin();
  for (BlockIter A = FirstSectPos, B = FirstShdrPos;
       B != Blocks.end(); ++A, ++B, ++C) {
    A->release();
    A->reset(C->first);
    B->release();
    B->reset(C->second);
  }

  // Update sh_link and sh_info field.
  for (auto A = FirstShdrPos; A != Blocks.end(); ++A) {
    const ViewFileShdr *Shdr = getShdr(A->get());
    switch (uint32_t(Shdr->sh_type)) {
      case llvm::ELF::SHT_REL:
      case llvm::ELF::SHT_RELA:
        {
          ViewFileShdr *S = getWritableShdr(A->get());
          S->sh_info = IdxMap[uint32_t(S->sh_info)];
        }
        LLVM_FALLTHROUGH;
      case llvm::ELF::SHT_DYNAMIC:
      case llvm::ELF::SHT_HASH:
      case llvm::ELF::SHT_SYMTAB:
      case llvm::ELF::SHT_DYNSYM:
        {
          ViewFileShdr *S = getWritableShdr(A->get());
          S->sh_link = IdxMap[uint32_t(S->sh_link)];
          break;
        }
      default: {}
    }
  }

  ViewFileEhdr *Ehdr = getWritableEhdr((*EhdrPos).get());
  Ehdr->e_shstrndx = uint16_t(IdxMap[uint16_t(Ehdr->e_shstrndx)]);

  BlockIter P = FirstSectPos, Q = FirstShdrPos;
  for (int I = 0, J = (uint16_t)(Ehdr->e_shstrndx); I < J; ++I) {
    ++P;
    ++Q;
  }
  assert(uint32_t(getShdr(Q->get())->sh_type) == llvm::ELF::SHT_STRTAB);
  assert(ELFTUInt(getShdr(Q->get())->sh_size) == (*P)->getSize());
  ShStrSectPos = P;
  ShStrShdrPos = Q;
  setupSymTabAndSymTabStrPos();
  assert(check());
}

// Parameters with "_" needs to be converted into ELFT specific values.
template <class ELFT>
void ELFViewImpl<ELFT>::AppendSection(StringRef SecName,
                                      uint32_t SecType,
                                      uint64_t _SecFlags,
                                      uint64_t _SecAddr,
                                      uint64_t &_OutputSecOffset,
                                      uint64_t _SecSize,
                                      uint32_t SecLink,
                                      uint32_t SecInfo,
                                      uint64_t _SecAlign,
                                      uint64_t _SecEntSize,
                                      char *SecContent) {
  assert(check());
  ELFTUInt SecFlags = ELFTUInt(_SecFlags);
  ELFTUInt SecAddr = ELFTUInt(_SecAddr);
  ELFTUInt OutputSecOffset = ELFTUInt(_OutputSecOffset);
  ELFTUInt SecSize = ELFTUInt(_SecSize);
  ELFTUInt SecAlign = ELFTUInt(_SecAlign);
  ELFTUInt SecEntSize = ELFTUInt(_SecEntSize);

  // Find .shstrtab & its Shdr
  ViewFileEhdr *Ehdr = getWritableEhdr(EhdrPos->get());
  BlockIter B = FirstSectPos;
  BlockIter C = FirstShdrPos;
  for (int I = 0; I < uint16_t(Ehdr->e_shstrndx); ++I, ++B, ++C) {}
  ViewFileShdr *StrTabShdr = getWritableShdr(C->get());
  assert(uint32_t(StrTabShdr->sh_type) == llvm::ELF::SHT_STRTAB);
  assert((*B)->getSize() == ELFTUInt(StrTabShdr->sh_size));
  uint64_t OldSecContentSize = ELFTUInt(StrTabShdr->sh_size);
  uint64_t IncreaseSizeBy = SecName.size() + 1;
  (*B)->resizeOnWrite(OldSecContentSize + IncreaseSizeBy);
  char *StrTab = (*B)->getWritableData();
  memcpy(StrTab + OldSecContentSize, SecName.data(), SecName.size());
  *(StrTab + OldSecContentSize + IncreaseSizeBy) = '\0';

  // Append new block.
  auto SecBlock = new ELFBlock(ELFBlock::Ty::SECT_BLK, SecSize);
  memcpy(SecBlock->getWritableData(), SecContent, SecSize);
  // Insert section block right before the first shdr block.
  Blocks.emplace(FirstShdrPos, SecBlock);

  // Figure out last section. Shdrs are not ordered by sh_offset. Pick up the
  // Shdr with largest sh_offset.
  const ViewFileShdr *LastShdr = nullptr;
  auto ShdrIR = getShdrBlocks();
  for (auto SS = ShdrIR.begin(), SE = ShdrIR.end(); SS != SE; ++SS) {
    if (!LastShdr ||
        ELFTUInt(getShdr(SS->get())->sh_offset) >
        ELFTUInt(LastShdr->sh_offset)) {
      LastShdr = getShdr(SS->get());
    }
  }

  OutputSecOffset = ELFTUInt(LastShdr->sh_offset) + ELFTUInt(LastShdr->sh_size);

  // Append new block shdr.
  // New sec name starts from the last byte of origin strtab.
  uint32_t SecNameOffset = uint32_t(OldSecContentSize);
  Blocks.emplace(Blocks.end(), createShdrBlock(
      SecNameOffset,
      SecType,
      SecFlags,
      SecAddr,
      OutputSecOffset,
      SecSize,
      SecLink,
      SecInfo,
      SecAlign,
      SecEntSize));

  // Push all sections (that must include the newly created Block) that follow
  // StrTab by strlen(SecName) + 1.
  for (auto SS = ShdrIR.begin(), SE = ShdrIR.end(); SS != SE; ++SS) {
    if (ELFTUInt(getShdr(SS->get())->sh_offset) >
        ELFTUInt(StrTabShdr->sh_offset)) {
      getWritableShdr(SS->get())->sh_offset +=
          ELFTUInt(SecName.size() + 1);
    }
  }
  OutputSecOffset += SecName.size() + 1;

  // Increase StrTabShdr size.
  StrTabShdr->sh_size += ELFTUInt(IncreaseSizeBy);

  // Updatr Ehdr.
  Ehdr->e_shnum += uint16_t(1);
  ELFTUInt NewShOff = ELFTUInt(Ehdr->e_shoff);
  NewShOff += SecSize + SecName.size() + 1;
  Ehdr->e_shoff = alignTo(NewShOff);

  setupSymTabAndSymTabStrPos();

  assert(check());
}

template <class ELFT>
bool
ELFViewImpl<ELFT>::Write(const int OutFd) const {
  // EHDR
  assert(check());
  const ViewFileEhdr *Ehdr = getEhdr(EhdrPos->get());
  assert(uint16_t(Ehdr->e_ehsize) == (*EhdrPos)->getSize());
  lseek(OutFd, 0, SEEK_SET);
  write(OutFd, (*EhdrPos)->getContent(), (*EhdrPos)->getSize());
  // Section contents
  off_t FShdrWPos = (off_t)(ELFTUInt(Ehdr->e_shoff));
  for (BlockIter A = FirstSectPos, B = FirstShdrPos;
       A != FirstShdrPos; ++A, ++B) {
    const ViewFileShdr *Shdr = getShdr(B->get());
    lseek(OutFd, FShdrWPos, SEEK_SET);
    write(OutFd, (*B)->getContent(), (*B)->getSize());
    FShdrWPos += (*B)->getSize();
    assert((*B)->getSize() == uint16_t(Ehdr->e_shentsize));
    if (ELFTUInt(Shdr->sh_size) > 0) {
      lseek(OutFd, off_t(ELFTUInt(Shdr->sh_offset)), SEEK_SET);
      write(OutFd, (*A)->getContent(), (*A)->getSize());
    }
  }
  return true;
}

template <class ELFT>
bool ELFViewImpl<ELFT>::check() const {
  BlockIter A = EhdrPos, B = FirstShdrPos;
  assert(++A == FirstSectPos);
  uint16_t ShNum = 0;
  for (ConstBlockIter C = Blocks.end(); B != C; ++A, ++B) {
    const ViewFileShdr *Shdr = getShdr(B->get());
    assert(ELFTUInt(Shdr->sh_size) == (*A)->getSize());
    if (uint32_t(Shdr->sh_type) == llvm::ELF::SHT_STRTAB) {
      assert((*A)->getContent()[0] == '\0');
      assert((*A)->getContent()[(*A)->getSize() - 1] == '\0');
    }
    ++ShNum;
  }
  assert(ShNum == uint16_t(getEhdr(EhdrPos->get())->e_shnum));
  assert(ELFTUInt(getShdr(FirstShdrPos->get())->sh_offset) == 0);
  assert(A == FirstShdrPos);
  assert(B == Blocks.end());
  assert(ELFTUInt(getShdr(ShStrShdrPos->get())->sh_size) ==
         (*ShStrSectPos)->getSize());
  assert(uint32_t(getShdr(ShStrShdrPos->get())->sh_type) == llvm::ELF::SHT_STRTAB);
  uint16_t I = 0, J = uint16_t(getEhdr(EhdrPos->get())->e_shstrndx);
  for (A = FirstSectPos, B = FirstShdrPos; I < J; ++I, ++A, ++B) {}
  assert(A == ShStrSectPos);
  assert(B == ShStrShdrPos);

  assert(uint32_t(getShdr(SymTabShdrPos->get())->sh_type) == llvm::ELF::SHT_SYMTAB);
  assert((*SymTabStrSectPos)->getSize() ==
         ELFTUInt(getShdr(SymTabStrShdrPos->get())->sh_size));
  assert((*SymTabStrSectPos)->getContent()[0] == '\0');
  assert(*((*SymTabStrSectPos)->getContent() +
           (*SymTabStrSectPos)->getSize()) == '\0');

  // Check overlap.
  struct RangeComp {
    bool operator()(const std::pair<ELFTUInt, ELFTUInt> &P1,
                    const std::pair<ELFTUInt, ELFTUInt> &P2) const {
      return P1.first < P2.first;
    }
  };
  std::set<std::pair<ELFTUInt, ELFTUInt>, RangeComp> Ranges;
  auto HasOverlap =
      [&Ranges] (ELFTUInt Start, ELFTUInt Size) -> bool {
        auto I = Ranges.lower_bound(std::pair<ELFTUInt, ELFTUInt>(Start, Size));
        if (I != Ranges.end()) {
          // Special case, I points to a zero size section, no need to check.
          if (I->second) {
            if (Start + Size > I->first)
              return true;
          }
          if (I != Ranges.begin()) {
            --I;
            if (I->second) {
              if (I->first + I->second > Start)
                return true;
            }
          }
        }
        return false;
      };
  (void)(static_cast<void *>(&HasOverlap));  // To avoid release build warnings.

  Ranges.emplace(0, sizeof((*EhdrPos)->getSize()));
  ELFTUInt ShOff = ELFTUInt(getEhdr(EhdrPos->get())->e_shoff);
  uint16_t ShEntSize = uint16_t(getEhdr(EhdrPos->get())->e_shentsize);
  for (BlockIter A = FirstSectPos, B = FirstShdrPos;
       B != Blocks.end(); ++A, ++B) {
    ELFTUInt Start = ELFTUInt(getShdr(B->get())->sh_offset);
    ELFTUInt Size = ELFTUInt(getShdr(B->get())->sh_size);
    assert(!HasOverlap(Start, Size));
    Ranges.emplace(Start, Size);

    assert(!HasOverlap(ShOff, ShEntSize));
    Ranges.emplace(ShOff, ShEntSize);
    ShOff += ShEntSize;
  }
  return true;
}

template <class ELFT>
void ELFViewImpl<ELFT>::BuildCfgs() {
  ELFCfgBuilder<ELFT> CfgBuilder(this);
  CfgBuilder.BuildCfgs();
  Plo.TotalBB += CfgBuilder.BB;
  Plo.TotalBBWoutAddr += CfgBuilder.BBWoutAddr;
  Plo.ValidCfgs += this->Cfgs.size();
  Plo.InvalidCfgs += CfgBuilder.InvalidCfgs;
}

template class ELFViewImpl<llvm::object::ELF32LE>;
template class ELFViewImpl<llvm::object::ELF32BE>;
template class ELFViewImpl<llvm::object::ELF64LE>;
template class ELFViewImpl<llvm::object::ELF64BE>;

}  // namespace plo
}  // namespace lld
