#include "PLOFuncOrdering.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

#include "PLO.h"
#include "PLOBBOrdering.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"

using std::map;
using std::vector;

namespace lld {
namespace plo {

// PLOFuncOrdering::PLOFuncOrdering(PLO &Plo) {
//   CG.InitGraph(Plo);
//   CG.DoOrder();
// }

void CallGraph::InitGraph(PLO &Plo) {
  map<ELFCfg *, CGPoint *> Map;
  auto FindOrCreatePoint =
    [&Map, this](ELFCfg *Cfg) {
      auto P = Map.find(Cfg);
      if (P != Map.end()) {
        return P->second;
      } else {
        auto *RV = this->CreatePoint(Cfg);
        Map[Cfg] = RV;
        return RV;
      }
    };
  for (auto &Pair: Plo.CfgMap) {
    ELFCfg *Cfg = *(Pair.second.begin());
    CGPoint *P1 = FindOrCreatePoint(Cfg);
    for (auto &IEdge: Cfg->InterEdges) {
      if (IEdge->Src->Cfg == IEdge->Sink->Cfg) continue;
      CGPoint *P2 = FindOrCreatePoint(IEdge->Sink->Cfg);
      CGLink *Link = FindOrCreateLink(P1, P2);
      Link->Weight += IEdge->Weight;
    }
  }
}

CGPoint *CallGraph::CollapseLink(CGLink *L) {
  CGPoint *P1 = L->A;
  CGPoint *P2 = L->B;
  if (P1->Weight < L->Weight)
    P1->Weight = L->Weight;
  RemoveLink(L);  // L is deleted now.

  /* TODO: more fine grained merge */
  P1->Cfgs.insert(P1->Cfgs.end(), P2->Cfgs.begin(), P2->Cfgs.end());
  P2->Cfgs.clear();

  while (!P2->Links.empty()) {
    auto *L2 = *(P2->Links.begin());
    // For edges in P2, they are: x <-> P2
    // make them into P1 <-> x
    // If P1 <-> x exists, we update the weight.
    auto *X = L2->A == P2 ? L2->B : L2->A;
    auto *ResultLink = FindOrCreateLink(P1, X);
    ResultLink->Weight += L2->Weight;
    RemoveLink(L2);
  }
  RemovePoint(P2);
  return P1;
}

void CallGraph::DoOrder() {
  while (!Links.empty()) {
    CGLink *MaxLink = nullptr;
    for (auto &LP: Links) {
      if (!MaxLink || LP.second->Weight > MaxLink->Weight) {
        MaxLink = LP.second.get();
      }
    }
    // std::cout << "Collapse: " << *MaxLink << std::endl;
    CollapseLink(MaxLink);
    // std::cout << "Merged point: " << *ResultPoint << std::endl;
  }

  list<StringRef> HotSymbols;
  list<StringRef> ColdSymbols;

  std::cout << "Creating PointHeap" << std::endl;
  std::vector<CGPoint *> PointHeap(Points.size());
  int i = 0;
  for (auto &Pair: Points) {
    PointHeap[i++] = Pair.second.get();
  }
  auto HeapComp = [](CGPoint *P1, CGPoint *P2) {
                   if (P1->Weight == P2->Weight) {
                     return P1->Cfgs.size() < P2->Cfgs.size();
                   }
                   return P1->Weight < P2->Weight;
                  };
  std::make_heap(PointHeap.begin(), PointHeap.end(), HeapComp);
  auto S = PointHeap.begin(), T = PointHeap.end();
  while (S != T) {
    pop_heap(S, T, HeapComp);
    CGPoint *P = *std::prev(T);
    if (P->Weight == 0 || P->Cfgs.size() == 1)
      break;
    for (ELFCfg *Cfg: P->Cfgs) {
      std::cout << "Do order: " << Cfg->Name.str() << std::endl;
      for (auto &N: Cfg->Nodes) {
        std::cout << "SYM: " << N->ShName.str() << std::endl;
      }
      // PLOBBOrdering BBO(*Cfg);
      // BBO.DoOrder(HotSymbols, ColdSymbols);
    }
    T = std::prev(T);
  }

  for (auto &T: HotSymbols) {
    std::cout << "SYM: " << T.str() << std::endl;
  }

  for (auto &T: ColdSymbols) {
    std::cout << "SYM: " << T.str() << std::endl;
  }
  // Output [S, T) as cold symbols
  for (; S != T; ++S) {
    CGPoint *P = *S;
    for (ELFCfg *Cfg: P->Cfgs) {
      for (auto &Node: Cfg->Nodes) {
        std::cout << "SYM: " << Node->ShName.str() << std::endl;
      }
    }
  }
}

bool CCubeAlgorithm::DoOrder(list<ELFCfg *> &OrderResult) {
  map<uint64_t, ELFCfg *> WeightMap;
  map<ELFCfg *, Cluster *> ClusterMap;
  Plo.ForEachCfgRef([this, &ClusterMap, &WeightMap](ELFCfg &Cfg) {
                      uint64_t CfgWeight = 0;
                      Cfg.ForEachNodeRef([&CfgWeight](ELFCfgNode &N) {
                                           CfgWeight += N.Weight;
                                         });
                      WeightMap[CfgWeight] = &Cfg;
                      Cluster *C = new Cluster(&Cfg);
                      Clusters.emplace_back(C);
                      ClusterMap[&Cfg] = C;
                    });

  auto MostLikelyPredecessor = [](ELFCfg *Cfg) -> ELFCfg * {
                                 ELFCfgNode *Entry = Cfg->GetEntryNode();
                                 if (!Entry) return nullptr;
                                 ELFCfgEdge *E = nullptr;
                                 for (ELFCfgEdge *CallIn: Entry->CallIns) {
                                   if (!E || E->Weight < CallIn->Weight) {
                                     E = CallIn;
                                   }
                                 }
                                 return E->Src->Cfg;
                               };

  for (auto P = WeightMap.rbegin(), E = WeightMap.rend(); P != E; ++P) {
    ELFCfg *Cfg = P->second;
    ELFCfg *PredecessorCfg = MostLikelyPredecessor(Cfg);
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    auto *Cluster = ClusterMap[Cfg];
    
  }
  
  return true;
}

using std::endl;
ostream & operator << (ostream &Out, CallGraph &CG) {
  Out << "Global call graph: " << endl;
  for (auto &P: CG.Points) {
    Out << "  " << *(P.second) << endl;
  }

  for (auto &L: CG.Links) {
    Out << "  " << *(L.second) << endl;
  }
  return Out;
}
  
ostream & operator << (ostream &Out, CGPoint &P) {
  Out << "Point: " << P.Ordinal
      << " (W=" << P.Weight << ", C=" << P.Cfgs.size() << ") [";
  for (auto *Cfg: P.Cfgs) {
    Out << " " << Cfg->Name.str();
  }
  Out << " ]";
  return Out;
}
  
ostream & operator << (ostream &Out, CGLink &L) {
  Out << "Link: " << *L.A << " <---[" << L.Weight << "]---> " << *L.B;
  return Out;
}
  
}
}
