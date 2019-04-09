#include "PLOELFCfg.h"

#include "PLO.h"
#include "PLOELFView.h"

#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <ostream>

#include "llvm/Object/ELFTypes.h"

using llvm::StringRef;
using std::endl;
using std::list;
using std::map;
using std::ostream;
using std::unique_ptr;

namespace lld {
namespace plo {

ELFCfgEdge *ELFCfg::CreateEdge(ELFCfgNode *From,
                               list<ELFCfgEdge *>& FromOuts,
                               ELFCfgNode *To,
                               list<ELFCfgEdge *>& ToIns,
                               typename ELFCfgEdge::EdgeType Type) {
  ELFCfgEdge *Edge = new ELFCfgEdge(From, To, Type);
  FromOuts.push_back(Edge);
  ToIns.push_back(Edge);
  EmplaceEdge(Edge);  // Take ownership of "Edge".
  return Edge;
}

ELFCfgNode *ELFCfg::CreateNode(uint16_t Shndx, StringRef &ShName,
                               uint64_t ShSize, uint64_t MappedAddress) {
  auto E = Nodes.emplace(MappedAddress,
                         new ELFCfgNode(Shndx, ShName, ShSize, MappedAddress, this));
  return E->second.get();
}

bool ELFCfg::MarkPath(ELFCfgNode *From, ELFCfgNode *To) {
  if (From == To) return true;
  ELFCfgNode *P = From;
  while (P && P != To) {
    P = P->FTEdge ? P->FTEdge->Sink : nullptr;
  }
  if (!P) {
    // fprintf(stderr, "Failed to mark path: %s -> %s.\n",
    //              From->ShName.str().c_str(),
    //              To->ShName.str().c_str());
    return false;
  }
  return true;
}

void ELFCfg::MapBranch(ELFCfgNode *From, ELFCfgNode *To) {
  for (auto &E : From->Outs) {
    if (E->Sink == To) {
      ++(E->Weight);
      return;
    }
  }
  ++(CreateEdge(From, From->Outs, To, To->Ins, ELFCfgEdge::INTRA_DYNA)->Weight);
}

void ELFCfg::MapCallOut(ELFCfgNode *From, ELFCfgNode *To) {
  assert(From->Cfg == this);
  assert(From->Cfg != To->Cfg);
  for (auto &E : From->CallOuts) {
    if (E->Sink == To) {
      ++(E->Weight);
      return ;
    }
  }
  ++(CreateEdge(From,
                From->CallOuts,
                To,
                To->CallIns,
                ELFCfgEdge::INTER_FUNC)->Weight);
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

  map<uint64_t, ELFCfg *> AddrCfgMap;
  // bool dbg = false;
  for (auto &I : Groups) {
    const ViewFileSym *CfgSym = *(I.second.begin());
    unique_ptr<ELFCfg> Cfg(new ELFCfg(I.first));
    Cfg->Size = ELFT::Is64Bits ?
      uint64_t(CfgSym->st_size) : uint32_t(CfgSym->st_size);
    for (const ViewFileSym *Sym : I.second) {
      StringRef SymName(StrTab + uint32_t(Sym->st_name));
      uint16_t SymShndx = uint16_t(Sym->st_shndx);
      uint64_t SymSize = View->getSectionSize(SymShndx);
      auto ResultP = Plo.SymAddrSizeMap.find(SymName);
      if (ResultP != Plo.SymAddrSizeMap.end()) {
        Cfg->CreateNode(SymShndx, SymName, SymSize,
                        ResultP->second.first);
      } else {
        Cfg.reset(nullptr); // discard invalid cfgs;
        break;
      }
    }
    if (!Cfg) continue;

    uint64_t CfgMappedAddr = Cfg->Nodes.begin()->second->MappedAddr;
    auto ExistingI = AddrCfgMap.find(CfgMappedAddr);
    if (ExistingI != AddrCfgMap.end()) {
      auto *ExistingCfg = ExistingI->second;
      // Discard smaller cfg that begins on the same address.
      if (ExistingCfg->Nodes.size() >= Cfg->Nodes.size())
        Cfg.reset(nullptr);
      else
        View->EraseCfg(ExistingCfg);
    }
    if (Cfg) {
      AddrCfgMap[CfgMappedAddr] = Cfg.get();
      BuildCfg(*Cfg, CfgSym);
      // Transfer ownership of Cfg to View.Cfgs.
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
      (void)(InsertResult);
      assert(InsertResult.second);
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
        ELFCfgEdge *E = Cfg.CreateEdge(SrcNode, SrcNode->Outs,
                                       TargetNode, TargetNode->Ins,
                                       IsRSC ? ELFCfgEdge::INTRA_RSC :
                                       ELFCfgEdge::INTRA_FUNC);
        if (IsRSC) RSCEdges.push_back(E);
      }
    }
  }

