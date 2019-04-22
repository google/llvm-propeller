#include "PLOBBOrdering.h"

#include <algorithm>
#include <iostream>
#include <map>

#include "llvm/ADT/StringRef.h"

#include "PLOELFCfg.h"

using std::map;
using std::pair;

namespace lld {
namespace plo {

PLOBBOrdering::~PLOBBOrdering() {}
  
PLOBBOrdering::PLOBBOrdering(ELFCfg &C) : Cfg(C) {
  for (auto &Uptr: Cfg.Nodes) {
    Chains.emplace_back(new BBChain(Uptr.get()));
  }
}

void PLOBBOrdering::ConnectChain(ELFCfgEdge *Edge, BBChain *C1, BBChain *C2) {
  // std::cout << "Merging edge: " << *Edge << std::endl;
  // std::cout << "  " << *C1 << std::endl;
  // std::cout << "  " << *C2 << std::endl;
  C1->Nodes.insert(C1->Nodes.end(), C2->Nodes.begin(), C2->Nodes.end());
  C2->Nodes.clear();
  // std::cout << "==>" << std::endl;
  // std::cout << "  " << *C1 << std::endl;
}

void PLOBBOrdering::DoOrder(list<StringRef> &HotSymbols, list<StringRef> &ColdSymbols) {
  // Sort Cfg intra edges
  list<ELFCfgEdge *> Edges;
  std::for_each(Cfg.IntraEdges.begin(), Cfg.IntraEdges.end(),
                [&Edges](unique_ptr<ELFCfgEdge> &U) {
                  if (U->Type == ELFCfgEdge::INTRA_FUNC) {
                    Edges.push_back(U.get());
                  }
                });
  Edges.sort([](ELFCfgEdge *E1, ELFCfgEdge *E2) {
                return E1->Weight < E2->Weight;
              });
  for (auto P = Edges.rbegin(), Q = Edges.rend(); P != Q; ++P) {
    ELFCfgEdge *Edge = *P;
    if (Edge->Weight == 0) {
      break;
    }
    for (auto I = Chains.begin(), E = Chains.end(); I != E; ++I) {
      auto *C1 = (*I).get();
      auto *C1Head = *(C1->Nodes.begin());
      auto *C1Tail = *(C1->Nodes.rbegin());
      if (!(Edge->Src == C1Tail || Edge->Src == C1Head ||
            Edge->Sink == C1Tail || Edge->Sink == C1Head)) {
        continue;
      }
      for (auto J = std::next(I); J != E; ++J) {
        auto C2 = (*J).get();
        auto *C2Head = *(C2->Nodes.begin());
        auto *C2Tail = *(C2->Nodes.rbegin());
        if (Edge->Src == C1Tail && Edge->Sink == C2Head) {
          ConnectChain(Edge, C1, C2);
          Chains.erase(J);
          goto done;
        } else if (Edge->Src == C2Tail && Edge->Sink == C1Head) {
          ConnectChain(Edge, C2, C1);
          Chains.erase(I);
          goto done;
        }
      }
     }
   done:
     ;
  }

  for (auto &C: Chains) {
    if (C->Nodes.size() == 1) {
      ColdSymbols.push_back((*C->Nodes.begin())->ShName);
    } else {
      for (auto *N: C->Nodes) {
        HotSymbols.push_back(N->ShName);
      }
    }
  }
}

ostream &operator << (ostream & Out, BBChain &C) {
  Out << "{";
  for (auto *N: C.Nodes) {
    Out << " " << *N;
  }
  Out << " }";
  return Out;
}

// static bool Linkable(MeshPoint *MP1, MeshPoint *MP2) {
//   auto NodeIsConnected = [](ELFCfgNode *N1, ELFCfgNode *N2) {
//                            for (ELFCfgEdge *E: N1->Outs) {
//                              if (E->Sink == N2)
//                                return true;
//                            }
//                            return false;
//                          };
//   return NodeIsConnected((*MP1->Nodes.rbegin()), (*MP2->Nodes.begin())) ||
//     NodeIsConnected((*MP2->Nodes.rbegin()), (*MP1->Nodes.begin()));
// }

}
};
