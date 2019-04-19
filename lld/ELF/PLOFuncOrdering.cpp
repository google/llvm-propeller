#include "PLOFuncOrdering.h"

#include <map>

#include "PLO.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"

using std::map;

namespace lld {
namespace plo {

PLOFuncOrdering::PLOFuncOrdering(PLO &Plo) {
  map<ELFCfg *, CGPoint *> Map;
  auto FindOrCreatePoint =
    [&Map, this](ELFCfg *Cfg) {
      auto P = Map.find(Cfg);
      if (P != Map.end()) {
        return P->second;
      } else {
        auto *RV = this->CG.CreatePoint(Cfg);
        Map[Cfg] = RV;
        return RV;
      }
    };
  map<pair<StringRef, StringRef>, CGLink *> LinkMap;
  auto FindOrCreateLink =
    [&LinkMap, this](CGPoint *P1, CGPoint *P2) -> CGLink * {
      pair<StringRef, StringRef> Key;
      ELFCfg *C1 = *(P1->Cfgs.begin());
      ELFCfg *C2 = *(P2->Cfgs.begin());
      if (C1->Name < C2->Name) {
        Key.first = C1->Name;
        Key.second = C2->Name;
      } else {
        Key.first = C2->Name;
        Key.second = C1->Name;
      }
      auto I = LinkMap.find(Key);
      if (I != LinkMap.end()) {
        return I->second;
      }
      auto *RV = this->CG.CreateLink(P1, P2);
      LinkMap[Key] = RV;
      return RV;
    };
  
  for (auto &Pair: Plo.CfgMap) {
    StringRef CfgName = Pair.first;
    ELFView *V = *(Pair.second.begin());
    ELFCfg *Cfg = V->Cfgs[CfgName].get();
    CGPoint *P1 = FindOrCreatePoint(Cfg);
    for (auto &IEdge: Cfg->InterEdges) {
      if (IEdge->Src->Cfg == IEdge->Sink->Cfg) continue;
      CGPoint *P2 = FindOrCreatePoint(IEdge->Sink->Cfg);
      CGLink *Link = FindOrCreateLink(P1, P2);
      Link->Weight += IEdge->Weight;
    }
  }
}

using std::endl;
ostream & operator << (ostream &Out, CallGraph &CG) {
  Out << "Global call graph: " << endl;
  for (auto &P: CG.Points) {
    Out << "  " << *P << endl;
  }

  for (auto &L: CG.Links) {
    Out << "  " << *L << endl;
  }
  return Out;
}
  
ostream & operator << (ostream &Out, CGPoint &P) {
  Out << "Point: [";
  for (auto *Cfg: P.Cfgs) {
    Out << " " << Cfg->Name.str();
  }
  Out << " ]";
  return Out;
}
  
ostream & operator << (ostream &Out, CGLink &L) {
  Out << "Link: " << *L.A << " ---[" << L.Weight << "]---> " << *L.B;
  return Out;
}
  
}
}
