#ifndef LLD_ELF_PLO_ELF_CFG_H
#define LLD_ELF_PLO_ELF_CFG_H

#include <list>
#include <map>
#include <memory>
#include <ostream>

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFObjectFile.h"

using std::list;
using std::multimap;
using std::ostream;
using std::unique_ptr;

using llvm::object::ELFSymbolRef;
using llvm::StringRef;

namespace lld {
namespace plo {

template <class ELFT>
class ELFViewImpl;
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
      INTER_FUNC
  } Type {INTRA_FUNC};

protected:
  ELFCfgEdge(ELFCfgNode *N1, ELFCfgNode *N2, EdgeType T)
    :Src(N1), Sink(N2), Weight(0), Type(T) {}

  friend class ELFCfg;
};

class ELFCfgNode {
 public:
  const uint16_t     Shndx;
  StringRef          ShName;
  uint64_t           ShSize;
  uint64_t           MappedAddr;
  ELFCfg            *Cfg;
  
  list<ELFCfgEdge *> Outs;      // Intra function edges.
  list<ELFCfgEdge *> Ins;       // Intra function edges.
  list<ELFCfgEdge *> CallOuts;  // Callouts/returns to other functions.
  list<ELFCfgEdge *> CallIns;   // Callins/returns from other functions.
  
  // Fallthrough edge, could be nullptr. And if not, FTEdge is in Outs.
  ELFCfgEdge *       FTEdge;

  const static uint64_t InvalidAddress = -1l;

private:
  ELFCfgNode(const uint16_t _Shndx, const StringRef &_ShName,
             uint64_t _Size, uint64_t _MappedAddr, ELFCfg *_Cfg)
    : Shndx(_Shndx), ShName(_ShName), ShSize(_Size),
      MappedAddr(_MappedAddr), Cfg(_Cfg),
      Outs(), Ins(), CallOuts(), CallIns(), FTEdge(nullptr) {}

  friend class ELFCfg;
};

class ELFCfg {
 public:
  StringRef Name;
  // Cfg size is the first bb section size. Not the size of all bb sections.
  uint64_t Size{0};
  // ELFCfg has the ownership for all Nodes / Edges.
  multimap<uint64_t, unique_ptr<ELFCfgNode>> Nodes;
  list<unique_ptr<ELFCfgEdge>> IntraEdges;
  list<unique_ptr<ELFCfgEdge>> InterEdges;

  ELFCfg(const StringRef &N) : Name(N) {}
  ~ELFCfg() {}

  bool MarkPath(ELFCfgNode *From, ELFCfgNode *To);
  void MapBranch(ELFCfgNode *From, ELFCfgNode *To);
  void MapCallOut(ELFCfgNode *From, ELFCfgNode *To);

  ELFCfgNode *GetEntryNode() const {
    if (Nodes.empty()) return nullptr;
    return Nodes.begin()->second.get();
  }

private:
  // Create and take ownership.
  ELFCfgEdge *CreateEdge(ELFCfgNode *From,
                         list<ELFCfgEdge *>& FromOuts,
                         ELFCfgNode *To,
                         list<ELFCfgEdge *>& ToIns,
                         typename ELFCfgEdge::EdgeType Type);

  // Create and take ownership.
  ELFCfgNode *CreateNode(uint16_t Shndx, StringRef &ShName,
                         uint64_t ShSize, uint64_t MappedAddress);

  void EmplaceEdge(ELFCfgEdge *Edge) {
    if (Edge->Type < ELFCfgEdge::INTER_FUNC) {
      IntraEdges.emplace_back(Edge);
    } else {
      InterEdges.emplace_back(Edge);
    }
  }

  template<class ELFT>
  friend class ELFCfgBuilder;
};


template <class ELFT>
class ELFCfgBuilder {
 public:
  using ViewFileShdr = typename ELFViewImpl<ELFT>::ViewFileShdr;
  using ViewFileSym  = typename ELFViewImpl<ELFT>::ViewFileSym;
  using ViewFileRela = typename ELFViewImpl<ELFT>::ViewFileRela;
  using ELFTUInt     = typename ELFViewImpl<ELFT>::ELFTUInt;

  ELFViewImpl<ELFT> *View;

  uint32_t BB{0};
  uint32_t BBWoutAddr{0};
  uint32_t InvalidCfgs{0};

  ELFCfgBuilder(ELFViewImpl<ELFT> *V) : View(V) {}
  void BuildCfgs();

protected:
  void BuildCfg(ELFCfg &Cfg, const ELFSymbolRef &CfgSym);
  void CalculateFallthroughEdges(ELFCfg &Cfg);
};

ostream & operator << (ostream &Out, const ELFCfgNode &Node);
ostream & operator << (ostream &Out, const ELFCfgEdge &Edge);
ostream & operator << (ostream &Out, const ELFCfg     &Cfg);

}
}
#endif
