#include "PLOBBOrdering.h"

#include <algorithm>
#include <map>

#include "llvm/ADT/StringRef.h"

#include "PLOELFCfg.h"

using std::map;
using std::pair;

namespace lld {
namespace plo {

MeshLink *Mesh::CreateLink(MeshPoint *P1, MeshPoint *P2, uint64_t W) {
  MeshLink *L = new MeshLink(P1, P2, W);
  P1->Links.emplace_back(L);
  Links.emplace_back(L);
  return L;
}

void Mesh::Init() {
  map<ELFCfgNode *, MeshPoint *> MeshMap;
  for (auto &NodeUptr: Cfg.Nodes) {
    MeshPoint *MEP = new MeshPoint();
    this->Points.emplace_back(MEP);
    ELFCfgNode *Node = NodeUptr.get();
    MEP->Nodes.push_back(Node);
    MeshMap[Node] = MEP;
  }

  for (auto &Edge: Cfg.IntraEdges) {
    if (Edge->Src != Edge->Sink) {
      CreateLink(MeshMap[Edge->Src], MeshMap[Edge->Sink], Edge->Weight);
    }
  }
}

template <class T, class Deleter>
static void Dedupe(T &Links, Deleter D) {
  map<pair<MeshPoint *, MeshPoint *>, MeshLink *> LinkMap;
  for (auto P = Links.begin(), E = Links.end(); P != E; ) {
    auto &L = *P;
    auto I = LinkMap.find(pair<MeshPoint *, MeshPoint *>(L->Src,  L->Sink));
    if (I != LinkMap.end()) {
      MeshLink *ExistingLink = I->second;
      ExistingLink->Weight += L->Weight;
      D(P);
    } else {
      // &(*L) works here for "MeshLink *" and "unique_ptr<MeshLink>",
      // and the result type is "MeshLink *".
      LinkMap.emplace(std::piecewise_construct,
                      std::forward_as_tuple(L->Src, L->Sink),
                      std::forward_as_tuple(&(*L)));
    }
  }
}

MeshPoint *Mesh::ReduceLink(MeshLink *ML) {
  MeshPoint *P1 = ML->Src;
  MeshPoint *P2 = ML->Sink;
  auto D = [&P1, &P2](MeshLink *V) { return V->Sink == P1 || V->Sink == P2; };
  P1->Links.remove_if(D);
  P2->Links.remove_if(D);
  this->Links.remove_if([&P1, &P2](unique_ptr<MeshLink> &V) {
                          return (V->Src == P1 && V->Sink == P1)
                            || (V->Src == P1 && V->Sink == P2)
                            || (V->Src == P2 && V->Sink == P1)
                            || (V->Src == P2 && V->Sink == P2); });

  MeshPoint *P3 = new MeshPoint();
  P3->Nodes.insert(P1->Nodes.begin(), P1->Nodes.end(), P3->Nodes.end());
  P3->Nodes.insert(P2->Nodes.begin(), P2->Nodes.end(), P3->Nodes.end());

  P3->Links.insert(P1->Links.begin(), P1->Links.end(), P3->Links.end());
  P3->Links.insert(P2->Links.begin(), P2->Links.end(), P3->Links.end());
  std::for_each(P3->Links.begin(), P3->Links.end(),
                [&P3](MeshLink *L) { L->Src = P3; });
  std::for_each(this->Links.begin(), this->Links.end(),
                [&P1, &P2, &P3](unique_ptr<MeshLink> &L) {
                  if (L->Sink == P1 || L->Sink == P2)
                    L->Sink = P3;
                });

  Dedupe(P3->Links, [this, &P3](list<MeshLink *>::iterator I) {
      P3->Links.erase(I);
      // Delete the link and release memory.
      this->Links.remove_if([&I](unique_ptr<MeshLink> &Handle) {
                              return Handle.get() == *I;
                            });
    });
  Dedupe(this->Links, [this](list<unique_ptr<MeshLink>>::iterator I) {
      this->Links.erase(I);
    });

  this->Points.remove_if([&P1, &P2](unique_ptr<MeshPoint> &Handler) {
                           return Handler.get() == P1 || Handler.get() == P2;
                         });
  this->Points.emplace_back(P3);
  return P3;
}


void Mesh::Reduce() {
  while (true) {
    MeshLink *ML = nullptr;
    for (auto &L: this->Links) {
      if (!ML || ML->Weight < L->Weight) {
        ML = L.get();
      }
    }
    if (ML && ML->Weight)
      ReduceLink(ML);
    else
      break;
  }
}


std::ostream & operator << (std::ostream &Out, const MeshPoint &P) {
  Out << P.GetName().str() << "[ ";
  for (auto *N: P.Nodes) {
    Out << N->GetShortName().str() << " ";
  }
  Out << "]";
  return Out;
}

std::ostream & operator << (std::ostream &Out, const MeshLink  &L) {
  Out << L.Src->GetName().str() << " -> " << L.Sink->GetName().str();
  return Out;
}
  
std::ostream & operator << (std::ostream &Out, const Mesh &M) {
  Out << "Mesh: " << M.Cfg.Name.str() << std::endl;
  for (auto &P: M.Points) {
    Out << "  P: " << *P << std::endl;
  }
  for (auto &L: M.Links) {
    Out << "  L: " << *L << std::endl;
  }
  return Out;
}


}
};
