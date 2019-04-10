#include "PLOELFView.h"

#include "PLO.h"
#include "PLOELFCfg.h"

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

using llvm::object::ELFSectionRef;
using llvm::StringRef;

namespace lld {
namespace plo {

ELFView *
ELFView::Create(const StringRef &VN, const uint32_t Ordinal,
		const MemoryBufferRef FR) {
  const char *FH = FR.getBufferStart();
  if (FR.getBufferSize() <= 6) return nullptr;
  if (FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' && FH[3] == 'F') {
    char EClass = FH[4];
    char EData = FH[5];
    if (0 < EClass && EClass <= 2 && 0 < EData && EData <= 2) {
      if (EClass == 1 && EData == 1)
        return new ELFViewImpl<llvm::object::ELF32LE>(VN, Ordinal, FR);
      if (EClass == 1 && EData == 2)
        return new ELFViewImpl<llvm::object::ELF32BE>(VN, Ordinal, FR);
      if (EClass == 2 && EData == 1)
        return new ELFViewImpl<llvm::object::ELF64LE>(VN, Ordinal, FR);
      if (EClass == 2 && EData == 2)
        return new ELFViewImpl<llvm::object::ELF64BE>(VN, Ordinal, FR);
    }
  }
  return nullptr;
}

ELFView::~ELFView() {}
void ELFView::EraseCfg(ELFCfg *&CfgPtr) {
  auto I = Cfgs.find(CfgPtr->Name);
  assert(I != Cfgs.end());
  I->second.reset(nullptr);
  Cfgs.erase(I);
  CfgPtr = nullptr;
}

template <class ELFT>
bool ELFViewImpl<ELFT>::Init() {
  // ViewFile::create is an extremely cheap op.
  auto EVF = ViewFile::create(FileRef.getBuffer());
  auto R = ELFObjectFile<ELFT>::create(FileRef);
  if (R) {
    FilePtr.reset(new ELFObjectFile<ELFT>(std::move(*R)));
  }
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
ELFSectionRef ELFViewImpl<ELFT>::getELFSectionRef(const uint16_t shndx) const {
  auto I = FilePtr->section_begin(), E = FilePtr->section_end();
  for (uint16_t i = 0; i < shndx && I != E; ++i, ++I);
  return *I;
}

template <class ELFT>
section_iterator ELFViewImpl<ELFT>::getRelaSectIter(const uint16_t shndx) {
  for (auto I = FilePtr->section_begin(), J = FilePtr->section_end();
       I != J; ++I) {
    auto R = I->getRelocatedSection();
    if (R != J && R->getIndex() == shndx) {
      return I;
    }
  }
  return FilePtr->section_end();
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

bool PLO::ELFViewOrdinalComparator::Impl(const ELFView *A, const ELFView *B) {
  return A->Ordinal < B->Ordinal;
}

template class ELFViewImpl<llvm::object::ELF32LE>;
template class ELFViewImpl<llvm::object::ELF32BE>;
template class ELFViewImpl<llvm::object::ELF64LE>;
template class ELFViewImpl<llvm::object::ELF64BE>;

}  // namespace plo
}  // namespace lld
