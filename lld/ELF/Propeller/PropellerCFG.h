//===-------------------- PropellerELFCFG.h -------------------------------===//
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
// cfg for each function and store it in ObjectView::cfgs, indexed by cfg name.
//
// CFGBuilder::buildCFGs works this way:
//   - groups funcName, a.bb.funcName, aa.bb.funcName and alike into one set,
//     for each set, passes the set to "CFGBuilder::buildCFG"
//   - each element in the set is a section, we then know from its section
//     relocations the connections to other sections. (a)
//   - from (a), we build controlFlowGraph.
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

// All instances of CFGEdge are owned by their controlFlowGraph.
class CFGEdge {
public:
  CFGNode *src;
  CFGNode *sink;
  uint64_t weight;

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
  } type = INTRA_FUNC;

  bool isCall() const { return type == INTER_FUNC_CALL || type == INTRA_RSC; }

  bool isReturn() const {
    return type == INTER_FUNC_RETURN || type == INTRA_RSR;
  }

  bool isFTEdge() const;

protected:
  CFGEdge(CFGNode *n1, CFGNode *n2, EdgeType t)
      : src(n1), sink(n2), weight(0), type(t) {}

  friend class ControlFlowGraph;
};

// All instances of CFGNode are owned by their controlFlowGraph.
class CFGNode {
public:
  uint64_t shndx;
  StringRef shName;
  uint64_t shSize;
  // Note, "mappedAddr"s are not real/virtual addresses, they are ordinals from
  // the propeller file. However, ordinals from propeller do reflect the true
  // orders of symbol address.
  uint64_t mappedAddr;
  uint64_t freq;
  ControlFlowGraph *controlFlowGraph;

  // Containing chain for this node assigned by the ordering algorithm.
  // This will be updated as chains keep merging together during the algorithm.
  NodeChain *chain;

  // Offset of this node in the assigned chain.
  uint64_t chainOffset;

  std::vector<CFGEdge *> outs;     // Intra function edges.
  std::vector<CFGEdge *> ins;      // Intra function edges.
  std::vector<CFGEdge *> callOuts; // Callouts/returns to other functions.
  std::vector<CFGEdge *> callIns;  // Callins/returns from other functions.

  // Fallthrough edge, could be nullptr. And if not, ftEdge is in outs.
  CFGEdge *ftEdge;

  // In Selective bb mode - if this bb appears in the hot bbs section, then this
  // is true.
  // In allBBMode - this is true if the function it belongs to appears in the
  // hot bbs sections, even if the bb itself is cold.
  // If hotTag is false, then the node freq and all its edges Freqs are zero-ed
  // out.
  bool hotTag;

  const static uint64_t InvalidAddress = -1;

  unsigned getBBIndex() const {
    StringRef fName, bName;
    if (SymbolEntry::isBBSymbol(shName, &fName, &bName))
      return bName.size();
    return 0;
  }

  bool isEntryNode() const;

  template <class Visitor> void forEachInEdgeRef(Visitor v) {
    for (auto &edgeList : {ins, callIns})
      for (CFGEdge *E : edgeList)
        v(*E);
  }

  template <class Visitor> void forEachIntraOutEdgeRef(Visitor v) {
    for (CFGEdge *E : outs)
      v(*E);
  }

  template <class Visitor> void forEachOutEdgeRef(Visitor v) {
    for (auto &edgeList : {outs, callOuts})
      for (CFGEdge *E : edgeList)
        v(*E);
  }

private:
  CFGNode(uint64_t _shndx, const StringRef &_shName, uint64_t _size,
          uint64_t _mappedAddr, ControlFlowGraph *_cfg, bool _hotTag)
      : shndx(_shndx), shName(_shName), shSize(_size), mappedAddr(_mappedAddr),
        freq(0), controlFlowGraph(_cfg), chain(nullptr), chainOffset(0), outs(),
        ins(), callOuts(), callIns(), ftEdge(nullptr), hotTag(_hotTag) {}

  friend class ControlFlowGraph;
  friend class CFGBuilder;
};

class ControlFlowGraph {
public:
  ObjectView *view;
  StringRef name;
  uint64_t size;

  // Whether propeller should print information about how this controlFlowGraph
  // is being reordered.
  bool debugCFG;
  bool hot;

  // ControlFlowGraph assumes the ownership for all nodes / Edges.
  std::vector<std::unique_ptr<CFGNode>> nodes; // Sorted by address.
  std::vector<std::unique_ptr<CFGEdge>> intraEdges;
  std::vector<std::unique_ptr<CFGEdge>> interEdges;

