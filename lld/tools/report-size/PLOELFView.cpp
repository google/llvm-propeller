#include "PLOELFView.h"

#if LLVM_ON_UNIX
#include <unistd.h>
#endif

#include <iterator>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"

using llvm::StringRef;

namespace llvm {
namespace plo {

ELFView *ELFView::Create(const MemoryBufferRef FR) {
  const char *FH = FR.getBufferStart();
  if (FR.getBufferSize() <= 6) {
    fprintf(stderr, "File is too small.\n");
    return nullptr;
  }
  if (FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' && FH[3] == 'F') {
    char EClass = FH[4];
    char EData = FH[5];
    if (0 < EClass && EClass <= 2 && 0 < EData && EData <= 2) {
      if (EClass == 1 && EData == 1)
        return new ELFViewImpl<object::ELF32LE>(FR);
      if (EClass == 1 && EData == 2)
        return new ELFViewImpl<object::ELF32BE>(FR);
      if (EClass == 2 && EData == 1)
        return new ELFViewImpl<object::ELF64LE>(FR);
      if (EClass == 2 && EData == 2)
        return new ELFViewImpl<object::ELF64BE>(FR);
    }
  }
  fprintf(stderr, "File has invalid magic number.\n");
  return nullptr;
}

template <class ELFT> bool ELFViewImpl<ELFT>::Init() {
  // ViewFile::create is an extremely cheap op.
  auto EVF = ViewFile::create(FileRef.getBuffer());
  if (!EVF)
    return false;

  return initEhdr(*EVF) && initSections(*EVF);
}

template <class ELFT> bool ELFViewImpl<ELFT>::initEhdr(const ViewFile &VF) {
  const ViewFileEhdr *EHDR = VF.getHeader();
  // if (uint16_t(EHDR->e_phnum)) {
  //   // PHDR in object file is not supported yet.
  //   return false;
  // }
  if (uint16_t(EHDR->e_ehsize) != sizeof(*EHDR))
    return false;
  EhdrPos = Blocks.emplace(
      Blocks.begin(),
      new ELFBlock(ELFBlock::Ty::EHDR_BLK,
                   StringRef((const char *)EHDR, sizeof(*EHDR))));
  return true;
}

template <class ELFT> bool ELFViewImpl<ELFT>::initSections(const ViewFile &VF) {
  uint64_t BufSize = FileRef.getBufferSize();
  uint64_t SecOff = ELFTUInt(VF.getHeader()->e_shoff);
  uint16_t ShdrEntSize = uint16_t(VF.getHeader()->e_shentsize);
  RealSecNum = (BufSize - SecOff) / ShdrEntSize;
  uint16_t ReportedSecNumber = uint16_t(VF.getHeader()->e_shnum);
  // Choose realsecnum or reportedsecnumber, heuristically.
  if (ReportedSecNumber >= 1 && ReportedSecNumber < 10000) {
    RealSecNum = ReportedSecNumber;
  }

  FirstSectPos = Blocks.end();
  FirstShdrPos = Blocks.end();
  uint16_t ShStrNdx = uint16_t(VF.getHeader()->e_shstrndx);
  for (int64_t i = RealSecNum - 1; i >= 0; --i) {
    auto ErrOrShdr = VF.getSection(i);
    if (!ErrOrShdr) {
      return false;
    }
    const ViewFileShdr *Shdr = *ErrOrShdr;
    const char *DataStart;
    size_t DataSize;
    if (Shdr->sh_type == llvm::ELF::SHT_NOBITS) {
      DataStart = nullptr;
      DataSize = 0;
    } else {
      auto EContents = VF.template getSectionContentsAsArray<char>(Shdr);
      if (EContents) {
        DataStart = EContents->data();
        DataSize = EContents->size();
      } else {
        fprintf(stderr,
                "Invalid section (ShNdx=%ld) content presented in file.\n", i);
        return false;
      }
    }
    FirstSectPos = Blocks.emplace(
        FirstSectPos,
        new ELFBlock(ELFBlock::Ty::SECT_BLK, StringRef(DataStart, DataSize)));
    FirstShdrPos = Blocks.emplace(
        FirstShdrPos,
        new ELFBlock(ELFBlock::Ty::SHDR_BLK,
                     StringRef((const char *)Shdr, sizeof(*Shdr))));
    if (i == ShStrNdx) {
      ShStrSectPos = FirstSectPos;
      ShStrShdrPos = FirstShdrPos;
    }
  }
  return setupSymTabAndSymTabStrPos();
}

template <class ELFT> bool ELFViewImpl<ELFT>::setupSymTabAndSymTabStrPos() {
  bool Found = false;
  for (BlockIter A = FirstSectPos, B = FirstShdrPos, E = Blocks.end(); B != E;
       ++A, ++B) {
    if (uint32_t(getShdr(B->get())->sh_type) == ELF::SHT_SYMTAB) {
      SymTabSectPos = A;
      SymTabShdrPos = B;
      Found = true;
      break;
    }
  }
  if (!Found)
    return false;

  uint32_t SymTabStrShndx(getShdr(SymTabShdrPos->get())->sh_link);
  SymTabStrSectPos = FirstSectPos;
  SymTabStrShdrPos = FirstShdrPos;
  for (uint32_t I = 0; I != SymTabStrShndx;
       ++I, ++SymTabStrSectPos, ++SymTabStrShdrPos) {
  }
  return true;
}

template <class ELFT>
bool ELFViewImpl<ELFT>::GetELFSizeInfo(ELFSizeInfo *SizeInfo) {
  memset(SizeInfo, 0, sizeof(ELFSizeInfo));
  for (auto &T : getShdrBlocks()) {
    const ViewFileShdr &hdr = *(getShdr(T.get()));
    ELFTUInt Flags = ELFTUInt(hdr.sh_flags);
    uint32_t Type = uint32_t(hdr.sh_type);
    ELFTUInt SecSize = ELFTUInt(hdr.sh_size);
    if ((Flags & llvm::ELF::SHF_ALLOC) != 0 &&
        (Flags & llvm::ELF::SHF_EXECINSTR) != 0) {
      SizeInfo->TextSize += SecSize;
    } else if ((Flags & llvm::ELF::SHF_ALLOC) != 0) {
      SizeInfo->OtherAllocSize += SecSize;
    }
    if (Type == llvm::ELF::SHT_SYMTAB) {
      SizeInfo->SymTabSize += SecSize;
      SizeInfo->SymTabEntryNum += SecSize / ELFTUInt(hdr.sh_entsize);
      assert(SecSize % ELFTUInt(hdr.sh_entsize) == 0);
    } else if (Type == llvm::ELF::SHT_STRTAB) {
      SizeInfo->StrTabSize += SecSize;
    }
    if (((Flags & llvm::ELF::SHF_ALLOC) != 0) || Type == llvm::ELF::SHT_RELA ||
        Type == llvm::ELF::SHT_REL) {
      StringRef SecName = this->getSectionName(&hdr);
      // .eh_frame_hdr only exists in executables.
      if (SecName == ".eh_frame" || SecName == ".eh_frame_hdr" ||
          SecName == ".rela.eh_frame") {
        SizeInfo->EhFrameRelatedSize += SecSize;
      }
    }
    if (Type == llvm::ELF::SHT_RELA || Type == llvm::ELF::SHT_REL) {
      SizeInfo->RelaSize += SecSize;
    }
  }
  SizeInfo->FileSize = FileRef.getBufferSize();
  return true;
}

template <class ELFT> bool ELFViewImpl<ELFT>::check() const {
  BlockIter A = EhdrPos, B = FirstShdrPos;
  assert(++A == FirstSectPos);
  uint16_t ShNum = 0;
  for (ConstBlockIter C = Blocks.end(); B != C; ++A, ++B) {
    const ViewFileShdr *Shdr = getShdr(B->get());
    assert(ELFTUInt(Shdr->sh_size) == (*A)->getSize());
    if (uint32_t(Shdr->sh_type) == ELF::SHT_STRTAB) {
      assert((*A)->getContent()[0] == '\0');
      assert((*A)->getContent()[(*A)->getSize() - 1] == '\0');
    }
    ++ShNum;
  }
  // The following might not be true in case where shnum > 65536 (possible).
  //  assert(ShNum == uint16_t(getEhdr(EhdrPos->get())->e_shnum));
  assert(ShNum == RealSecNum);

  assert(ELFTUInt(getShdr(FirstShdrPos->get())->sh_offset) == 0);
  assert(A == FirstShdrPos);
  assert(B == Blocks.end());
  assert(ELFTUInt(getShdr(ShStrShdrPos->get())->sh_size) ==
         (*ShStrSectPos)->getSize());
  assert(uint32_t(getShdr(ShStrShdrPos->get())->sh_type) == ELF::SHT_STRTAB);
  uint16_t I = 0, J = uint16_t(getEhdr(EhdrPos->get())->e_shstrndx);
  for (A = FirstSectPos, B = FirstShdrPos; I < J; ++I, ++A, ++B) {
  }
  assert(A == ShStrSectPos);
  assert(B == ShStrShdrPos);

  assert(uint32_t(getShdr(SymTabShdrPos->get())->sh_type) == ELF::SHT_SYMTAB);
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
  auto HasOverlap = [&Ranges](ELFTUInt Start, ELFTUInt Size) -> bool {
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
  (void)(static_cast<void *>(&HasOverlap)); // To avoid release build warnings.

  Ranges.emplace(0, sizeof((*EhdrPos)->getSize()));
  ELFTUInt ShOff = ELFTUInt(getEhdr(EhdrPos->get())->e_shoff);
  uint16_t ShEntSize = uint16_t(getEhdr(EhdrPos->get())->e_shentsize);
  for (BlockIter A = FirstSectPos, B = FirstShdrPos; B != Blocks.end();
       ++A, ++B) {
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

template class ELFViewImpl<object::ELF32LE>;
template class ELFViewImpl<object::ELF32BE>;
template class ELFViewImpl<object::ELF64LE>;
template class ELFViewImpl<object::ELF64BE>;

} // namespace plo
} // namespace llvm
