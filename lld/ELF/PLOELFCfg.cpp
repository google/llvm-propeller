#include "PLOELFCfg.h"

#include "PLO.h"
#include "PLOELFView.h"

#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <ostream>

#include "llvm/Object/ObjectFile.h"
// Needed by ELFSectionRef & ELFSymbolRef.
#include "llvm/Object/ELFObjectFile.h"

using llvm::object::RelocationRef;
using llvm::object::SectionRef;
using llvm::object::section_iterator;
using llvm::object::SymbolRef;
using llvm::StringRef;

using std::list;
using std::map;
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

ELFCfgNode *ELFCfg::CreateNode(uint64_t Shndx, StringRef &ShName,
                               uint64_t ShSize, uint64_t MappedAddress) {
  // auto E = Nodes.emplace(new ELFCfgNode(
  //                            Shndx, ShName, ShSize, MappedAddress, this));
  auto *N = new ELFCfgNode(Shndx, ShName, ShSize, MappedAddress, this);
  Nodes2[MappedAddress].emplace_back(N);
  return N;
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

void ELFCfgBuilder::BuildCfgs() {
  auto Symbols = View->ViewFile->symbols();
  map<StringRef, list<SymbolRef>> Groups;
  for (const SymbolRef &Sym : Symbols) {
    auto R = Sym.getType();
    auto S = Sym.getName();
    if (R && S && *R == SymbolRef::ST_Function) {
      StringRef SymName = *S;
      auto IE = Groups.emplace(
           std::piecewise_construct,
           std::forward_as_tuple(SymName),
           std::forward_as_tuple(1, Sym));
      (void)(IE.second);
      assert(IE.second);
    }
  }

  // Now we have a map of function names, group "funcname.bb.x".
  for (const SymbolRef &Sym : Symbols) {
    if ((Sym.getFlags() & SymbolRef::SF_Global) != 0) break;
    auto NameOrErr = Sym.getName();
    if (!NameOrErr) continue;
    StringRef SymName(*NameOrErr);
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
          L->second.push_back(Sym);
        }
      }
    }
  }

  map<uint64_t, ELFCfg *> AddrCfgMap;
  for (auto &I : Groups) {
    assert(I.second.size() >= 1);
    SymbolRef CfgSym = *(I.second.begin());
    unique_ptr<ELFCfg> Cfg(new ELFCfg(View, I.first));
    for (SymbolRef Sym: I.second) {
      auto SymNameE = Sym.getName();
      auto SectionIE = Sym.getSection();
      if (SymNameE && SectionIE &&
          (*SectionIE) != Sym.getObject()->section_end()) {
        StringRef SymName = *SymNameE;
        uint64_t SymShndx = (*SectionIE)->getIndex();
        uint64_t SymSize = llvm::object::ELFSymbolRef(Sym).getSize();
        auto ResultP = Plo.SymAddrSizeMap.find(SymName);
        if (ResultP != Plo.SymAddrSizeMap.end()) {
          Cfg->CreateNode(SymShndx, SymName, SymSize,
                          ResultP->second.first);
          continue;
        }
      }
      Cfg.reset(nullptr);
      break;
    }
    if (!Cfg) continue;

    uint64_t CfgMappedAddr = Cfg->GetEntryNode()->MappedAddr;
    auto ExistingI = AddrCfgMap.find(CfgMappedAddr);
    if (ExistingI != AddrCfgMap.end()) {
      auto *ExistingCfg = ExistingI->second;
      // Discard smaller cfg that begins on the same address.
      if (ExistingCfg->Nodes2.size() >= Cfg->Nodes2.size())
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
  }  // Enf of processing all groups.
}

void ELFCfgBuilder::BuildRelocationSectionMap(
    map<uint64_t, section_iterator> &RelocationSectionMap) {
  for (section_iterator I = View->ViewFile->section_begin(),
         J = View->ViewFile->section_end(); I != J; ++I) {
    SectionRef SecRef = *I;
    if (llvm::object::ELFSectionRef(SecRef).getType() == llvm::ELF::SHT_RELA) {
      section_iterator R = SecRef.getRelocatedSection();
      assert(R != J);
      RelocationSectionMap.emplace(R->getIndex(), *I);
    }
  }
}

void ELFCfgBuilder::BuildShndxNodeMap(
    ELFCfg &Cfg,
    map<uint64_t, ELFCfgNode *> &ShndxNodeMap) {
  for (auto &NodeL: Cfg.Nodes2) {
    for (auto &Node: NodeL.second) {
      auto InsertResult = ShndxNodeMap.emplace(Node->Shndx,
                                               Node.get());
      (void)(InsertResult);
      assert(InsertResult.second);
    }
  }
}

