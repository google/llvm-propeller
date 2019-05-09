#include "PLOELFCfg.h"

#include "PLO.h"
#include "PLOELFView.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <unordered_map>

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
using std::string;
using std::unique_ptr;

namespace lld {
namespace plo {

void ELFCfg::DumpToOS(std::ostream &os) const {
  os << Name.str() << " " << Size << "\n";
  os << Nodes.size() << "\n";

  for (const auto &Node : Nodes) {
    os << Node->Shndx << " " << Node->ShName.str() << " " << Node->MappedAddr
       << " " << Node->ShSize << " " << Node->Freq << "\n";
  }

  os << IntraEdges.size() << "\n";
  for (const auto &Edge : IntraEdges) {
    bool IsFTEdge = (Edge.get() == Edge->Src->FTEdge);
    os << Edge->Src->ShName.str() << " " << Edge->Sink->ShName.str() << " "
       << Edge->Weight << " " << Edge->Type << " " << IsFTEdge << "\n";
  }

  os << InterEdges.size() << "\n";
  for (const auto &Edge : InterEdges) {
    os << Edge->Src->ShName.str() << " " << Edge->Sink->ShName.str() << " "
       << Edge->Weight << " " << Edge->Type << "\n";
  }
}

ELFCfgEdge *ELFCfg::CreateEdge(ELFCfgNode *From,
                               ELFCfgNode *To,
                               typename ELFCfgEdge::EdgeType Type) {
  ELFCfgEdge *Edge = new ELFCfgEdge(From, To, Type);
  if (Type <  ELFCfgEdge::EdgeType::INTER_FUNC_CALL) {
    From->Outs.push_back(Edge);
    To->Ins.push_back(Edge);
  } else {
    From->CallOuts.push_back(Edge);
    To->CallIns.push_back(Edge);
  }
  EmplaceEdge(Edge);  // Take ownership of "Edge".
  return Edge;
}

bool ELFCfg::MarkPath(ELFCfgNode *From, ELFCfgNode *To) {
  assert(From->Cfg == To->Cfg);
  if (From == To) return true;
  ELFCfgNode *P = From;
  while (P && P != To) {
    ++P->Weight;
    if (P->FTEdge) {
      ++P->FTEdge->Weight;
      P = P->FTEdge->Sink;
    } else {
      P = nullptr;
    }
  }
  if (!P) {
    return false;
  }
  ++(P->Weight);
  return true;
}

void ELFCfg::MapBranch(ELFCfgNode *From, ELFCfgNode *To) {
  ++From->Weight;
  ++To->Weight;
  for (auto &E : From->Outs) {
    if (E->Sink == To) {
      ++(E->Weight);
      return;
    }
  }
  ++(CreateEdge(From, To, ELFCfgEdge::INTRA_DYNA)->Weight);
}

void ELFCfg::MapCallOut(ELFCfgNode *From, ELFCfgNode *To, uint64_t ToAddr) {
  assert(From->Cfg == this);
  assert(From->Cfg != To->Cfg);
  ++From->Weight;
  ++To->Weight;
  ELFCfgEdge::EdgeType EdgeType = ELFCfgEdge::INTER_FUNC_RETURN;
  if (To->Cfg->GetEntryNode() == To && ToAddr == To->MappedAddr) {
      EdgeType = ELFCfgEdge::INTER_FUNC_CALL;
  }
  for (auto &E : From->CallOuts) {
    if (E->Sink == To && E->Type == EdgeType) {
      ++(E->Weight);
      return ;
    }
  }
  ++(CreateEdge(From, To, EdgeType)->Weight);
}

void ELFCfgReader::ReadCfgs() {
  std::ifstream fin(CfgFilePath.str());
  if (!fin.good()) {
    fprintf(stderr, "Cannot open file: <%s>.", CfgFilePath.str().c_str());
    exit(0);
  }
  std::unordered_map<std::string, ELFCfgNode*> AllNodes;
  std::list<ELFCfgEdgeBuilder> InterEdges;
  while(true){
    string *CfgName = new string();
    uint64_t CfgSize;
    fin >> *CfgName >> CfgSize;
    if (fin.eof())
      break;
    unique_ptr<ELFCfg> Cfg(new ELFCfg(nullptr, StringRef(*CfgName), CfgSize));
    Cfg->Size = CfgSize;
    unsigned NNodes;
    fin >> NNodes;
    for (unsigned i=0; i<NNodes; ++i){
      uint16_t Shndx;
      string *ShName = new string();
      uint64_t MappedAddr, ShSize, Freq;
      fin >> Shndx >> *ShName >> MappedAddr >> ShSize >> Freq;
      StringRef ShNameRef(*ShName);
      ELFCfgNode * Node = new ELFCfgNode(Shndx, ShNameRef, ShSize, MappedAddr, Cfg.get());
      Node->Freq = Freq;
      AllNodes.emplace(ShNameRef, Node);
      Cfg->Nodes.emplace_back(std::move(Node));
    }
    unsigned NIntraEdges;
    fin >> NIntraEdges;
    for(unsigned i=0; i<NIntraEdges; ++i){
      std::string SrcShName, SinkShName;
      uint64_t Weight;
      uint16_t Type;
      uint8_t IsFTEdge;
      fin >> SrcShName >> SinkShName >> Weight >> Type >> IsFTEdge;
      auto SrcNodeIt = AllNodes.find(SrcShName);
      if(SrcNodeIt == AllNodes.end()){
        fprintf(stderr, "Intra edge Src: %s could not be mapped to Cfg\n", SrcShName.c_str());
        exit(0);
      }
      auto& SrcNode = SrcNodeIt->second;
      auto SinkNodeIt = AllNodes.find(SinkShName);
      if(SinkNodeIt == AllNodes.end()){
        fprintf(stderr, "Intra edge Sink: %s could not be mapped to Cfg\n", SinkShName.c_str());
        exit(0);
      }
      auto& SinkNode = SinkNodeIt->second;
      ELFCfgEdge * Edge = Cfg->CreateEdge(SrcNode, SinkNode, static_cast<ELFCfgEdge::EdgeType>(Type));
      Edge->Weight = Weight;
      if (IsFTEdge)
        SrcNode->FTEdge = Edge;
    }
    unsigned NInterEdges;
    fin >> NInterEdges;
    for(unsigned i=0; i<NInterEdges; ++i){
      std::string SrcShName, SinkShName;
      uint64_t Weight;
      uint16_t Type;
      fin >> SrcShName >> SinkShName >> Weight >> Type;
      InterEdges.push_back(ELFCfgEdgeBuilder(SrcShName, SinkShName, Weight, Type));
    }
    Cfgs.emplace_back(std::move(Cfg));
  }
  for (ELFCfgEdgeBuilder& EdgeBuilder: InterEdges){
    auto SrcNodeIt = AllNodes.find(EdgeBuilder.SrcShName);
    if (SrcNodeIt == AllNodes.end()){
      fprintf(stderr, "Inter edge source was not found\n");
      continue;
    }
    auto SinkNodeIt = AllNodes.find(EdgeBuilder.SinkShName);
    if (SinkNodeIt == AllNodes.end()){
      fprintf(stderr, "Inter edge sink was not found\n");
      continue;
    }
    auto& SrcNode = SrcNodeIt->second;
    auto& SinkNode = SinkNodeIt->second;
    SrcNode->Cfg->CreateEdge(SrcNode, SinkNode, static_cast<ELFCfgEdge::EdgeType>(EdgeBuilder.Type))->Weight = EdgeBuilder.Weight;
  }

  std::sort(Cfgs.begin(), Cfgs.end(), [] (const unique_ptr<ELFCfg>& A, const unique_ptr<ELFCfg>& B){
    const auto& AEntry = A->GetEntryNode();
    const auto& BEntry = B->GetEntryNode();
    assert(AEntry->MappedAddr != BEntry->MappedAddr);
    return AEntry->MappedAddr < BEntry->MappedAddr;
  });
}

void ELFCfgBuilder::BuildCfgs() {
  // fprintf(stderr, "Building Cfgs for %s...\n", Inf->getName().str().c_str());
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
    StringRef BBSymBaseName = PLO::BBSymbol(SymName);
    if (!BBSymBaseName.empty()) {
      auto L = Groups.find(BBSymBaseName);
      if (L != Groups.end()) {
        L->second.push_back(Sym);
      }
    }
  }

