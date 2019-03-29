#include "PLO.h"
#include "PLOELFView.h"

#include <list>
#include <map>
#include <memory>

#include "llvm/Object/ELFTypes.h"

using llvm::StringRef;
using std::list;
using std::map;
using std::unique_ptr;

namespace lld {
namespace plo {

void ELFCfg::Diagnose() const {
  size_t S = Name.size();
  auto DisplayName = [this, S](const char *T) {
		       if (Name.data() == T)
			 return "<Entry>";
		       return T + S + 1;
		     };
  fprintf(stderr, "Cfg for '%s'\n",
	  Name.data());
  const char *TypeStr[] = {"", " (*RSC*)", " (*RSR*)", " (*OTHER)"};
  for (auto &N : Nodes) {
    fprintf(stderr, " %s (0x%lx)\n", DisplayName(N.second->ShName.data()), N.second->MappedAddr);
    for (auto &Edge : N.second->Outs) {
      auto *Src = Edge->Src;
      auto *Dst = Edge->Sink;
      fprintf(stderr, "\t%s -> %s%s[%08ld]%s\n",
	      DisplayName(Src->ShName.data()),
	      DisplayName(Dst->ShName.data()),
	      Edge == N.second->FTEdge ? " (*FT*) " : "",
	      Edge->Weight,
	      TypeStr[Edge->Type]);
    }
  }
}

ELFCfgEdge *ELFCfg::CreateEdge(ELFCfgNode *From, ELFCfgNode *To,
			       typename ELFCfgEdge::EdgeType Type) {
  ELFCfgEdge *Edge = new ELFCfgEdge(From, To, Type);
  From->Outs.push_back(Edge);
  To->Ins.push_back(Edge);
  Edges.emplace_back(Edge);  // Take ownership of the edge.
  return Edge;
}

bool ELFCfg::MarkPath(ELFCfgNode *From, ELFCfgNode *To) {
  if (From == To) return true;
  ELFCfgNode *P = From;
  while (P && P != To) {
    P = P->FTEdge ? P->FTEdge->Sink : nullptr;
  }
  if (!P) {
    // fprintf(stderr, "Failed to mark path: %s -> %s.\n",
    // 	    From->ShName.str().c_str(),
    // 	    To->ShName.str().c_str());
    return false;
  }
  return true;
}


// Handling Recursive calls
void ELFCfg::MapBranch(ELFCfgNode *From, ELFCfgNode *To) {
  for (auto &E : From->Outs) {
    if (E->Sink == To) {
      ++(E->Weight);
      return;
    }
  }
  ++(CreateEdge(From, To, ELFCfgEdge::OTHER)->Weight);
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
    const ViewFileSym *CfgSym = *(I.second.begin());
    unique_ptr<ELFCfg> Cfg(new ELFCfg(I.first));
    Cfg->Size = ELFT::Is64Bits ? uint64_t(CfgSym->st_size) : uint32_t(CfgSym->st_size);
    for (const ViewFileSym *Sym : I.second) {
      StringRef SymName(StrTab + uint32_t(Sym->st_name));
      ELFCfgNode *N = new ELFCfgNode(Sym->st_shndx, SymName);
      auto ResultP = Plo.SymAddrSizeMap.find(SymName);
      if (ResultP != Plo.SymAddrSizeMap.end()) {
	N->MappedAddr = ResultP->second.first;
	Cfg->Nodes.emplace(N->MappedAddr, N);
      } else {
	Cfg.reset(nullptr); // discard invalid cfgs;
	break;
      }
    }

    if (Cfg) {
      BuildCfg(*Cfg, CfgSym);
      // Transfer ownership of Cfg to View.Cfgs.
      // Cfg->Diagnose();
      View->Cfgs.emplace(Cfg->Name, Cfg.release());
    }
  }
}

template <class ELFT>
void ELFCfgBuilder<ELFT>::BuildCfg(ELFCfg &Cfg, const ViewFileSym *CfgSym) {
  assert(Cfg.Nodes.size() >= 1);
  auto Symbols = View->getSymbols();

  bool UsingMap = false;
  map<uint16_t, ELFCfgNode *> ShndxNodeMap;
  if (Cfg.Nodes.size() >= 100) {
    UsingMap = true;
    // For crazy large CFG, create map to accerate lookup.
    for (auto &Node: Cfg.Nodes) {
      auto InsertResult = ShndxNodeMap.emplace(Node.second->Shndx,
					       Node.second.get());
      if (!InsertResult.second) {
	assert(false);
	fprintf(stderr, "internal error, please check.\n");
	return ;
      }
    }
  }
  list<ELFCfgEdge *> RSCEdges;
  for (auto &N : Cfg.Nodes) {
    ELFCfgNode *SrcNode = N.second.get();
    auto Relas = View->getRelasForSection(SrcNode->Shndx);
    for (const ViewFileRela &Rela : Relas) {
      uint32_t RSym = Rela.getSymbol(false);
      assert(RSym < Symbols.size());
      auto &Sym = Symbols[RSym];
      bool IsRSC = (CfgSym == &Sym);
      // All bb section symbols are local symbols.
      if (!IsRSC && Sym.getBinding() != llvm::ELF::STB_LOCAL)
	continue;
      uint16_t SymShndx(Sym.st_shndx);
      ELFCfgNode *TargetNode{nullptr};
      if (UsingMap) {
	auto Result = ShndxNodeMap.find(SymShndx);
	if (Result != ShndxNodeMap.end()) {
	  TargetNode = Result->second;
	}
      } else {
	for (auto &T : Cfg.Nodes) {
	  if (T.second->Shndx == SymShndx) {
	    TargetNode = T.second.get();
	    break;
	  }
	}
      }
      if (TargetNode) {
	ELFCfgEdge *E = Cfg.CreateEdge(SrcNode, TargetNode,
				       IsRSC ? ELFCfgEdge::RSC :
				       ELFCfgEdge::NORMAL);
	if (IsRSC) RSCEdges.push_back(E);
      }
    }
  }

  // Create recursive-self-return edges for all exit edges.
  // In the following example, create an edge bb5->bb3
  // FuncA:
  //    bb1:	        <---+
  //        ...		    |
  //    bb2:		    |
  //        ...		    |   R(ecursie)-S(elf)-C(all) edge
  //    bb3:		    |
  //        ...		    |
  //        call FuncA  --- +
  //        xxx yyy     <---+
  //        ...		    |
  //    bb4:		    |
  //        ...		    |   R(ecursie)-S(elf)-R(eturn) edge
  //    bb5:		    |
  //        ...		    |
  //        ret   ----------+
  
  for (auto *REdge : RSCEdges) {
    for (auto &N : Cfg.Nodes) {
      if (N.second->Outs.size() == 0 ||
	  (N.second->Outs.size() == 1 &&
	   (*N.second->Outs.begin())->Type == ELFCfgEdge::RSC)) {
	Cfg.CreateEdge(N.second.get(), REdge->Src, ELFCfgEdge::RSR);
      }
    }
  }

  // Calculate fallthroughs.  Edge P->Q is fallthrough if P & Q are
  // adjacent, and there is an NORMAL edge from P->Q.
  for (auto P = Cfg.Nodes.begin(), Q = std::next(P), E = Cfg.Nodes.end();
       Q != E; ++P, ++Q) {
    for (auto &E: P->second->Outs) {
      if (E->Sink == Q->second.get()) {
	P->second->FTEdge = E;
	break;
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