void ELFCfgBuilder::BuildCfg(ELFCfg &Cfg, const SymbolRef &CfgSym) {
  assert(Cfg.Nodes2.size() >= 1);

  map<uint64_t, ELFCfgNode *> ShndxNodeMap;
  BuildShndxNodeMap(Cfg, ShndxNodeMap);

  map<uint64_t, section_iterator> RelocationSectionMap;
  BuildRelocationSectionMap(RelocationSectionMap);

  list<ELFCfgEdge *> RSCEdges;
  for (auto &NPair : Cfg.Nodes2) {
    for (auto &N: NPair.second) {
      ELFCfgNode *SrcNode = N.get();
      auto RelaSecRefI = RelocationSectionMap.find(SrcNode->Shndx);
      if (RelaSecRefI == RelocationSectionMap.end())
	continue;

      for (const RelocationRef &Rela : RelaSecRefI->second->relocations()) {
	SymbolRef RSym = *(Rela.getSymbol());
	bool IsRSC = (CfgSym == RSym);

	// All bb section symbols are local symbols.
	if (!IsRSC &&
	    ((RSym.getFlags() & llvm::object::BasicSymbolRef::SF_Global) != 0))
	  continue;

	auto SectionIE = RSym.getSection();
	if (!SectionIE) continue;
	uint64_t SymShndx((*SectionIE)->getIndex());
	ELFCfgNode *TargetNode{nullptr};
	auto Result = ShndxNodeMap.find(SymShndx);
	if (Result != ShndxNodeMap.end()) {
	  TargetNode = Result->second;
	  if (TargetNode) {
	    ELFCfgEdge *E = Cfg.CreateEdge(SrcNode, SrcNode->Outs,
					   TargetNode, TargetNode->Ins,
					   IsRSC ? ELFCfgEdge::INTRA_RSC :
					   ELFCfgEdge::INTRA_FUNC);
	    if (IsRSC) RSCEdges.push_back(E);
	  }
	}
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
    for (auto &NPair : Cfg.Nodes2) {
      for (auto &N: NPair.second) {
	if (N->Outs.size() == 0 ||
	    (N->Outs.size() == 1 &&
	     (*N->Outs.begin())->Type == ELFCfgEdge::INTRA_RSC)) {
	  Cfg.CreateEdge(N.get(),
			 N->Outs,
			 REdge->Src,
			 REdge->Src->Ins,
			 ELFCfgEdge::INTRA_RSR);
	}
      }
    }
  }
  CalculateFallthroughEdges(Cfg);
}

// Calculate fallthroughs.  Edge P->Q is fallthrough if P & Q are
// adjacent, and there is an NORMAL edge from P->Q.
void ELFCfgBuilder::CalculateFallthroughEdges(ELFCfg &Cfg) {
  auto SetupFallthrough =
    [&Cfg](ELFCfgNode *N1, ELFCfgNode *N2) {
      for (auto *E : N1->Outs) {
        if (E->Type == ELFCfgEdge::INTRA_FUNC && E->Sink == N2) {
          N1->FTEdge = E;
          return true;
        }
      }
      if (N1->ShSize == 0) {
        // An empty section always fallthrough to the next adjacent section.
        N1->FTEdge = Cfg.CreateEdge(N1, N1->Outs,
                                    N2, N2->Ins,
                                    ELFCfgEdge::INTRA_FUNC);
        return true;
      }
      return false;
    };

  struct SameComparator {
    bool operator() (const unique_ptr<ELFCfgNode>& P1,
                     const unique_ptr<ELFCfgNode>& P2) {
      if (P1->ShSize == 0)
	return true;
      for (auto *E : P1->Outs) {
	if (E->Type == ELFCfgEdge::INTRA_FUNC && E->Sink == P2.get())
	  return true;
      }
      return false;
    }
  };

  for (auto &Pair: Cfg.Nodes2) {
    auto &NodeL = Pair.second;
    if (NodeL.size() > 1) {
      NodeL.sort(SameComparator());
      for (auto P = NodeL.begin(), Q = std::next(P), E = NodeL.end();
	   Q != E; ++P, ++Q) {
	SetupFallthrough((*P).get(), (*Q).get());
      }
    }
  }

  // bool dbg = (Cfg.Name == "_ZNK5clang6driver10toolchains11Generic_GCC10GCCVersion11isOlderThanEiiiN4llvm9StringRefE");
  for (auto P = Cfg.Nodes2.begin(), Q = std::next(P), E = Cfg.Nodes2.end();
       Q != E; ++P, ++Q) {
    ELFCfgNode *PNode = P->second.rbegin()->get();
    ELFCfgNode *QNode = Q->second.begin()->get();
    SetupFallthrough(PNode, QNode);
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
  }
}

ostream & operator << (ostream &Out, const ELFCfgNode &Node) {
  Out << (Node.ShName == Node.Cfg->Name ? "<Entry>" :
          Node.ShName.data() + Node.Cfg->Name.size() + 1)
      << " [size=" << std::noshowbase << std::dec << Node.ShSize << ", "
      << " addr=" << std::showbase << std::hex << Node.MappedAddr << ", "
      << " shndx=" << std::noshowbase << std::dec << Node.Shndx << "]";
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
  Out << "Cfg: '" << Cfg.View->ViewName.str() << ":"
      << Cfg.Name.str() << "'" << std::endl;
  for (auto &NPair : Cfg.Nodes2) {
    for (auto &N: NPair.second) {
      auto &Node = *N;
      Out << "  Node: " << Node << std::endl;
      for (auto &Edge: Node.Outs) {
	Out << "    " << *Edge
	    << (Edge == Node.FTEdge ? " (*FT*)" : "")
	    << std::endl;
      }
    }
    for (auto &N : Cfg.InterEdges) {
      auto *Edge = N.get();
      Out << "  Calls: '" << Edge->Sink->Cfg->Name.str() << "': "
	  << std::noshowbase << std::dec << Edge->Weight << std::endl;
    }
    Out << std::endl;
  }
  return Out;
}


}  // namespace plo
}  // namespace lld
