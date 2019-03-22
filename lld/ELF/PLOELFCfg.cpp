#include "PLO.h"
#include "PLOELFView.h"

#include <list>
#include <map>
#include <memory>

#include "llvm/Object/ELFTypes.h"

using llvm::StringRef;
using std::list;
using std::unique_ptr;

namespace lld {
namespace plo {

void ELFCfg::Diagnose() const {
  fprintf(stderr, "Edges for '%s'\n", Name.data());
  size_t S = Name.size();
  auto DisplayName = [this, S](const char *T) {
		       if (Name.data() == T)
			 return "<Entry>";
		       return T + S + 1;
		     };
  for (auto &N : Nodes) {
    for (auto &Edge : N->Outs) {
      auto *Src = Edge->Src;
      auto *Dst = Edge->Sink;
      fprintf(stderr, "%s(0x%lx) -> %s(0x%lx)\n",
	      DisplayName(Src->ShName.data()), Src->Address,
	      DisplayName(Dst->ShName.data()), Dst->Address);
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
    unique_ptr<ELFCfg> Cfg(new ELFCfg(I.first));
    for (const ViewFileSym *Sym : I.second) {
      StringRef SymName(StrTab + uint32_t(Sym->st_name));
      uint64_t SymAddr = ELFCfgNode::InvalidAddress;
      if (Plo.SymAddrMap.find(SymName) != Plo.SymAddrMap.end()) {
	SymAddr = Plo.SymAddrMap[SymName];
      } else {
	++BBWoutAddr;
	Cfg.reset(nullptr);  // delete the instance
	break;
      }
      ELFCfgNode *N = new ELFCfgNode(Sym->st_shndx, SymName, SymAddr);
      if (!Cfg->EntryNode || Cfg->EntryNode->Address > SymAddr) {
	Cfg->EntryNode = N;
      }
      Cfg->Nodes.emplace_back(N);
      ++BB;
    }
    if (Cfg) {
      BuildCfg(*Cfg);
      // Cfg->Diagnose();
      // Transfer ownership of Cfg to View.Cfgs.
      View->Cfgs.emplace_back(Cfg.release());
    } else {
      ++InvalidCfgs;
    }
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
	  ELFCfgEdge *E = new ELFCfgEdge(SrcNode.get(), TargetNode.get());
          // TODO(shenhan): even if a rela in A points to B, it does not
          // necessarily mean A has a jump to B. Check it.
          SrcNode->Outs.emplace_back(E);
          TargetNode->Ins.push_back(E);
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
