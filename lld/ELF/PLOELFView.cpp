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
ELFView::Create(const StringRef &VN,
                const uint32_t Ordinal,
		const MemoryBufferRef &FR) {
  const char *FH = FR.getBufferStart();
  if (FR.getBufferSize() > 6 &&
      FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' && FH[3] == 'F') {
    auto R = ObjectFile::createELFObjectFile(FR);
    if (R) {
      return new ELFView(*R, VN, Ordinal, FR);
    }
  }
  return nullptr;
}

void ELFView::EraseCfg(ELFCfg *&CfgPtr) {
  auto I = Cfgs.find(CfgPtr->Name);
  assert(I != Cfgs.end());
  I->second.reset(nullptr);
  Cfgs.erase(I);
  CfgPtr = nullptr;
}

}  // namespace plo
}  // namespace lld