  map<uint64_t, ELFCfg *> AddrCfgMap;
  for (auto &I : Groups) {
    assert(I.second.size() >= 1);
    map<uint64_t, list<unique_ptr<ELFCfgNode>>> TmpNodeMap;
    SymbolRef CfgSym = *(I.second.begin());
    StringRef CfgName = I.first;
    uint64_t  CfgSize = llvm::object::ELFSymbolRef(CfgSym).getSize();
    unique_ptr<ELFCfg> Cfg(new ELFCfg(View, CfgName, CfgSize));
    for (SymbolRef Sym: I.second) {
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
        auto ResultP = Plo.Syms.NameMap.find(SymName);
        if (ResultP != Plo.Syms.NameMap.end()) {
          uint64_t MappedAddr = Plo.Syms.GetAddr(ResultP->second);
          TmpNodeMap[MappedAddr].emplace_back(new ELFCfgNode(
              SymShndx, SymName, SymSize, MappedAddr, Cfg.get()));
          continue;
        }
        // Otherwise fallthrough to ditch Cfg & TmpNodeMap.
      }
      TmpNodeMap.clear();
      Cfg.reset(nullptr);
      break;
    }
    if (!Cfg) continue;  // to next Cfg group.

    uint64_t CfgMappedAddr = TmpNodeMap.begin()->first;
    auto ExistingI = AddrCfgMap.find(CfgMappedAddr);
    if (ExistingI != AddrCfgMap.end()) {
      auto *ExistingCfg = ExistingI->second;
      // Discard smaller cfg that begins on the same address.
      if (ExistingCfg->Nodes.size() >= TmpNodeMap.size()) {
        Cfg.reset(nullptr);
      } else {
        View->EraseCfg(ExistingCfg);
        AddrCfgMap.erase(ExistingI);
      }
    }
    if (Cfg) {
      BuildCfg(*Cfg, CfgSym, TmpNodeMap);
      AddrCfgMap[CfgMappedAddr] = Cfg.get();
      View->Cfgs.emplace(Cfg->Name, std::move(Cfg));
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
    map<uint64_t, list<unique_ptr<ELFCfgNode>>> &TmpNodeMap,
    map<uint64_t, ELFCfgNode *> &ShndxNodeMap) {
  for (auto &NodeL: TmpNodeMap) {
    for (auto &Node: NodeL.second) {
      auto InsertResult = ShndxNodeMap.emplace(Node->Shndx,
                                               Node.get());
      (void)(InsertResult);
      assert(InsertResult.second);
    }
  }
}

void ELFCfgBuilder::BuildCfg(ELFCfg &Cfg, const SymbolRef &CfgSym,
                             map<uint64_t,
                                 list<unique_ptr<ELFCfgNode>>> &TmpNodeMap) {
  map<uint64_t, ELFCfgNode *> ShndxNodeMap;
  BuildShndxNodeMap(TmpNodeMap, ShndxNodeMap);

  map<uint64_t, section_iterator> RelocationSectionMap;
  BuildRelocationSectionMap(RelocationSectionMap);

  list<ELFCfgEdge *> RSCEdges;
  for (auto &NPair : TmpNodeMap) {
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
            ELFCfgEdge *E = Cfg.CreateEdge(SrcNode,
                                           TargetNode,
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
      for (auto &N: NPair.second) {
        if (N->Outs.size() == 0 ||
            (N->Outs.size() == 1 &&
             (*N->Outs.begin())->Type == ELFCfgEdge::INTRA_RSC)) {
          Cfg.CreateEdge(N.get(), REdge->Src, ELFCfgEdge::INTRA_RSR);
        }
      }
    }
  }
  CalculateFallthroughEdges(Cfg, TmpNodeMap);
}



// Calculate fallthroughs.  Edge P->Q is fallthrough if P & Q are
// adjacent, and there is a NORMAL edge from P->Q.
void ELFCfgBuilder::CalculateFallthroughEdges(
    ELFCfg &Cfg, map<uint64_t, list<unique_ptr<ELFCfgNode>>> &TmpNodeMap) {
  /*
    TmpNodeMap groups nodes according to their address:
      addr1: [Node1]
      addr2: [Node2, Node3]
      addr3: [Node4]
      addr4: [Node5]
      addr5: [Node6, Node 7]
    For the above example, Node2, Node3 have same "MappedAddr" (*).

    We firstly sort Nodes that have same address, like addr2 and addr5
    groups, within each group, if NodeA has a fallthrough to NodeB, we
    place NodeA before NodeB. (Op. A)

    We then try to find fallthrough relationship between the previous groups's
    last node and curent group's first node. (Op. B)
  */
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
        N1->FTEdge = Cfg.CreateEdge(N1, N2, ELFCfgEdge::INTRA_FUNC);
        return true;
      }
      return false;
    };

