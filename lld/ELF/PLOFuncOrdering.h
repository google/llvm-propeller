#ifndef LLD_ELF_PLO_FUNC_ORDERING_H
#define LLD_ELF_PLO_FUNC_ORDERING_H

#include <list>
#include <memory>
#include <ostream>
#include <utility>

using std::list;
using std::pair;
using std::unique_ptr;

namespace lld {
namespace plo {

class PLO;
class CGLink;
class ELFCfg;

class CGPoint {
public:

  CGPoint(ELFCfg *Cfg) : Cfgs(1, Cfg) {}
  ~CGPoint() {}
  
  list<ELFCfg *>  Cfgs;
  list<CGLink *>  Links;
};

class CGLink {
public:
  CGLink(CGPoint *P1, CGPoint *P2) : A(P1), B(P2), Weight(0) {}
  ~CGLink() {}
  
  CGPoint   *A;
  CGPoint   *B;
  uint64_t   Weight;
};

class CallGraph {
public:
  CGPoint *CreatePoint(ELFCfg *Cfg) {
    return Points.insert(Points.end(), unique_ptr<CGPoint>(new CGPoint(Cfg)))->get();
  }

  CGLink *CreateLink(CGPoint *P1, CGPoint *P2) {
    return Links.insert(Links.end(), unique_ptr<CGLink>(new CGLink(P1, P2)))->get();
  }
  
  list<unique_ptr<CGLink>>  Links;
  list<unique_ptr<CGPoint>> Points;
};

class PLOFuncOrdering {
 public:
  PLOFuncOrdering(PLO &Plo);
  ~PLOFuncOrdering() {}

  CallGraph CG;
};

std::ostream & operator << (std::ostream &Out, CallGraph &);
std::ostream & operator << (std::ostream &Out, CGPoint &);
std::ostream & operator << (std::ostream &Out, CGLink &);
  
}
}

#endif
