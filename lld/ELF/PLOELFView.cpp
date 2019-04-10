#include "PLOELFView.h"

#include "PLO.h"
#include "PLOELFCfg.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFObjectFile.h"

using llvm::object::ELFSectionRef;
using llvm::StringRef;

namespace lld {
namespace plo {

ELFView *
ELFView::Create(const StringRef &VN, const uint32_t Ordinal,
		const MemoryBufferRef &FR) {
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
  auto R = ELFObjectFile<ELFT>::create(FileRef);
  if (!R) {
    return false;
  }
  ViewFile.reset(new ELFObjectFile<ELFT>(std::move(*R)));
  InitRelocationSectionMap();
  return true;
}

template <class ELFT>
void ELFViewImpl<ELFT>::InitRelocationSectionMap() {
  for (auto I = ViewFile->section_begin(), J = ViewFile->section_end();
       I != J; ++I) {
    ELFSectionRef SecRef = *I;
    if (SecRef.getType() == llvm::ELF::SHT_RELA) {
      auto R = SecRef.getRelocatedSection();
      assert(R != J);
      RelocationSectionMap.emplace(R->getIndex(), *I);
    }
  }
}

template <class ELFT>
section_iterator ELFViewImpl<ELFT>::GetRelaSectIter(const uint16_t shndx) {
  auto I = RelocationSectionMap.find(shndx);
  if (I != RelocationSectionMap.end())
    return I->second;
  return ViewFile->section_end();
  // for (auto I = ViewFile->section_begin(), J = ViewFile->section_end();
  //      I != J; ++I) {
  //   auto R = I->getRelocatedSection();
  //   if (R != J && R->getIndex() == shndx) {
  //     return I;
  //   }
  // }
  // return ViewFile->section_end();
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
