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
// The ObjectView class represents one ELF file. The CFGBuilder class builds
// cfg for each function and store it in ObjectView::CFGs, indexed by cfg name.
//
// CFGBuilder::buildCFGs works this way:
//   - groups funcName, a.BB.funcName, aa.BB.funcName and alike into one set,
//     for each set, passes the set to "CFGBuilder::buildCFG"
//   - each element in the set is a section, we then know from its section
//     relocations the connections to other sections. (a)
//   - from (a), we build CFG.
//
// Three important functions in ControlFlowGraph:
//   mapBranch - apply counter to edge A->B, where A, B belong to the same func
//
//   mapCallOut - apply counter to edge A->B, where A, B belong to diff funcs
//
//   markPath - apply counter to all nodes/edges betwee A and B, A and B belong
//              to same func
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_PROPELLER_CFG_H
#define LLD_ELF_PROPELLER_CFG_H

#include "Propeller.h"
#include "PropellerConfig.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"

#include <map>
#include <memory>
#include <ostream>
#include <vector>

using llvm::object::ObjectFile;
using llvm::object::section_iterator;
using llvm::object::SymbolRef;

namespace lld {
namespace propeller {

class ObjectView;
class CFGNode;
class ControlFlowGraph;
class NodeChain;

// All instances of CFGEdge are owned by their CFG.
class CFGEdge {
public:
  CFGNode *Src;
  CFGNode *Sink;
  uint64_t Weight;

  // Whether it's an edge introduced by recursive-self-call.  (Usually
  // calls do not split basic blocks and do not introduce new edges.)
  enum EdgeType : char {
    INTRA_FUNC,
    INTRA_RSC, // Recursive call.
    INTRA_RSR, // Return from recursive call.
    // Intra edge dynamically created because of indirect jump, etc.
    INTRA_DYNA,
    // Inter function jumps / calls.
    INTER_FUNC_CALL,
    INTER_FUNC_RETURN,
  } Type = INTRA_FUNC;

  bool isCall() const {
    return Type == INTER_FUNC_CALL || Type == INTRA_RSC;
  }

  bool isReturn() const {
    return Type == INTER_FUNC_RETURN || Type == INTRA_RSR;
  }

protected:
  CFGEdge(CFGNode *N1, CFGNode *N2, EdgeType T)
      : Src(N1), Sink(N2), Weight(0), Type(T) {}

  friend class ControlFlowGraph;
};

// All instances of CFGNode are owned by their CFG.
class CFGNode {
public:
  uint64_t Shndx;
  StringRef ShName;
  uint64_t ShSize;
  // Note, "MappedAddr"s are not real/virtual addresses, they are ordinals from
  // the propeller file. However, ordinals from propeller do reflect the true
  // orders of symbol address.
  uint64_t MappedAddr;
  uint64_t Freq;
  ControlFlowGraph *CFG;

  // Containing chain for this node assigned by the ordering algorithm.
  // This will be updated as chains keep merging together during the algorithm.
  NodeChain * Chain;

  // Offset of this node in the assigned chain.
  uint64_t ChainOffset;

  std::vector<CFGEdge *> Outs;     // Intra function edges.
  std::vector<CFGEdge *> Ins;      // Intra function edges.
  std::vector<CFGEdge *> CallOuts; // Callouts/returns to other functions.
  std::vector<CFGEdge *> CallIns;  // Callins/returns from other functions.

  // Fallthrough edge, could be nullptr. And if not, FTEdge is in Outs.
  CFGEdge *FTEdge;

  bool HotTag;

  const static uint64_t InvalidAddress = -1;

  unsigned getBBIndex() const {
    StringRef FName, BName;
    if (SymbolEntry::isBBSymbol(ShName, &FName, &BName))
      return BName.size();
    return 0;
  }

  bool isEntryNode() const;

  template <class Visitor> void forEachInEdgeRef(Visitor V) {
    for (auto& edgeList: {Ins, CallIns})
      for (CFGEdge * E: edgeList)
        V(*E);
  }

  template <class Visitor> void forEachIntraOutEdgeRef(Visitor V) {
    for (CFGEdge * E: Outs)
      V(*E);
  }

  template <class Visitor> void forEachOutEdgeRef(Visitor V) {
    for (auto& edgeList: {Outs, CallOuts})
      for (CFGEdge * E: edgeList)
        V(*E);
  }

private:
  CFGNode(uint64_t _Shndx, const StringRef &_ShName, uint64_t _Size,
          uint64_t _MappedAddr, ControlFlowGraph *_Cfg, bool _HotTag)
      : Shndx(_Shndx), ShName(_ShName), ShSize(_Size), MappedAddr(_MappedAddr),
        Freq(0), CFG(_Cfg), Chain(nullptr), ChainOffset(0), Outs(), Ins(),
        CallOuts(), CallIns(), FTEdge(nullptr), HotTag(_HotTag) {}

