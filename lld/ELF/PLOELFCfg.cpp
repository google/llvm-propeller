#include "PLOELFCfg.h"
#include "PLOELFView.h"

#include <list>
#include <map>

#include "llvm/Object/ELFTypes.h"

using llvm::StringRef;

namespace lld {
namespace plo {

void ELFCfg::Diagnose() const {
  fprintf(stderr, "Edges for '%s'\n", Name.data());
  for (auto &Src : Nodes) {
    for (auto &Dst : Src->Outs) {
      fprintf(stderr, "%s(%d) -> %s(%d)\n",
	      Src->ShName.data(), Src->Shndx,
	      Dst->ShName.data(), Dst->Shndx);
    }
  }
}

template <class ELFT>
void ELFCfgBuilder<ELFT>::BuildCfgs() {
  std::map<StringRef, std::list<const ViewFileSym *>> Groups;
  auto Symbols = View->getSymbols();
  const char *StrTab = (*View->SymTabStrSectPos)->getContent();
  for (const ViewFileSym &Sym : Symbols) {
    unsigned char T = Sym.getType();
    if (T == llvm::ELF::STT_FUNC) {
      auto IE = Groups.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(StrTab + uint32_t(Sym.st_name)),
          std::forward_as_tuple(1, &Sym));
      (void)(IE.second);
      assert(IE.second);
    }
  }

  // Now we have a map of function names, group "funcname.bb.x".
  for (const ViewFileSym &Sym : Symbols) {
    unsigned char Binding = Sym.getBinding();
    if (Binding != llvm::ELF::STB_LOCAL) break;
    StringRef SymName(StrTab + uint32_t(Sym.st_name));
    auto T = SymName.rsplit('.');
    StringRef RL = T.first, RR = T.second;
    bool AllDigits = true;
    for (const char *P0 = RR.data(), *PN = RR.data() + RR.size();
         P0 != PN; ++P0) {
      if (!(*P0 <= '9' && *P0 >= '0')) {
        AllDigits = false;
        break;
      }
    }
    if (AllDigits) {
      auto T = RL.rsplit('.');
      StringRef RFN = T.first, RBB = T.second;
      if (RBB == "bb") {
        auto L = Groups.find(RFN);
        if (L != Groups.end()) {
          L->second.push_back(&Sym);
        }
      }
    }
  }

  for (auto &I : Groups) {
    if (I.second.size() == 1)
      continue;
    ELFCfg *Cfg = new ELFCfg(I.first);
    for (const ViewFileSym *Sym : I.second) {
      Cfg->Nodes.emplace_back(
          new ELFCfgNode(Sym->st_shndx,
			 StringRef(StrTab + uint32_t(Sym->st_name))));
    }
    BuildCfg(*Cfg);
    Cfg->Diagnose();
    Cfgs.emplace_back(Cfg);
  }
}

template <class ELFT>
void ELFCfgBuilder<ELFT>::BuildCfg(ELFCfg &Cfg) {
  assert(Cfg.Nodes.size() >= 1);
  auto Symbols = View->getSymbols();
  for (auto &SrcNode : Cfg.Nodes) {
    auto Relas = View->getRelasForSection(SrcNode->Shndx);
    for (const ViewFileRela &Rela : Relas) {
      uint32_t RSym = Rela.getSymbol(false);
      assert(RSym < Symbols.size());
      uint16_t SymShndx(Symbols[RSym].st_shndx);
      for (auto &TargetNode : Cfg.Nodes) {
        if (TargetNode->Shndx == SymShndx) {
          // TODO(shenhan): even if a rela in A points to B, it does not
          // necessarily mean A has a jump to B. Check it.
          SrcNode->Outs.push_back(TargetNode.get());
          TargetNode->Ins.push_back(SrcNode.get());
        }
      }
    }
  }
}


template class ELFCfgBuilder<llvm::object::ELF32LE>;
template class ELFCfgBuilder<llvm::object::ELF32BE>;
template class ELFCfgBuilder<llvm::object::ELF64LE>;
template class ELFCfgBuilder<llvm::object::ELF64BE>;

}  // namespace plo
}  // namespace lld
