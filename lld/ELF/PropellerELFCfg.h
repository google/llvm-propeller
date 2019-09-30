//===-------------------- PropellerELFCfg.h -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Class definitions for propeller cfg, edge, nodes and CfgBuilder.
//
// The ELFView class represents one ELF file. The ELFCfgBuilder class builds
// cfg for each function and store it in ELFView::Cfgs, indexed by cfg name.
//
// ELFCfgBuilder::buildCfgs works this way:
//   - groups funcName, a.BB.funcName, aa.BB.funcName and alike into one set, 
//     for each set, passes the set to "ELFCfgBuilder::buildCfg"
//   - each element in the set is a section, we then know from its section
//     relocations the connections to other sections. (a)
//   - from (a), we build cfg.
//
// Three important functions in ELFCfg:
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

#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <set>

using llvm::MemoryBufferRef;
using llvm::object::ObjectFile;
using llvm::object::SymbolRef;
using llvm::object::section_iterator;
using llvm::StringRef;

using std::list;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::unique_ptr;

namespace lld {
namespace propeller {

class ELFView;
class ELFCfgNode;
class ELFCfg;

class ELFCfgEdge {
public:
  ELFCfgNode *Src;
  ELFCfgNode *Sink;
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
  ELFCfgEdge(ELFCfgNode *N1, ELFCfgNode *N2, EdgeType T)
    :Src(N1), Sink(N2), Weight(0), Type(T) {}

  friend class ELFCfg;
};

class ELFCfgNode {
 public:
  uint64_t           Shndx;
  StringRef          ShName;
  uint64_t           ShSize;
  uint64_t           MappedAddr;
  uint64_t           Freq;
  ELFCfg            *Cfg;
  
  list<ELFCfgEdge *> Outs;      // Intra function edges.
  list<ELFCfgEdge *> Ins;       // Intra function edges.
  list<ELFCfgEdge *> CallOuts;  // Callouts/returns to other functions.
  list<ELFCfgEdge *> CallIns;   // Callins/returns from other functions.
  
  // Fallthrough edge, could be nullptr. And if not, FTEdge is in Outs.
  ELFCfgEdge *       FTEdge;

  const static uint64_t InvalidAddress = -1l;

  unsigned getBBIndex() {
    StringRef FName, BName;
    if (SymbolEntry::isBBSymbol(ShName, &FName, &BName))
      return BName.size();
    else
      return 0;
  }

private:
  ELFCfgNode(uint64_t _Shndx, const StringRef &_ShName,
             uint64_t _Size, uint64_t _MappedAddr, ELFCfg *_Cfg)
    : Shndx(_Shndx), ShName(_ShName), ShSize(_Size),
      MappedAddr(_MappedAddr), Freq(0), Cfg(_Cfg),
      Outs(), Ins(), CallOuts(), CallIns(), FTEdge(nullptr) {}

  friend class ELFCfg;
  friend class ELFCfgBuilder;
};

class ELFCfg {
public:
  ELFView    *View;
  StringRef   Name;
  uint64_t    Size;
  
  // ELFCfg assumes the ownership for all Nodes / Edges.
  list<unique_ptr<ELFCfgNode>> Nodes;  // Sorted by address.
  list<unique_ptr<ELFCfgEdge>> IntraEdges;
  list<unique_ptr<ELFCfgEdge>> InterEdges;

  ELFCfg(ELFView *V, const StringRef &N, uint64_t S)
    : View(V), Name(N), Size(S) {}
  ~ELFCfg() {}

  bool markPath(ELFCfgNode *from, ELFCfgNode *to, uint64_t cnt = 1);
  void mapBranch(ELFCfgNode *from, ELFCfgNode *to, uint64_t cnt = 1,
                 bool isCall = false, bool isReturn = false);
  void mapCallOut(ELFCfgNode *from, ELFCfgNode *to, uint64_t toAddr,
                  uint64_t cnt = 1, bool isCall = false, bool isReturn = false);

  ELFCfgNode *getEntryNode() const {
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
    for (auto &N: Nodes) {
      V(*N);
    }
  }

  bool writeAsDotGraph(const char *cfgOutName);

private:
  // Create and take ownership.
  ELFCfgEdge *createEdge(ELFCfgNode *from,
                         ELFCfgNode *to,
                         typename ELFCfgEdge::EdgeType type);

  void emplaceEdge(ELFCfgEdge *edge) {
    if (edge->Type < ELFCfgEdge::INTER_FUNC_CALL) {
      IntraEdges.emplace_back(edge);
    } else {
      InterEdges.emplace_back(edge);
    }
  }

  friend class ELFCfgBuilder;
};


class ELFCfgBuilder {
public:
  Propeller *Prop;
  ELFView   *View;

  uint32_t BB{0};
  uint32_t BBWoutAddr{0};
  uint32_t InvalidCfgs{0};

  ELFCfgBuilder(Propeller &prop, ELFView *vw) : Prop(&prop), View(vw) {}
  void buildCfgs();

protected:
  void buildCfg(ELFCfg &cfg, const SymbolRef &cfgSym,
                map<uint64_t, unique_ptr<ELFCfgNode>> &nodeMap);

  void
  calculateFallthroughEdges(ELFCfg &cfg,
                            map<uint64_t, unique_ptr<ELFCfgNode>> &nodeMap);

  // Build a map from section "Idx" -> Section that relocates this
  // section. Only used during building phase.
  void buildRelocationSectionMap(map<uint64_t, section_iterator> &relocSecMap);
  // Build a map from section "Idx" -> node representing "Idx". Only
  // used during building phase.
  void buildShndxNodeMap(map<uint64_t, unique_ptr<ELFCfgNode>> &nodeMap,
                         map<uint64_t, ELFCfgNode *> &shndxNodeMap);
};

// ELFView is a structure that corresponds to a single ELF file.
class ELFView {
 public:
  static ELFView *create(const StringRef &vN,
                         const uint32_t ordinal,
                         const MemoryBufferRef &fR);

  ELFView(unique_ptr<ObjectFile> &vF,
          const StringRef &vN,
          const uint32_t vO,
          const MemoryBufferRef &fR) :
    ViewFile(std::move(vF)), ViewName(vN), Ordinal(vO), FileRef(fR), Cfgs() {}
  ~ELFView() {}

  void EraseCfg(ELFCfg *&cfgPtr);

  unique_ptr<ObjectFile> ViewFile;
  StringRef              ViewName;
  const uint32_t         Ordinal;
  MemoryBufferRef        FileRef;

  // Name -> ELFCfg mapping.
  map<StringRef, unique_ptr<ELFCfg>> Cfgs;
};

ostream & operator << (ostream &out, const ELFCfgNode &node);
ostream & operator << (ostream &out, const ELFCfgEdge &edge);
ostream & operator << (ostream &out, const ELFCfg     &cfg);

}
} // namespace lld
#endif
