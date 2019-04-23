#ifndef LLD_ELF_PLO_FUNC_ORDERING_H
#define LLD_ELF_PLO_FUNC_ORDERING_H

#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <utility>

using std::list;
using std::map;
using std::pair;
using std::set;
using std::unique_ptr;

namespace lld {
namespace plo {

class PLO;
class CGLink;
class CGPoint;
class CallGraph;
class ELFCfg;

std::ostream & operator << (std::ostream &Out, CallGraph &);
std::ostream & operator << (std::ostream &Out, CGPoint &);
std::ostream & operator << (std::ostream &Out, CGLink &);

class CGPoint {
public:

  CGPoint(uint64_t O, ELFCfg *Cfg) : Ordinal(O), Weight(0), Cfgs(1, Cfg) {}
  ~CGPoint() {}

  uint64_t        Ordinal;
  uint64_t        Weight;
  list<ELFCfg *>  Cfgs;
  list<CGLink *>  Links;
};

class CGLink {
public:
  ~CGLink() {}
  
  CGPoint   * const A;
  CGPoint   * const B;
  uint64_t   Weight;
  
private:
  CGLink(CGPoint *P1, CGPoint *P2)
    : A(P1->Ordinal < P2->Ordinal ? P1 : P2),
      B(P1->Ordinal < P2->Ordinal ? P2 : P1), Weight(0) {}

  friend CallGraph;
};

class CallGraph {
public:
  void InitGraph(PLO &Plo);
  void DoOrder();

private:

  CGPoint *CollapseLink(CGLink *L);
  
  CGPoint *CreatePoint(ELFCfg *Cfg) {
    auto *P = new CGPoint(Points.size(), Cfg);
    Points.emplace(P->Ordinal, P);
    return P;
  }

  CGLink *FindOrCreateLink(CGPoint *P1, CGPoint *P2) {
    auto *R = FindLink(P1, P2);
    if (R) return R;
    
    CGLink *L = new CGLink(P1, P2);
    Links.emplace(std::piecewise_construct,
                  std::forward_as_tuple(P1, P2),
                  std::forward_as_tuple(L));
    P1->Links.push_back(L);
    P2->Links.push_back(L);
    return L;
  }

  CGLink *FindLink(CGPoint *P1, CGPoint *P2) {
    auto I = Links.find(LinkMapKey(P1, P2));
    if (I != Links.end()) {
      return I->second.get();
    }
    return nullptr;
  }

  void RemoveLink(CGLink *L) {
    L->A->Links.remove(L);
    L->B->Links.remove(L);
    Links.erase(LinkMapKey(L->A, L->B));
  }

  void RemovePoint(CGPoint *P) {
    Points.erase(P->Ordinal);
  }

public:
  struct LinkMapKey {
    LinkMapKey(CGPoint *P1, CGPoint *P2) {
      if (P1->Ordinal < P2->Ordinal) {
        A = P1->Ordinal;
        B = P2->Ordinal;
      } else {
        A = P2->Ordinal;
        B = P1->Ordinal;
      }
    }
    uint64_t A;
    uint64_t B;

    bool operator < (const LinkMapKey &K2) const {
      if (A == K2.A)
        return B < K2.B;
      return A < K2.A;
    }
  };

  map<LinkMapKey, unique_ptr<CGLink>>  Links;
  map<uint64_t, unique_ptr<CGPoint>>   Points;
};

class CCubeAlgorithm {
public:
  class Cluster {
  public:
    Cluster(ELFCfg *Cfg) : Nodes(1, Cfg) {}
    ~Cluster() {}
    list<ELFCfg *> Nodes;
  };

public:
  CCubeAlgorithm(PLO &P) : Plo(P) {}

  bool DoOrder(list<ELFCfg *> &OrderResult);

  PLO &Plo;
  list<unique_ptr<Cluster>> Clusters;
};

template <class ReorderingAlgorithm>
class PLOFuncOrdering {
 public:
  PLOFuncOrdering(PLO &P) :Algo(P) {}
  ~PLOFuncOrdering() {}

  bool DoOrder(list<ELFCfg *> &OrderResult) {
    return Algo.DoOrder(OrderResult);
  }

  ReorderingAlgorithm Algo;
};
 
}
}

#endif
