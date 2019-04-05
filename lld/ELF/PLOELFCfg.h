#ifndef LLD_ELF_PLO_ELF_CFG_H
#define LLD_ELF_PLO_ELF_CFG_H

#include <list>
#include <map>
#include <memory>
#include <ostream>

#include "llvm/ADT/StringRef.h"

using std::list;
using std::map;
using std::ostream;
using std::unique_ptr;

using llvm::StringRef;

namespace lld {
namespace plo {

class ELFCfgNode;

class ELFCfgEdge {
public:
  ELFCfgNode *Src;
  ELFCfgNode *Sink;
  uint64_t    Weight;
  // Whether it's an edge introduced by recursive-self-call.  (Usually
  // calls do not split basic blocks and do not introduce new edges.)
  enum EdgeType : char {NORMAL = 0, RSC, RSR, OTHER} Type {NORMAL};

protected:
  ELFCfgEdge(ELFCfgNode *N1, ELFCfgNode *N2, EdgeType T)
    :Src(N1), Sink(N2), Weight(0), Type(T) {}

  friend class ELFCfg;
};

class ELFCfg {
 public:
  StringRef Name;
  // Cfg size is the first bb section size. Not the size of all bb sections.
  uint64_t Size{0};
  map<uint64_t, unique_ptr<ELFCfgNode>> Nodes;
  list<unique_ptr<ELFCfgEdge>> Edges;

  ELFCfg(const StringRef &N) : Name(N) {}
  ~ELFCfg() {}

  bool MarkPath(ELFCfgNode *From, ELFCfgNode *To);
  void MapBranch(ELFCfgNode *From, ELFCfgNode *To);
  ELFCfgEdge *CreateEdge(ELFCfgNode *From, ELFCfgNode *To,
                         typename ELFCfgEdge::EdgeType Type);
};

class ELFCfgNode {
 public:
  const uint16_t     Shndx;
  StringRef          ShName;
  ELFCfg            *Cfg;
  list<ELFCfgEdge *> Outs;
  list<ELFCfgEdge *> Ins;
  // Fallthrough edge, could be nullptr. And if not, FTEdge is in Outs.
  ELFCfgEdge *       FTEdge{nullptr};
  uint64_t           MappedAddr{InvalidAddress};

  ELFCfgNode(const uint16_t _Shndx, const StringRef &_ShName, ELFCfg *_Cfg)
    : Shndx(_Shndx), ShName(_ShName), Cfg(_Cfg) {}

  const char *GetShortName() const {
    if (ShName == Cfg->Name)
      return "<Entry>";
    return ShName.data() + Cfg->Name.size() + 1;
  }

  const static uint64_t InvalidAddress = -1l;
};

ostream & operator << (ostream &Out, const ELFCfgNode &Node);
ostream & operator << (ostream &Out, const ELFCfgEdge &Edge);
ostream & operator << (ostream &Out, const ELFCfg     &Cfg);

}
}
#endif
