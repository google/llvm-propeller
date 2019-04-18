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

class ELFCfgEdge;
class ELFCfgNode;
class ELFCfg;

class BBChain {
public:
  BBChain(ELFCfgNode *N) : Nodes(1, N) {}
  list<ELFCfgNode *> Nodes;
};

class PLOBBOrdering {
public:
  PLOBBOrdering(ELFCfg &C);
  ~PLOBBOrdering();

  void DoOrder();

  void ConnectChain(ELFCfgEdge *E, BBChain *C1, BBChain *C2);

  ELFCfg &Cfg;
  list<unique_ptr<BBChain>>  Chains;
};

ostream &operator << (ostream & Out, BBChain &C);
  
}
}
#endif