  // Create recursive-self-return edges for all exit edges.
  // In the following example, create an edge bb5->bb3
  // FuncA:
  //    bb1:            <---+
  //        ...                     |
  //    bb2:                |
  //        ...                     |   R(ecursie)-S(elf)-C(all) edge
  //    bb3:                |
  //        ...                     |
  //        call FuncA  --- +
  //        xxx yyy     <---+
  //        ...                     |
  //    bb4:                |
  //        ...                     |   R(ecursie)-S(elf)-R(eturn) edge
  //    bb5:                |
  //        ...                     |
  //        ret   ----------+

  for (auto *REdge : RSCEdges) {
    for (auto &N : Cfg.Nodes) {
      if (N.second->Outs.size() == 0 ||
          (N.second->Outs.size() == 1 &&
           (*N.second->Outs.begin())->Type == ELFCfgEdge::INTRA_RSC)) {
        Cfg.CreateEdge(N.second.get(),
                       N.second->Outs,
                       REdge->Src,
                       REdge->Src->Ins,
                       ELFCfgEdge::INTRA_RSR);
      }
    }
  }

  CalculateFallthroughEdges(Cfg);
}

// Calculate fallthroughs.  Edge P->Q is fallthrough if P & Q are
// adjacent, and there is an NORMAL edge from P->Q.
template <class ELFT>
void ELFCfgBuilder<ELFT>::CalculateFallthroughEdges(ELFCfg &Cfg) {
  auto SetupFallthrough =
    [](typename decltype(ELFCfg::Nodes)::iterator I1,
       typename decltype(ELFCfg::Nodes)::iterator I2) {
      ELFCfgNode *N1 = I1->second.get();
      ELFCfgNode *N2 = I2->second.get();
      for (auto *E : N1->Outs) {
        if (E->Type == ELFCfgEdge::INTRA_FUNC && E->Sink == N2) {
          N1->FTEdge = E;
          return true;
        }
      }
      return false;
    };

  for (auto P = Cfg.Nodes.begin(), Q = std::next(P), E = Cfg.Nodes.end();
       Q != E; ++P, ++Q) {
    if (P->first != Q->first) {
      // Normal case.
      SetupFallthrough(P, Q);
      continue;
    }
    // Very rare case: we have AT MOST 2 BBs with same address
    // mapping, which is possible (but very rare, the scenario
    // explained below). For example:
    //   P[addr=0x123] Q[addr=0x123] R(=std::next(Q))[addr=0x125]
     
    // Firstly,  either P fallthroughs Q or Q fallthroughs P must happen.
    // Secondly:
    //     if P fallthroughs Q, then test if Q fallthroughs R.
    //     if Q fallthroughs P, then test if P fallthroughs R.

    // One example that 2 BB symbols have same address,
    //   00000000018ed2f0  _ZN4llvm12InstCombiner16foldSelectIntoOpERNS_10SelectInstEPNS_5ValueES4_.bb.35
    //   00000000018ed2f0 _ZN4llvm12InstCombiner16foldSelectIntoOpERNS_10SelectInstEPNS_5ValueES4_.bb.4

    // In -fbasicblock-section=all mode:
    // Disassembly of section .text.special_basic_block:
    // 0000000000000000 <_ZN4llvm12InstCombiner16foldSelectIntoOpERNS_10SelectInstEPNS_5ValueES4_.bb.4>:
    //    0:   e9 00 00 00 00          jmpq   5 <.L.str.2+0x2>
    
    // Disassembly of section .text:
    // 0000000000000000 <_ZN4llvm12InstCombiner16foldSelectIntoOpERNS_10SelectInstEPNS_5ValueES4_.bb.35>:
    //    0:   45 31 ed                xor    %r13d,%r13d
    //    3:   4d 85 f6                test   %r14,%r14
    //    6:   0f 84 00 00 00 00       je     c <_ZN4llvm12InstCombiner16foldSelectIntoOpERNS_10SelectInstEPNS_5ValueES4_.bb.35+0xc>
    //    c:   e9 00 00 00 00          jmpq   11 <.L.str.4+0x2>
    
    // The reason is that in -fbasicblock-section mode, bb.4 contains
    // only 1 jump. However, in BB instrument mode, the jump is not
    // emitted, so bb.4 collapses into an empty block, sharing the
    // same address as bb.35.
    assert(std::next(Q) == E || Q->first != std::next(Q)->first);
    if (SetupFallthrough(P, Q)) {
      ;
    } else if (SetupFallthrough(Q, P)) {
      // Swap P and Q's position.
      ELFCfgNode *PNode = P->second.release();
      Cfg.Nodes.erase(P);
      P = Q;
      Q = Cfg.Nodes.emplace(PNode->MappedAddr, PNode);
    } else {
      assert(false);
    }
  }
}

ostream & operator << (ostream &Out, const ELFCfgNode &Node) {
  Out << (Node.ShName == Node.Cfg->Name ? "<Entry>" :
          Node.ShName.data() + Node.Cfg->Name.size() + 1)
      << " [size=" << std::noshowbase << std::dec << Node.ShSize << ", "
      << " addr=" << std::showbase << std::hex << Node.MappedAddr << "]";
  return Out;
}

ostream & operator << (ostream &Out, const ELFCfgEdge &Edge) {
  static const char *TypeStr[] = {"", " (*RSC*)", " (*RSR*)", " (*DYNA*)"};
  Out << "Edge: " << *Edge.Src << " -> " << *Edge.Sink
      << " [" << std::setw(12) << std::setfill('0')
      << std::noshowbase << std::dec << Edge.Weight << "]"
      << TypeStr[Edge.Type];
  return Out;
}

ostream & operator << (ostream &Out, const ELFCfg &Cfg) {
  Out << "Cfg: '" << Cfg.Name.str() << "'" << endl;
  for (auto &N : Cfg.Nodes) {
    auto &Node = *(N.second);
    Out << "  Node: " << Node << endl;
    for (auto &Edge: Node.Outs) {
      Out << "    " << *Edge
          << (Edge == Node.FTEdge ? " (*FT*)" : "")
          << endl;
    }
  }
  for (auto &N : Cfg.InterEdges) {
    auto *Edge = N.get();
    Out << "  Calls: '" << Edge->Sink->Cfg->Name.str() << "': "
        << std::noshowbase << std::dec << Edge->Weight << endl;
  }
  Out << endl;
  return Out;
}

template class ELFCfgBuilder<llvm::object::ELF32LE>;
template class ELFCfgBuilder<llvm::object::ELF32BE>;
template class ELFCfgBuilder<llvm::object::ELF64LE>;
template class ELFCfgBuilder<llvm::object::ELF64BE>;

}  // namespace plo
}  // namespace lld
