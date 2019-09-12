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

  bool markPath(ELFCfgNode *From, ELFCfgNode *To, uint64_t Cnt = 1);
  void mapBranch(ELFCfgNode *From, ELFCfgNode *To, uint64_t Cnt = 1,
                 bool isCall = false, bool isReturn = false);
  void mapCallOut(ELFCfgNode *From, ELFCfgNode *To, uint64_t ToAddr,
                  uint64_t Cnt = 1, bool isCall = false, bool isReturn = false);

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

  bool writeAsDotGraph(StringRef CfgOutName);

private:
  // Create and take ownership.
  ELFCfgEdge *createEdge(ELFCfgNode *From,
                         ELFCfgNode *To,
                         typename ELFCfgEdge::EdgeType Type);

  void emplaceEdge(ELFCfgEdge *Edge) {
    if (Edge->Type < ELFCfgEdge::INTER_FUNC_CALL) {
      IntraEdges.emplace_back(Edge);
    } else {
      InterEdges.emplace_back(Edge);
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

  ELFCfgBuilder(Propeller &P, ELFView *V) : Prop(&P), View(V) {}
  void buildCfgs();

protected:
  void buildCfg(ELFCfg &Cfg, const SymbolRef &CfgSym,
                map<uint64_t, unique_ptr<ELFCfgNode>> &NodeMap);

  void
  calculateFallthroughEdges(ELFCfg &Cfg,
                            map<uint64_t, unique_ptr<ELFCfgNode>> &NodeMap);

  // Build a map from section "Idx" -> Section that relocates this
  // section. Only used during building phase.
  void buildRelocationSectionMap(
      map<uint64_t, section_iterator> &RelocationSectionMap);
  // Build a map from section "Idx" -> Node representing "Idx". Only
  // used during building phase.
  void buildShndxNodeMap(map<uint64_t, unique_ptr<ELFCfgNode>> &NodeMap,
                         map<uint64_t, ELFCfgNode *> &ShndxNodeMap);
};

class ELFView {
 public:
  static ELFView *create(const StringRef &VN,
                         const uint32_t O,
                         const MemoryBufferRef &FR);

  ELFView(unique_ptr<ObjectFile> &VF,
          const StringRef &VN,
          const uint32_t VO,
          const MemoryBufferRef &FR) :
    ViewFile(std::move(VF)), ViewName(VN), Ordinal(VO), FileRef(FR), Cfgs() {}
  ~ELFView() {}

  void EraseCfg(ELFCfg *&CfgPtr);

  unique_ptr<ObjectFile> ViewFile;
  StringRef              ViewName;
  const uint32_t         Ordinal;
  MemoryBufferRef        FileRef;

  map<StringRef, unique_ptr<ELFCfg>> Cfgs;
};

ostream & operator << (ostream &Out, const ELFCfgNode &Node);
ostream & operator << (ostream &Out, const ELFCfgEdge &Edge);
ostream & operator << (ostream &Out, const ELFCfg     &Cfg);

}
} // namespace lld
#endif
