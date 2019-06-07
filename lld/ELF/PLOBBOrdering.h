#ifndef LLD_ELF_PROPELLER_BB_ORDERING_H
#define LLD_ELF_PROPELLER_BB_ORDERING_H

#include "llvm/ADT/StringRef.h"

#include <list>
#include <memory>
#include <ostream>

using std::list;
using std::unique_ptr;
using llvm::StringRef;

namespace lld {
namespace propeller {

class ELFCfgEdge;
class ELFCfgNode;
class ELFCfg;

class BBChain {
public:
  BBChain(ELFCfgNode *N);
  ~BBChain();
  list<ELFCfgNode *> Nodes;
  uint64_t           Size;
  double             Density;
};

class PLOBBOrdering {
public:
  PLOBBOrdering(ELFCfg &C);
  ~PLOBBOrdering();

  void doOrder(list<StringRef> &SymbolList,
               list<StringRef>::iterator HotPlaceHolder,
               list<StringRef>::iterator ColdPlaceHolder);

  void ConnectChain(ELFCfgEdge *E, BBChain *C1, BBChain *C2);

  ELFCfg &Cfg;
  list<unique_ptr<BBChain>>  Chains;
};

std::ostream &operator << (std::ostream & Out, BBChain &C);
  
}
}
#endif
