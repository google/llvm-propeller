#ifndef LLD_ELF_PLO_ELF_CFG_H
#define LLD_ELF_PLO_ELF_CFG_H

#include "PLO.h"

#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <set>

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"

using std::list;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::unique_ptr;

using llvm::object::SymbolRef;
using llvm::object::section_iterator;
using llvm::StringRef;

namespace lld {
namespace plo {

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
  uint64_t           Freq;
  uint64_t           MappedAddr;
  uint64_t           Weight;
  ELFCfg            *Cfg;
  
  list<ELFCfgEdge *> Outs;      // Intra function edges.
  list<ELFCfgEdge *> Ins;       // Intra function edges.
  list<ELFCfgEdge *> CallOuts;  // Callouts/returns to other functions.
  list<ELFCfgEdge *> CallIns;   // Callins/returns from other functions.
  
  // Fallthrough edge, could be nullptr. And if not, FTEdge is in Outs.
  ELFCfgEdge *       FTEdge;

  const static uint64_t InvalidAddress = -1l;

  StringRef getShortName() {
    auto I = ShName.rsplit(".bb.");
    if (!I.second.empty()) {
      return I.second;
    }
    return "E"; // Entry;
  }

private:
  ELFCfgNode(uint64_t _Shndx, const StringRef &_ShName,
             uint64_t _Size, uint64_t _MappedAddr, ELFCfg *_Cfg)
    : Shndx(_Shndx), ShName(_ShName), ShSize(_Size),
      MappedAddr(_MappedAddr), Weight(0), Cfg(_Cfg),
      Outs(), Ins(), CallOuts(), CallIns(), FTEdge(nullptr) {}

  friend class ELFCfg;
  friend class ELFCfgBuilder;
  friend class ELFCfgReader;
};

class ELFCfgEdgeBuilder {
public:
  std::string SrcShName, SinkShName;
  uint64_t Weight;
  uint16_t Type;

  ELFCfgEdgeBuilder(std::string _SrcShName, std::string _SinkShName,
                    uint64_t _Weight, uint16_t _Type)
      : SrcShName(_SrcShName), SinkShName(_SinkShName), Weight(_Weight),
        Type(_Type) {}
};

class ELFCfgReader {
 public:
  StringRef CfgFilePath;
  std::vector<unique_ptr<ELFCfg>> Cfgs;
  ELFCfgReader(StringRef& _CfgFilePath): CfgFilePath(_CfgFilePath) {}

  void readCfgs();
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

  bool markPath(ELFCfgNode *From, ELFCfgNode *To);
  void mapBranch(ELFCfgNode *From, ELFCfgNode *To);
  void mapCallOut(ELFCfgNode *From, ELFCfgNode *To, uint64_t ToAddr);
  void dumpToOS(std::ostream&) const;

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

  double computeDensity() {
    double W = 0;
    forEachNodeRef([&W](ELFCfgNode &N) { W += N.Weight; });
    return W / this->Size;
  }

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
  friend class ELFCfgReader;
};


class ELFCfgBuilder {
public:
  PLO     &Plo;
  ELFView *View;

  uint32_t BB{0};
  uint32_t BBWoutAddr{0};
  uint32_t InvalidCfgs{0};

  ELFCfgBuilder(PLO &P, ELFView *V) : Plo(P), View(V) {}
  void buildCfgs();

protected:
  void buildCfg(ELFCfg &Cfg, const SymbolRef &CfgSym,
                map<uint64_t, list<unique_ptr<ELFCfgNode>>> &NodeMap);

  void calculateFallthroughEdges(
      ELFCfg &Cfg, map<uint64_t, list<unique_ptr<ELFCfgNode>>> &NodeMap);

  // Build a map from section "Idx" -> Section that relocates this
  // section. Only used during building phase.
  void buildRelocationSectionMap(
      map<uint64_t, section_iterator> &RelocationSectionMap);
  // Build a map from section "Idx" -> Node representing "Idx". Only
  // used during building phase.
  void buildShndxNodeMap(map<uint64_t, list<unique_ptr<ELFCfgNode>>> &NodeMap,
                         map<uint64_t, ELFCfgNode *> &ShndxNodeMap);
};

ostream & operator << (ostream &Out, const ELFCfgNode &Node);
ostream & operator << (ostream &Out, const ELFCfgEdge &Edge);
ostream & operator << (ostream &Out, const ELFCfg     &Cfg);

}
}
#endif
