//===-------------------- PropellerELFCfg.h -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Class definitions for propeller cfg, edge, nodes and CFGBuilder.
//
// The ELFView class represents one ELF file. The ELFCFGBuilder class builds
// cfg for each function and store it in ELFView::CFGs, indexed by cfg name.
//
// ELFCFGBuilder::buildCFGs works this way:
//   - groups funcName, a.BB.funcName, aa.BB.funcName and alike into one set, 
//     for each set, passes the set to "ELFCFGBuilder::buildCFG"
//   - each element in the set is a section, we then know from its section
//     relocations the connections to other sections. (a)
//   - from (a), we build CFG.
//
// Three important functions in ELFCFG:
//   mapBranch - apply counter to edge A->B, where A, B belong to the same func
//
//   mapCallOut - apply counter to edge A->B, where A, B belong to diff funcs
//
//   markPath - apply counter to all nodes/edges betwee A and B, A and B belong
//              to same func
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_ELF_CFG_H
#define LLD_ELF_PROPELLER_ELF_CFG_H

#include "Propeller.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"

#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <vector>

using llvm::object::ObjectFile;
using llvm::object::SymbolRef;
using llvm::object::section_iterator;

namespace lld {
namespace propeller {

class ELFView;
class ELFCFGNode;
class ELFCFG;

class ELFCFGEdge {
public:
  ELFCFGNode *Src;
  ELFCFGNode *Sink;
  uint64_t    Weight;
  // Whether it's an edge introduced by recursive-self-call.  (Usually
  // calls do not split basic blocks and do not introduce new edges.)
  enum EdgeType : char {
      INTRA_FUNC = 0,
      INTRA_RSC,
      INTRA_RSR,
      // Intra edge dynamically created because of indirect jump, etc.
      INTRA_DYNA,
      INTER_FUNC_CALL,
      INTER_FUNC_RETURN,
  } Type {INTRA_FUNC};

protected:
  ELFCFGEdge(ELFCFGNode *N1, ELFCFGNode *N2, EdgeType T)
    :Src(N1), Sink(N2), Weight(0), Type(T) {}

  friend class ELFCFG;
};

class ELFCFGNode {
 public:
  uint64_t           Shndx;
  StringRef          ShName;
  uint64_t           ShSize;
  uint64_t           MappedAddr;
  uint64_t           Freq;
  ELFCFG            *CFG;
  
  std::vector<ELFCFGEdge *> Outs;      // Intra function edges.
  std::vector<ELFCFGEdge *> Ins;       // Intra function edges.
  std::vector<ELFCFGEdge *> CallOuts;  // Callouts/returns to other functions.
  std::vector<ELFCFGEdge *> CallIns;   // Callins/returns from other functions.
  
  // Fallthrough edge, could be nullptr. And if not, FTEdge is in Outs.
  ELFCFGEdge *       FTEdge;

  const static uint64_t InvalidAddress = -1l;

  unsigned getBBIndex() {
    StringRef FName, BName;
    if (SymbolEntry::isBBSymbol(ShName, &FName, &BName))
      return BName.size();
    else
      return 0;
  }

private:
  ELFCFGNode(uint64_t _Shndx, const StringRef &_ShName,
             uint64_t _Size, uint64_t _MappedAddr, ELFCFG *_Cfg)
    : Shndx(_Shndx), ShName(_ShName), ShSize(_Size),
      MappedAddr(_MappedAddr), Freq(0), CFG(_Cfg),
      Outs(), Ins(), CallOuts(), CallIns(), FTEdge(nullptr) {}

  friend class ELFCFG;
  friend class ELFCFGBuilder;
};

class ELFCFG {
public:
  ELFView    *View;
  StringRef   Name;
  uint64_t    Size;
  
  // ELFCFG assumes the ownership for all Nodes / Edges.
  std::vector<std::unique_ptr<ELFCFGNode>> Nodes;  // Sorted by address.
  std::vector<std::unique_ptr<ELFCFGEdge>> IntraEdges;
  std::vector<std::unique_ptr<ELFCFGEdge>> InterEdges;

