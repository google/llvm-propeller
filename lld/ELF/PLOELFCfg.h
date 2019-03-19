#ifndef LLD_ELF_PLO_ELF_CFG_H
#define LLD_ELF_PLO_ELF_CFG_H

#include "PLOELFView.h"

#include <list>
#include <memory>

#include <llvm/ADT/StringRef.h>

using llvm::StringRef;

namespace lld {
namespace plo {

class ELFBlock;
class ELFView;

class ELFCfgNode {
 public:
  const uint16_t          Shndx;
  StringRef               ShName;
  std::list<ELFCfgNode *> Ins;
  std::list<ELFCfgNode *> Outs;
  std::list<double>       Weights;

  ELFCfgNode(const uint16_t _Shndx, const StringRef &_ShName)
    : Shndx(_Shndx), ShName(_ShName), Ins(0), Outs(0), Weights(0) {}
};

class ELFCfg {
 public:
  StringRef Name;
  std::list<std::unique_ptr<ELFCfgNode>> Nodes;

  ELFCfg(const StringRef &N) : Name(N) {}
  void Diagnose() const;

  ELFCfg() {}
};

template <class ELFT>
class ELFCfgBuilder {
 public:
  using ViewFileShdr = typename ELFViewImpl<ELFT>::ViewFileShdr;
  using ViewFileSym  = typename ELFViewImpl<ELFT>::ViewFileSym;
  using ViewFileRela = typename ELFViewImpl<ELFT>::ViewFileRela;
  using ELFTUInt     = typename ELFViewImpl<ELFT>::ELFTUInt;

  ELFViewImpl<ELFT> *View;

  std::list<std::unique_ptr<ELFCfg>> Cfgs;

  ELFCfgBuilder(ELFViewImpl<ELFT> *V) : View(V) {}
  void BuildCfgs();

protected:
  void BuildCfg(ELFCfg &Cfg);
};

}  // namespace plo
}  // namespace lld
#endif