  friend class ControlFlowGraph;
  friend class CFGBuilder;
};

class ControlFlowGraph {
public:
  ObjectView *View;
  StringRef Name;
  uint64_t Size;

  // Whether propeller should print information about how this CFG is being
  // reordered.
  bool DebugCFG;

  // ControlFlowGraph assumes the ownership for all Nodes / Edges.
  std::vector<std::unique_ptr<CFGNode>> Nodes; // Sorted by address.
  std::vector<std::unique_ptr<CFGEdge>> IntraEdges;
  std::vector<std::unique_ptr<CFGEdge>> InterEdges;

  ControlFlowGraph(ObjectView *V, const StringRef &N, uint64_t S)
      : View(V), Name(N), Size(S) {
        DebugCFG = std::find(propellerConfig.optDebugSymbols.begin(),
                             propellerConfig.optDebugSymbols.end(),
                             Name.str()) != propellerConfig.optDebugSymbols.end();
      }

  bool markPath(CFGNode *from, CFGNode *to, uint64_t cnt = 1);
  void mapBranch(CFGNode *from, CFGNode *to, uint64_t cnt = 1,
                 bool isCall = false, bool isReturn = false);
  void mapCallOut(CFGNode *from, CFGNode *to, uint64_t toAddr, uint64_t cnt = 1,
                  bool isCall = false, bool isReturn = false);

  CFGNode *getEntryNode() const {
    assert(!Nodes.empty());
    return Nodes.begin()->get();
  }

  bool isHot() const {
    if (Nodes.empty())
      return false;
    return (getEntryNode()->Freq != 0);
  }

  template <class Visitor> void forEachNodeRef(Visitor V) {
    for (auto &N : Nodes)
      V(*N);
  }

  bool writeAsDotGraph(StringRef cfgOutName);

private:
  // Create and take ownership.
  CFGEdge *createEdge(CFGNode *from, CFGNode *to,
                      typename CFGEdge::EdgeType type);

  void emplaceEdge(CFGEdge *edge) {
    if (edge->Type < CFGEdge::INTER_FUNC_CALL)
      IntraEdges.emplace_back(edge);
    else
      InterEdges.emplace_back(edge);
  }

  friend class CFGBuilder;
};

class CFGBuilder {
public:
  ObjectView *View;

  uint32_t BB = 0;
  uint32_t BBWoutAddr = 0;
  uint32_t InvalidCFGs = 0;

  CFGBuilder(ObjectView *vw) : View(vw) {}

  // See implementaion comments in .cpp.
  bool buildCFGs(std::map<uint64_t, uint64_t> &OrdinalRemapping);

protected:
  // See implementaion comments in .cpp.
  void buildCFG(ControlFlowGraph &cfg, const SymbolRef &cfgSym,
                std::map<uint64_t, std::unique_ptr<CFGNode>> &nodeMap);

  // See implementation comments in .cpp.
  void calculateFallthroughEdges(
      ControlFlowGraph &cfg,
      std::map<uint64_t, std::unique_ptr<CFGNode>> &nodeMap);

  // Build a map from section "Idx" -> Section that relocates this
  // section. Only used during building phase.
  void
  buildRelocationSectionMap(std::map<uint64_t, section_iterator> &relocSecMap);
  // Build a map from section "Idx" -> node representing "Idx". Only
  // used during building phase.
  void buildShndxNodeMap(std::map<uint64_t, std::unique_ptr<CFGNode>> &nodeMap,
                         std::map<uint64_t, CFGNode *> &shndxNodeMap);
};

// ObjectView is a structure that corresponds to a single ELF file.
class ObjectView {
public:
  ObjectView(std::unique_ptr<ObjectFile> &vF, const StringRef &vN,
             const uint32_t vO, const MemoryBufferRef &fR)
      : ViewFile(std::move(vF)), ViewName(vN), Ordinal(vO), FileRef(fR),
        CFGs() {}

  void EraseCfg(ControlFlowGraph *&cfgPtr);

  std::unique_ptr<ObjectFile> ViewFile;
  StringRef ViewName;
  const uint32_t Ordinal;
  MemoryBufferRef FileRef;

  // Name -> ControlFlowGraph mapping.
  std::map<StringRef, std::unique_ptr<ControlFlowGraph>> CFGs;
};

std::ostream &operator<<(std::ostream &out, const CFGNode &node);
std::ostream &operator<<(std::ostream &out, const CFGEdge &edge);
std::ostream &operator<<(std::ostream &out, const ControlFlowGraph &cfg);
} // namespace propeller
} // namespace lld
#endif