  ELFCFG(ELFView *V, const StringRef &N, uint64_t S)
    : View(V), Name(N), Size(S) {}
  ~ELFCFG() {}

  bool markPath(ELFCFGNode *from, ELFCFGNode *to, uint64_t cnt = 1);
  void mapBranch(ELFCFGNode *from, ELFCFGNode *to, uint64_t cnt = 1,
                 bool isCall = false, bool isReturn = false);
  void mapCallOut(ELFCFGNode *from, ELFCFGNode *to, uint64_t toAddr,
                  uint64_t cnt = 1, bool isCall = false, bool isReturn = false);

  ELFCFGNode *getEntryNode() const {
    assert(!Nodes.empty());
    return Nodes.begin()->get();
  }

  bool isHot() const {
    if (Nodes.empty())
      return false;
    return (getEntryNode()->Freq != 0);
  }

  template <class Visitor>
  void forEachNodeRef(Visitor V) {
    for (auto &N: Nodes)
      V(*N);
  }

  bool writeAsDotGraph(const char *cfgOutName);

private:
  // Create and take ownership.
  ELFCFGEdge *createEdge(ELFCFGNode *from,
                         ELFCFGNode *to,
                         typename ELFCFGEdge::EdgeType type);

  void emplaceEdge(ELFCFGEdge *edge) {
    if (edge->Type < ELFCFGEdge::INTER_FUNC_CALL)
      IntraEdges.emplace_back(edge);
    else
      InterEdges.emplace_back(edge);
  }

  friend class ELFCFGBuilder;
};


class ELFCFGBuilder {
public:
  Propeller *Prop;
  ELFView   *View;

  uint32_t BB{0};
  uint32_t BBWoutAddr{0};
  uint32_t InvalidCFGs{0};

  ELFCFGBuilder(Propeller &prop, ELFView *vw) : Prop(&prop), View(vw) {}
  void buildCFGs();

protected:
  void buildCFG(ELFCFG &cfg, const SymbolRef &cfgSym,
                std::map<uint64_t, std::unique_ptr<ELFCFGNode>> &nodeMap);

  void calculateFallthroughEdges(
      ELFCFG &cfg, std::map<uint64_t, std::unique_ptr<ELFCFGNode>> &nodeMap);

  // Build a map from section "Idx" -> Section that relocates this
  // section. Only used during building phase.
  void
  buildRelocationSectionMap(std::map<uint64_t, section_iterator> &relocSecMap);
  // Build a map from section "Idx" -> node representing "Idx". Only
  // used during building phase.
  void buildShndxNodeMap(std::map<uint64_t, std::unique_ptr<ELFCFGNode>> &nodeMap,
                         std::map<uint64_t, ELFCFGNode *> &shndxNodeMap);
};

// ELFView is a structure that corresponds to a single ELF file.
class ELFView {
 public:
  static ELFView *create(const StringRef &vN,
                         const uint32_t ordinal,
                         const MemoryBufferRef &fR);

  ELFView(std::unique_ptr<ObjectFile> &vF,
          const StringRef &vN,
          const uint32_t vO,
          const MemoryBufferRef &fR) :
    ViewFile(std::move(vF)), ViewName(vN), Ordinal(vO), FileRef(fR), CFGs() {}
  ~ELFView() {}

  void EraseCfg(ELFCFG *&cfgPtr);

  std::unique_ptr<ObjectFile> ViewFile;
  StringRef                   ViewName;
  const uint32_t              Ordinal;
  MemoryBufferRef             FileRef;

  // Name -> ELFCFG mapping.
  std::map<StringRef, std::unique_ptr<ELFCFG>> CFGs;
};

std::ostream &operator<<(std::ostream &out, const ELFCFGNode &node);
std::ostream &operator<<(std::ostream &out, const ELFCFGEdge &edge);
std::ostream &operator<<(std::ostream &out, const ELFCFG &cfg);
}
} // namespace lld
#endif
