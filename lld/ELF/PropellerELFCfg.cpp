#include "PropellerELFCfg.h"

#include "Propeller.h"
#include "Symbols.h"
#include "SymbolTable.h"

#include "llvm/Object/ObjectFile.h"
// Needed by ELFSectionRef & ELFSymbolRef.
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <stdio.h>
#include <string>

using llvm::object::ObjectFile;
using llvm::object::RelocationRef;
using llvm::object::SectionRef;
using llvm::object::section_iterator;
using llvm::object::SymbolRef;
using llvm::StringRef;

using std::list;
using std::map;
using std::ostream;
using std::string;
using std::unique_ptr;

namespace lld {
namespace propeller {

bool ELFCfg::writeAsDotGraph(const char *CfgOutName) {
  FILE *fp = fopen(CfgOutName, "w");
  if (!fp) {
    warn("[Propeller]: Failed to open: '" + StringRef(CfgOutName) + "'\n");
    return false;
  }
  fprintf(fp, "digraph %s {\n", Name.str().c_str());
  forEachNodeRef([&fp](ELFCfgNode &N) { fprintf(fp, "%u [size=\"%lu\"];", N.getBBIndex(), N.ShSize); });
  fprintf(fp, "\n");
  for (auto &E : IntraEdges) {
    bool IsFTEdge = (E->Src->FTEdge == E.get());
    fprintf(fp, " %u -> %u [label=\"%lu\", weight=%f];\n", E->Src->getBBIndex(),
            E->Sink->getBBIndex(), E->Weight, IsFTEdge ? 1.0 : 0.1);
  }
  fprintf(fp, "}\n");
  fclose(fp);
  llvm::outs() << "[Propeller]: Done dumping cfg '" << Name.str() << "' into '"
               << CfgOutName << "'.\n";
  return true;
}

ELFCfgEdge *ELFCfg::createEdge(ELFCfgNode *From, ELFCfgNode *To,
                               typename ELFCfgEdge::EdgeType Type) {
  ELFCfgEdge *Edge = new ELFCfgEdge(From, To, Type);
  if (Type < ELFCfgEdge::EdgeType::INTER_FUNC_CALL) {
    From->Outs.push_back(Edge);
    To->Ins.push_back(Edge);
  } else {
    From->CallOuts.push_back(Edge);
    To->CallIns.push_back(Edge);
  }
  emplaceEdge(Edge); // Take ownership of "Edge".
  return Edge;
}

bool ELFCfg::markPath(ELFCfgNode *From, ELFCfgNode *To, uint64_t Cnt) {
  if (From == nullptr) {
    /* If the From Node is null, walk backward from the To Node while only
     * one INTRA_FUNC incoming edge is found. */
    assert(To != nullptr);
    ELFCfgNode *P = To;
    do {
      vector<ELFCfgEdge *> IntraInEdges;
      std::copy_if(P->Ins.begin(), P->Ins.end(),
                   std::back_inserter(IntraInEdges), [this](ELFCfgEdge *E) {
                     return E->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
                            E->Sink != getEntryNode();
                   });
      if (IntraInEdges.size() == 1) {
        P = IntraInEdges.front()->Src;
      } else {
        P = nullptr;
      }
    } while (P && P != To);
    return true;
  }

  if (To == nullptr) {
    /* If the To Node is null, walk forward from the From Node while only
     * one INTRA_FUNC outgoing edge is found. */
    assert(From != nullptr);
    ELFCfgNode *P = From;
    do {
      vector<ELFCfgEdge *> IntraOutEdges;
      std::copy_if(P->Outs.begin(), P->Outs.end(),
                   std::back_inserter(IntraOutEdges), [this](ELFCfgEdge *E) {
                     return E->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
                            E->Sink != getEntryNode();
                   });
      if (IntraOutEdges.size() == 1) {
        P = IntraOutEdges.front()->Sink;
      } else {
        P = nullptr;
      }
    } while (P && P != From);
    return true;
  }

  assert(From->Cfg == To->Cfg);
  if (From == To)
    return true;
  ELFCfgNode *P = From;
  while (P && P != To) {
    if (P->FTEdge) {
      P->FTEdge->Weight += Cnt;
      P = P->FTEdge->Sink;
    } else {
      P = nullptr;
    }
  }
  if (!P) {
    return false;
  }
  return true;
}

void ELFCfg::mapBranch(ELFCfgNode *From, ELFCfgNode *To, uint64_t Cnt,
                       bool isCall, bool isReturn) {
  assert(From->Cfg == To->Cfg);

  for (auto &E : From->Outs) {
    bool EdgeTypeOk = true;
    if (!isCall && !isReturn) {
      EdgeTypeOk = E->Type == ELFCfgEdge::INTRA_FUNC ||
                   E->Type == ELFCfgEdge::INTRA_DYNA;
    } else {
      if (isCall)
        EdgeTypeOk = E->Type == ELFCfgEdge::INTRA_RSC;
      if (isReturn)
        EdgeTypeOk = E->Type == ELFCfgEdge::INTRA_RSR;
    }
    if (EdgeTypeOk && E->Sink == To) {
      E->Weight += Cnt;
      return;
    }
  }

  ELFCfgEdge::EdgeType Type = ELFCfgEdge::INTRA_DYNA;
  if (isCall)
    Type = ELFCfgEdge::INTRA_RSC;
  else if (isReturn)
    Type = ELFCfgEdge::INTRA_RSR;

  createEdge(From, To, Type)->Weight += Cnt;
}

void ELFCfg::mapCallOut(ELFCfgNode *From, ELFCfgNode *To, uint64_t ToAddr,
                        uint64_t Cnt, bool isCall, bool isReturn) {
  assert(From->Cfg == this);
  assert(From->Cfg != To->Cfg);
  ELFCfgEdge::EdgeType EdgeType = ELFCfgEdge::INTER_FUNC_RETURN;
  if (isCall ||
      (ToAddr && To->Cfg->getEntryNode() == To && ToAddr == To->MappedAddr)) {
    EdgeType = ELFCfgEdge::INTER_FUNC_CALL;
  }
  if (isReturn) {
    EdgeType= ELFCfgEdge::INTER_FUNC_RETURN;
  }
  for (auto &E : From->CallOuts) {
    if (E->Sink == To && E->Type == EdgeType) {
      E->Weight += Cnt;
      return ;
    }
  }
  createEdge(From, To, EdgeType)->Weight += Cnt;
}

void ELFCfgBuilder::buildCfgs() {
  auto Symbols = View->ViewFile->symbols();
  map<StringRef, list<SymbolRef>> Groups;
  for (const SymbolRef &Sym : Symbols) {
    auto R = Sym.getType();
    auto S = Sym.getName();
    if (R && S && *R == SymbolRef::ST_Function) {
      StringRef SymName = *S;
      /*
      lld::elf::Symbol *PSym =
          Plo ? Plo->Symtab->find(SymName) : Prop->Symtab->find(SymName);
      if (PSym) (PSym->kind() == lld::elf::Symbol::UndefinedKind)){ 
        fprintf(stderr, "%s UNDEFINED KIND\n", SymName.str().c_str());
        continue;
      }
      */
      auto IE = Groups.emplace(std::piecewise_construct,
                               std::forward_as_tuple(SymName),
                               std::forward_as_tuple(1, Sym));
      (void)(IE.second);
      assert(IE.second);
    }
  }

  // Now we have a map of function names, group "x.bb.funcname".
  for (const SymbolRef &Sym : Symbols) {
    // All bb symbols are local, upon seeing the first global, exit.
    if ((Sym.getFlags() & SymbolRef::SF_Global) != 0)
      break;
    auto NameOrErr = Sym.getName();
    if (!NameOrErr)
      continue;
    StringRef SName = *NameOrErr;
    StringRef FName;
    if (SymbolEntry::isBBSymbol(SName, &FName, nullptr)) {
      auto L = Groups.find(FName);
      if (L != Groups.end()) {
        L->second.push_back(Sym);
      }
    }
  }

  for (auto &I : Groups) {
    assert(I.second.size() >= 1);
    map<uint64_t, unique_ptr<ELFCfgNode>> TmpNodeMap;
    SymbolRef CfgSym = *(I.second.begin());
    StringRef CfgName = I.first;
    unique_ptr<ELFCfg> Cfg(new ELFCfg(View, CfgName, 0));
    for (SymbolRef Sym : I.second) {
      auto SymNameE = Sym.getName();
      auto SectionIE = Sym.getSection();
      if (SymNameE && SectionIE &&
          (*SectionIE) != Sym.getObject()->section_end()) {
        StringRef SymName = *SymNameE;
        uint64_t SymShndx = (*SectionIE)->getIndex();
        // Note here: BB Symbols only carry size information when
        // -fbasicblock-section=all. Objects built with
        // -fbasicblock-section=labels do not have size information
        // for BB symbols.
        uint64_t SymSize = llvm::object::ELFSymbolRef(Sym).getSize();
        // Drop bb sections with no code
        if (!SymSize)
          continue;
        auto *SE = Prop->Propf->findSymbol(SymName);
        if (SE) {
          if (TmpNodeMap.find(SE->Ordinal) != TmpNodeMap.end()) {
            error("Internal error checking Cfg map.");
            return;
          }
          TmpNodeMap.emplace(
              std::piecewise_construct, std::forward_as_tuple(SE->Ordinal),
              std::forward_as_tuple(new ELFCfgNode(SymShndx, SymName, SE->Size,
                                                   SE->Ordinal, Cfg.get())));
          continue;
        }
        // Otherwise fallthrough to ditch Cfg & TmpNodeMap.
      }
      TmpNodeMap.clear();
      Cfg.reset(nullptr);
      break;
    }

    if (TmpNodeMap.empty())
      Cfg.reset(nullptr);

    if (!Cfg){
      continue; // to next Cfg group.
    }

    uint32_t GroupShndx = 0;
    for (auto &T : TmpNodeMap) {
      if (GroupShndx != 0 && T.second->Shndx == GroupShndx) {
        Cfg.reset(nullptr);
        TmpNodeMap.clear();
        error("[Propeller]: Basicblock sections must not have same section "
              "index, this is usually caused by -fbasicblock-sections=labels. "
              "Use -fbasicblock-sections=list/all instead.");
        return ;
      }
      GroupShndx = T.second->Shndx;
    }

    if (Cfg) {
      buildCfg(*Cfg, CfgSym, TmpNodeMap);
      View->Cfgs.emplace(Cfg->Name, std::move(Cfg));
    }
  } // Enf of processing all groups.
}

void ELFCfgBuilder::buildRelocationSectionMap(
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

void ELFCfgBuilder::buildShndxNodeMap(
    map<uint64_t, unique_ptr<ELFCfgNode>> &TmpNodeMap,
    map<uint64_t, ELFCfgNode *> &ShndxNodeMap) {
  for (auto &NodeL : TmpNodeMap) {
    auto &Node = NodeL.second;
    auto InsertResult = ShndxNodeMap.emplace(Node->Shndx, Node.get());
    (void)(InsertResult);
    assert(InsertResult.second);
  }
}

void ELFCfgBuilder::buildCfg(
    ELFCfg &Cfg, const SymbolRef &CfgSym,
    map<uint64_t, unique_ptr<ELFCfgNode>> &TmpNodeMap) {
  map<uint64_t, ELFCfgNode *> ShndxNodeMap;
  buildShndxNodeMap(TmpNodeMap, ShndxNodeMap);

  map<uint64_t, section_iterator> RelocationSectionMap;
  buildRelocationSectionMap(RelocationSectionMap);

  list<ELFCfgEdge *> RSCEdges;
  for (auto &NPair : TmpNodeMap) {
    ELFCfgNode *SrcNode = NPair.second.get();
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
      if (!SectionIE)
        continue;
      uint64_t SymShndx((*SectionIE)->getIndex());
      ELFCfgNode *TargetNode{nullptr};
      auto Result = ShndxNodeMap.find(SymShndx);
      if (Result != ShndxNodeMap.end()) {
        TargetNode = Result->second;
        if (TargetNode) {
          ELFCfgEdge *E = Cfg.createEdge(SrcNode, TargetNode,
                                         IsRSC ? ELFCfgEdge::INTRA_RSC
                                               : ELFCfgEdge::INTRA_FUNC);
          if (IsRSC)
            RSCEdges.push_back(E);
        }
      }
    }
  }

  // Create recursive-self-return edges for all exit edges.
  // In the following example, create an edge bb5->bb3
  // FuncA:
  //    bb1:            <---+
  //        ...             |
  //    bb2:                |
  //        ...             |   R(ecursie)-S(elf)-C(all) edge
  //    bb3:                |
  //        ...             |
  //        call FuncA  --- +
  //        xxx yyy     <---+
  //        ...             |
  //    bb4:                |
  //        ...             |   R(ecursie)-S(elf)-R(eturn) edge
  //    bb5:                |
  //        ...             |
  //        ret   ----------+
  for (auto *REdge : RSCEdges) {
    for (auto &NPair : TmpNodeMap) {
      auto &N = NPair.second;
      if (N->Outs.size() == 0 ||
          (N->Outs.size() == 1 &&
           (*N->Outs.begin())->Type == ELFCfgEdge::INTRA_RSC)) {
        // Now "N" is the exit node.
        Cfg.createEdge(N.get(), REdge->Src, ELFCfgEdge::INTRA_RSR);
      }
    }
  }
  calculateFallthroughEdges(Cfg, TmpNodeMap);

  // Transfer nodes ownership to Cfg and destroy TmpNodeMap.
  for (auto &Pair : TmpNodeMap) {
    Cfg.Nodes.emplace_back(std::move(Pair.second));
    Pair.second.reset(nullptr);
  }
  TmpNodeMap.clear();

  // Set Cfg size and re-calculate size of the entry basicblock, which is
  // initially the size of the whole function.
  Cfg.Size = Cfg.getEntryNode()->ShSize;
  Cfg.forEachNodeRef([&Cfg](ELFCfgNode &N) {
    if (&N != Cfg.getEntryNode())
      Cfg.getEntryNode()->ShSize -= N.ShSize;
  });
}

// Calculate fallthroughs.  Edge P->Q is fallthrough if P & Q are
// adjacent, and there is a NORMAL edge from P->Q.
void ELFCfgBuilder::calculateFallthroughEdges(
    ELFCfg &Cfg, map<uint64_t, unique_ptr<ELFCfgNode>> &TmpNodeMap) {
  /*
    TmpNodeMap groups nodes according to their address:
      addr1: [Node1]
      addr2: [Node2]
      addr3: [Node3]
      addr4: [Node4]
    And addr1 < addr2 < addr3 < addr4.
  */
  auto SetupFallthrough = [&Cfg](ELFCfgNode *N1, ELFCfgNode *N2) {
    for (auto *E : N1->Outs) {
      if (E->Type == ELFCfgEdge::INTRA_FUNC && E->Sink == N2) {
        N1->FTEdge = E;
        return;
      }
    }
    if (N1->ShSize == 0) {
      // An empty section always fallthrough to the next adjacent section.
      N1->FTEdge = Cfg.createEdge(N1, N2, ELFCfgEdge::INTRA_FUNC);
    }
  };

  for (auto P = TmpNodeMap.begin(), Q = std::next(P), E = TmpNodeMap.end();
       Q != E; ++P, ++Q) {
    SetupFallthrough(P->second.get(), Q->second.get());
  }
}

ELFView *ELFView::create(const StringRef &VN, const uint32_t Ordinal,
                         const MemoryBufferRef &FR) {
  const char *FH = FR.getBufferStart();
  if (FR.getBufferSize() > 6 && FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' &&
      FH[3] == 'F') {
    auto R = ObjectFile::createELFObjectFile(FR);
    if (R) {
      return new ELFView(*R, VN, Ordinal, FR);
    }
  }
  return nullptr;
}

ostream &operator<<(ostream &Out, const ELFCfgNode &Node) {
  Out << (Node.ShName == Node.Cfg->Name
              ? "<Entry>"
              : Node.ShName.data() + Node.Cfg->Name.size() + 1)
      << " [size=" << std::noshowbase << std::dec << Node.ShSize << ", "
      << " addr=" << std::showbase << std::hex << Node.MappedAddr << ", "
      << " frequency=" << std::showbase << std::dec << Node.Freq << ", "
      << " shndx=" << std::noshowbase << std::dec << Node.Shndx << "]";
  return Out;
}

ostream &operator<<(ostream &Out, const ELFCfgEdge &Edge) {
  static const char *TypeStr[] = {"", " (*RSC*)", " (*RSR*)", " (*DYNA*)"};
  Out << "Edge: " << *Edge.Src << " -> " << *Edge.Sink << " [" << std::setw(12)
      << std::setfill('0') << std::noshowbase << std::dec << Edge.Weight << "]"
      << TypeStr[Edge.Type];
  return Out;
}

ostream &operator<<(ostream &Out, const ELFCfg &Cfg) {
  Out << "Cfg: '" << Cfg.View->ViewName.str() << ":" << Cfg.Name.str()
      << "', size=" << std::noshowbase << std::dec << Cfg.Size << std::endl;
  for (auto &N : Cfg.Nodes) {
    auto &Node = *N;
    Out << "  Node: " << Node << std::endl;
    for (auto &Edge : Node.Outs) {
      Out << "    " << *Edge << (Edge == Node.FTEdge ? " (*FT*)" : "")
          << std::endl;
    }
    for (auto &Edge : Node.CallOuts) {
      Out << "    Calls: '" << Edge->Sink->ShName.str()
          << "': " << std::noshowbase << std::dec << Edge->Weight << std::endl;
    }
  }
  Out << std::endl;
  return Out;
}

}  // namespace plo
}  // namespace lld