  ControlFlowGraph(ObjectView *v, const StringRef &n, uint64_t s)
      : view(v), name(n), size(s), hot(false) {
    debugCFG = std::find(propConfig.optDebugSymbols.begin(),
                         propConfig.optDebugSymbols.end(),
                         name.str()) != propConfig.optDebugSymbols.end();
  }

  bool markPath(CFGNode *from, CFGNode *to, uint64_t cnt = 1);
  void mapBranch(CFGNode *from, CFGNode *to, uint64_t cnt = 1,
                 bool isCall = false, bool isReturn = false);
  void mapCallOut(CFGNode *from, CFGNode *to, uint64_t toAddr, uint64_t cnt = 1,
                  bool isCall = false, bool isReturn = false);

  CFGNode *getEntryNode() const {
    assert(!nodes.empty());
    return nodes.begin()->get();
  }

  bool isHot() const {
    if (nodes.empty())
      return false;
    return hot;
  }

  template <class Visitor> void forEachNodeRef(Visitor v) {
    for (auto &N : nodes)
      v(*N);
  }

  bool writeAsDotGraph(StringRef cfgOutName);

private:
  // Create and take ownership.
  CFGEdge *createEdge(CFGNode *from, CFGNode *to,
                      typename CFGEdge::EdgeType type);

  void emplaceEdge(CFGEdge *edge) {
    if (edge->type < CFGEdge::INTER_FUNC_CALL)
      intraEdges.emplace_back(edge);
    else
      interEdges.emplace_back(edge);
  }

  friend class CFGBuilder;
};

class CFGBuilder {
public:
  ObjectView *view;

  uint32_t bb = 0;
  uint32_t bbWoutAddr = 0;
  uint32_t invalidCFGs = 0;

  CFGBuilder(ObjectView *vw) : view(vw) {}

  // See implementaion comments in .cpp.
  bool buildCFGs(std::map<uint64_t, uint64_t> &ordinalRemapping);

protected:
  // Group symbols according to function boundary.
  std::map<StringRef, std::list<SymbolRef>> buildPreCFGGroups();

  // See implementaion comments in .cpp.
  void buildCFG(ControlFlowGraph &cfg, const SymbolRef &cfgSym,
                std::map<uint64_t, std::unique_ptr<CFGNode>> &nodeMap,
                std::map<uint64_t, section_iterator> &relocationSectionMap);

  std::unique_ptr<ControlFlowGraph>
  buildCFGNodes(std::map<StringRef, std::list<SymbolRef>>::value_type &group,
                std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap,
                std::map<uint64_t, uint64_t> &ordinalRemapping);

  // See implementation comments in .cpp.
  void calculateFallthroughEdges(
      ControlFlowGraph &cfg,
      std::map<uint64_t, std::unique_ptr<CFGNode>> &nodeMap);

  // Build a map from section "Idx" -> Section that relocates this
  // section. Only used during building phase.
  std::map<uint64_t, section_iterator> buildRelocationSectionMap();

  // Build a map from section "Idx" -> node representing "Idx". Only
  // used during building phase.
  void buildShndxNodeMap(std::map<uint64_t, std::unique_ptr<CFGNode>> &nodeMap,
                         std::map<uint64_t, CFGNode *> &shndxNodeMap);
};

// ObjectView is a structure that corresponds to a single ELF file.
class ObjectView {
public:
  ObjectView(std::unique_ptr<ObjectFile> &vf, const StringRef &vn,
             const uint32_t vo, const MemoryBufferRef &mbr)
      : viewFile(std::move(vf)), viewName(vn), ordinal(vo), fileRef(mbr),
        cfgs() {}

  void EraseCfg(ControlFlowGraph *&cfgPtr);

  std::unique_ptr<ObjectFile> viewFile;
  StringRef viewName;
  const uint32_t ordinal;
  MemoryBufferRef fileRef;

  // name -> ControlFlowGraph mapping.
  std::map<StringRef, std::unique_ptr<ControlFlowGraph>> cfgs;
};

std::ostream &operator<<(std::ostream &out, const CFGNode &node);
std::ostream &operator<<(std::ostream &out, const CFGEdge &edge);
std::ostream &operator<<(std::ostream &out, const ControlFlowGraph &cfg);
} // namespace propeller
} // namespace lld
#endif
