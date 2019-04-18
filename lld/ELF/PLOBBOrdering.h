#ifndef LLD_ELF_PLO_BB_ORDERING_H
#define LLD_ELF_PLO_BB_ORDERING_H

#include <list>
#include <memory>
#include <ostream>

#include "PLOELFCfg.h"

using std::list;
using std::unique_ptr;

namespace lld {
namespace plo {

class ELFCfgNode;
class ELFCfg;
class MeshPoint;

class MeshLink {
public:
  MeshPoint *Src;
  MeshPoint *Sink;
  uint64_t   Weight;

  MeshLink(MeshPoint *S, MeshPoint *T, uint64_t W)
    : Src(S), Sink(T), Weight(W) {}
  ~MeshLink() {}
};

class MeshPoint {
public:
  list<ELFCfgNode *> Nodes;
  list<MeshLink *>   Links;

  StringRef GetName() const {
    return (*Nodes.begin())->GetShortName();
  }
};

class Mesh {
public:
  Mesh(ELFCfg &C) : Cfg(C) {}

  void Init();

  void Reduce();
  MeshPoint *ReduceLink(MeshLink *);

  MeshLink *CreateLink(MeshPoint *P1, MeshPoint *P2, uint64_t W);
  MeshPoint *MergeEndPoints(MeshPoint *P1, MeshPoint *P2);

  ELFCfg &Cfg;
  list<unique_ptr<MeshPoint>>    Points;
  list<unique_ptr<MeshLink>>     Links;
};

class PLOBBOrdering {
public:
  
};

std::ostream & operator << (std::ostream &Out, const MeshPoint &P);
std::ostream & operator << (std::ostream &Out, const MeshLink  &L);
std::ostream & operator << (std::ostream &Out, const Mesh      &M);
  
}
}
#endif
