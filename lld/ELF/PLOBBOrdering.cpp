#include "PLOBBOrdering.h"

#include <algorithm>
#include <iostream>
#include <map>

#include "llvm/ADT/StringRef.h"

#include "PLO.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"

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

BBChain::BBChain(ELFCfgNode *N) : Nodes(1, N), Size(N->ShSize) {}
BBChain::~BBChain() {}

void PLOBBOrdering::ConnectChain(ELFCfgEdge *Edge, BBChain *C1, BBChain *C2) {
  // std::cout << "Merging edge: " << *Edge << std::endl;
  // std::cout << "  " << *C1 << std::endl;
  // std::cout << "  " << *C2 << std::endl;
  C1->Nodes.insert(C1->Nodes.end(), C2->Nodes.begin(), C2->Nodes.end());
  C1->Size += C2->Size;
  C2->Nodes.clear();
  // std::cout << "==>" << std::endl;
  // std::cout << "  " << *C1 << std::endl;
}

void PLOBBOrdering::DoOrder(list<StringRef> &SymbolList,
                            list<StringRef>::iterator HotPlaceHolder,
                            list<StringRef>::iterator ColdPlaceHolder) {
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

  // for (auto &C: Chains) {
  //   if (C->Nodes.size() == 1) {
  //     ColdSymbols.push_back((*C->Nodes.begin())->ShName);
  //   } else {
  //     for (auto *N: C->Nodes) {
  //       HotSymbols.push_back(N->ShName);
  //     }
  //   }
  // }

  // for (auto I = Chains.begin(), E = Chains.end(); I != E;) {
  //   if ((*I)->Nodes.size() == 1) {
  //     ColdSymbols.push_back((*(*I)->Nodes.begin())->ShName);
  //     Chains.erase(I++);
  //   } else {
  //     ++I;
  //   }
  // }

  // creating nodes -> Chain mapping:
  map<ELFCfgNode *, BBChain *> Map;
  for (auto &C: Chains) {
    for (auto *N: C->Nodes) {
      Map[N] = C.get();
    }
  }

  // Calculating chain density
  for (auto &C: Chains) {
    uint64_t TotalNodeSize = 0;
    for (auto *N: C->Nodes) {
      TotalNodeSize += N->ShSize;
    }
    C->Density = TotalNodeSize / C->Nodes.size();
  }

  auto HasEdgeOver =
    [&Map](BBChain *C1, BBChain *C2) {
      uint64_t result = 0;
      for (auto *N: C1->Nodes) {
        if (N->Outs.size() == 2) {
          ELFCfgEdge *E1 = *(N->Outs.begin());
          ELFCfgEdge *E2 = *(N->Outs.rbegin());
          ELFCfgEdge *LesserEdge = nullptr;
          if (E1->Weight == E2->Weight) continue;
          if (E1->Weight > E2->Weight) {
            LesserEdge = E2;
          } else {
            LesserEdge = E1;
          }
          auto R = Map.find(LesserEdge->Sink);
          if (R != Map.end() && R->second == C2) {
            if (result == 0 || result < LesserEdge->Weight) {
              result = LesserEdge->Weight;
            }
          }
        }
      }
      return result;
    };

  auto CompareChain =
    [&HasEdgeOver](unique_ptr<BBChain> &C1, unique_ptr<BBChain> &C2) {
      int R1 = HasEdgeOver(C1.get(), C2.get());
      int R2 = HasEdgeOver(C2.get(), C1.get());
      if (R1 == R2) {
        return -C1->Size < -C2->Size;
      }
      return -R1 < -R2;
    };


  Chains.sort(CompareChain);
  for (auto &C: Chains) {
    for (auto *N: C->Nodes) {
      if (N->Weight)
        SymbolList.insert(HotPlaceHolder, N->ShName);
      else
        SymbolList.insert(ColdPlaceHolder, N->ShName);
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

}
};
