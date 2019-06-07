#include "PropellerFuncOrdering.h"

#include "Config.h"
#include "Propeller.h"
#include "PropellerELFCfg.h"

#include <algorithm>
#include <iostream>
#include <map>

using lld::elf::Config;
using std::map;

namespace lld {
namespace propeller {

template <class CfgContainerTy>
CCubeAlgorithm<CfgContainerTy>::CCubeAlgorithm(CfgContainerTy &P)
    : CfgContainer(P) {
  CfgContainer.forEachCfgRef([this](ELFCfg &Cfg) {
    if (Cfg.isHot())
      HotCfgs.push_back(&Cfg);
    else
      ColdCfgs.push_back(&Cfg);
  });

  fprintf(stderr, "Reordering %zu hot functions.\n", HotCfgs.size());
  vector<const ELFCfg *> AllCfgs[2] = {HotCfgs, ColdCfgs};
  for (auto &CfgVector : AllCfgs) {
    std::sort(CfgVector.begin(), CfgVector.end(),
              [](const ELFCfg *Cfg1, const ELFCfg *Cfg2) {
                return Cfg1->getEntryNode()->MappedAddr <
                       Cfg2->getEntryNode()->MappedAddr;
              });
  }
}

template<class CfgContainerTy>
CCubeAlgorithm<CfgContainerTy>::Cluster::Cluster(const ELFCfg *Cfg)
    : Cfgs(1, Cfg) {}

template<class CfgContainerTy>
CCubeAlgorithm<CfgContainerTy>::Cluster::~Cluster() {}

template <class CfgContainerTy>
const ELFCfg *CCubeAlgorithm<CfgContainerTy>::getMostLikelyPredecessor(
    Cluster *Cluster, const ELFCfg *Cfg,
    map<const ELFCfg *, CCubeAlgorithm<CfgContainerTy>::Cluster *>
        &ClusterMap) {
  ELFCfgNode *Entry = Cfg->getEntryNode();
  if (!Entry)
    return nullptr;
  ELFCfgEdge *E = nullptr;
  for (ELFCfgEdge *CallIn : Entry->CallIns) {
    auto *Caller = CallIn->Src->Cfg;
    auto *CallerCluster = ClusterMap[Caller];
    assert(Caller->isHot());
    if (Caller == Cfg || CallerCluster == Cluster)
      continue;
    // Ignore calls which are cold relative to the callee
    if (CallIn->Weight * 10 < Entry->Freq)
      continue;
    // Do not merge if the caller cluster's density would degrade by more than
    // 1/8.
    if (8 * CallerCluster->Size * (Cluster->Weight * CallerCluster->Weight) <
        CallerCluster->Weight * (Cluster->Size + CallerCluster->Size))
      continue;
    if (!E || E->Weight < CallIn->Weight) {
      // if (ClusterMap[CallIn->Src->Cfg]->Size > (1 << 21)) continue;
      E = CallIn;
    }
  }
  return E ? E->Src->Cfg : nullptr;
}

template<class CfgContainerTy>
void CCubeAlgorithm<CfgContainerTy>::mergeClusters() {
  // Signed key is used here, because negated density are used as
  // sorting keys.
  map<const ELFCfg *, double> CfgWeightMap;
  map<const ELFCfg *, Cluster *> ClusterMap;
  for(const ELFCfg * Cfg: HotCfgs){
    uint64_t CfgWeight = 0;
    double CfgSize = Config->SplitFunctions ? 0 : (double)Cfg->Size;
    Cfg->forEachNodeRefConst([&CfgSize, &CfgWeight](ELFCfgNode &N) {
      CfgWeight += N.Freq;
      if (Config->SplitFunctions && N.Freq)
        CfgSize += N.ShSize;
    });
    
    assert(CfgSize!=0);
    Cluster *C = new Cluster(Cfg);
    C->Weight = CfgWeight;
    C->Size = CfgSize;
    CfgWeightMap.emplace(Cfg, C->getDensity());
    C->Handler = Clusters.emplace(Clusters.end(), C);
    ClusterMap[Cfg] = C;
  }

  std::stable_sort(HotCfgs.begin(), HotCfgs.end(),
            [&CfgWeightMap] (const ELFCfg* Cfg1, const ELFCfg* Cfg2){
              return CfgWeightMap[Cfg1] > CfgWeightMap[Cfg2];
            });
  for (const ELFCfg* Cfg : HotCfgs){
    // "P->second" is in the range of [0, a_large_number]
    if (CfgWeightMap[Cfg] <= 0.005)
      break;
    auto *Cluster = ClusterMap[Cfg];
    assert(Cluster);

    const ELFCfg *PredecessorCfg =
        getMostLikelyPredecessor(Cluster, Cfg, ClusterMap);
    if (!PredecessorCfg)
      continue;
    assert(PredecessorCfg != Cfg);
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    assert(PredecessorCluster);

    if (PredecessorCluster == Cluster)
      continue;
    if (PredecessorCluster->Size + Cluster->Size > (1 << 21))
      continue;

    // Join 2 clusters into PredecessorCluster.
    *PredecessorCluster << *Cluster;
    
    // Update Cfg <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (const ELFCfg *Cfg : Cluster->Cfgs) {
      ClusterMap[Cfg] = PredecessorCluster;
    }
    Clusters.erase(Cluster->Handler);
  }
}

template <class CfgContainerTy>
void CCubeAlgorithm<CfgContainerTy>::sortClusters() {
  Clusters.sort([](unique_ptr<Cluster> &C1, unique_ptr<Cluster> &C2) {
    if (C1->getDensity() == C2->getDensity())
      return C1->Cfgs.front()->getEntryNode()->MappedAddr <
             C2->Cfgs.front()->getEntryNode()->MappedAddr;
    return C1->getDensity() > C2->getDensity();
  });
}

template<class CfgContainerTy>
list<const ELFCfg *> CCubeAlgorithm<CfgContainerTy>::doOrder() {
  mergeClusters();
  sortClusters();
  list<const ELFCfg *> L;
  for (auto &Cptr : Clusters) {
    L.insert(L.end(), Cptr->Cfgs.begin(), Cptr->Cfgs.end());
  }

  L.insert(L.end(), ColdCfgs.begin(), ColdCfgs.end());
  return L;
}

template class CCubeAlgorithm<lld::propeller::Propeller>;
 
}
}