  struct CompareNodesWithSameAddrGroup {
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

  // Op. A.
  for (auto &Pair: TmpNodeMap) {
    auto &NodeL = Pair.second;
    if (NodeL.size() > 1) {
      NodeL.sort(CompareNodesWithSameAddrGroup());
      for (auto P = NodeL.begin(), Q = std::next(P), E = NodeL.end();
           Q != E; ++P, ++Q) {
        SetupFallthrough((*P).get(), (*Q).get());
      }
    }
  }

  // Op. B.
  for (auto P = TmpNodeMap.begin(), Q = std::next(P), E = TmpNodeMap.end();
       Q != E; ++P, ++Q) {
    SetupFallthrough(P->second.rbegin()->get(), Q->second.begin()->get());
  }

  // Transfer nodes ownership to Cfg and destroy TmpNodeMap.
  for (auto &Pair: TmpNodeMap) {
    for (auto &Node: Pair.second) {
      Cfg.Nodes.emplace_back(std::move(Node));
      Node.reset(nullptr);
    }
    Pair.second.clear();
  }
  TmpNodeMap.clear();

  // (*) Rare case: we have BBs with same address mapping, which is
  // possible, but very rare.

  // One example that 2 BB symbols have same address,
  //   00000000018ed2f0 _ZN4llvm12InstCombiner16foldSelectIntoOpERNS_10SelectInstEPNS_5ValueES4_.bb.35
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

ostream & operator << (ostream &Out, const ELFCfgNode &Node) {
  Out << (Node.ShName == Node.Cfg->Name ? "<Entry>" :
          Node.ShName.data() + Node.Cfg->Name.size() + 1)
      << " [size=" << std::noshowbase << std::dec << Node.ShSize << ", "
      << " addr=" << std::showbase << std::hex << Node.MappedAddr << ", "
      << " weight=" << std::showbase << std::dec << Node.Weight << ", "
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
      << Cfg.Name.str() << "', size="
      << std::noshowbase << std::dec << Cfg.Size << std::endl;
  for (auto &N : Cfg.Nodes) {
    auto &Node = *N;
    Out << "  Node: " << Node << std::endl;
    for (auto &Edge: Node.Outs) {
      Out << "    " << *Edge
          << (Edge == Node.FTEdge ? " (*FT*)" : "")
          << std::endl;
    }
    for (auto &Edge: Node.CallOuts) {
      Out << "    Calls: '" << Edge->Sink->ShName.str() << "': "
          << std::noshowbase << std::dec << Edge->Weight << std::endl;
    }
  }
  Out << std::endl;
  return Out;
}

bool PLO::ELFViewOrdinalComparator::operator()(const ELFCfg *A,
                                               const ELFCfg *B) {
  return A->View->Ordinal < B->View->Ordinal;
}

}  // namespace plo
}  // namespace lld
