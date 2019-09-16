#include "PropellerFuncOrdering.h"

#include "Config.h"
#include "Propeller.h"
#include "PropellerELFCfg.h"

#include <algorithm>
#include <map>

using lld::elf::config;
using std::map;

namespace lld {
namespace propeller {

template <class CfgContainerTy>
void CCubeAlgorithm::init(CfgContainerTy &CfgContainer) {
  CfgContainer.forEachCfgRef([this](ELFCfg &Cfg) {
    if (Cfg.isHot())
      this->HotCfgs.push_back(&Cfg);
    else
      this->ColdCfgs.push_back(&Cfg);
  });

  auto CfgComparator = [](ELFCfg *Cfg1, ELFCfg *Cfg2) ->bool {
    return Cfg1->getEntryNode()->MappedAddr < Cfg2->getEntryNode()->MappedAddr;
  };
  auto sortCfg = [&CfgComparator](vector<ELFCfg *> &Cfg) {
    std::sort(Cfg.begin(), Cfg.end(), CfgComparator);
  };
  sortCfg(HotCfgs);
  sortCfg(ColdCfgs);
}

CCubeAlgorithm::Cluster::Cluster(ELFCfg *Cfg)
    : Cfgs(1, Cfg) {}

CCubeAlgorithm::Cluster::~Cluster() {}

ELFCfg *CCubeAlgorithm::getMostLikelyPredecessor(
    Cluster *Cluster, ELFCfg *Cfg,
    map<ELFCfg *, CCubeAlgorithm::Cluster *>
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
      if (ClusterMap[CallIn->Src->Cfg]->Size > (1 << 21)) continue;
      E = CallIn;
    }
  }
  return E ? E->Src->Cfg : nullptr;
}

void CCubeAlgorithm::mergeClusters() {
  // Signed key is used here, because negated density are used as
  // sorting keys.
  map<ELFCfg *, double> CfgWeightMap;
  map<ELFCfg *, Cluster *> ClusterMap;
  for(ELFCfg * Cfg: HotCfgs){
    uint64_t CfgWeight = 0;
    uint64_t CfgSize = config->propellerSplitFuncs ? 0 : (double)Cfg->Size;
    Cfg->forEachNodeRef([&CfgSize, &CfgWeight](ELFCfgNode &N) {
      CfgWeight += N.Freq * N.ShSize;
      if (config->propellerSplitFuncs && N.Freq)
        CfgSize += N.ShSize;
    });

    assert(CfgSize!=0);
    Cluster *C = new Cluster(Cfg);
    C->Weight = CfgWeight;
    C->Size = std::max(CfgSize, (uint64_t)1);
    CfgWeightMap.emplace(Cfg, C->getDensity());
    C->Handler = Clusters.emplace(Clusters.end(), C);
    ClusterMap[Cfg] = C;
  }

  std::stable_sort(HotCfgs.begin(), HotCfgs.end(),
            [&CfgWeightMap] (ELFCfg* Cfg1, ELFCfg* Cfg2){
              return CfgWeightMap[Cfg1] > CfgWeightMap[Cfg2];
            });
  for (ELFCfg* Cfg : HotCfgs){
    if (CfgWeightMap[Cfg] <= 0.005)
      break;
    auto *Cluster = ClusterMap[Cfg];
    if (Cluster->Size > (1 << 21))
      continue;
    assert(Cluster);

    ELFCfg *PredecessorCfg =
        getMostLikelyPredecessor(Cluster, Cfg, ClusterMap);
    if (!PredecessorCfg)
      continue;
    assert(PredecessorCfg != Cfg);
    // log("propeller: most-likely caller of " + Twine(Cfg->Name) + " -> " + Twine(PredecessorCfg->Name));
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    assert(PredecessorCluster);

    if (PredecessorCluster == Cluster)
      continue;
    // if (PredecessorCluster->Size + Cluster->Size > (1 << 21))
    //  continue;

    // Join 2 clusters into PredecessorCluster.
    *PredecessorCluster << *Cluster;

    // Update Cfg <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (ELFCfg *Cfg : Cluster->Cfgs) {
      ClusterMap[Cfg] = PredecessorCluster;
    }
    Clusters.erase(Cluster->Handler);
  }
}

void CCubeAlgorithm::sortClusters() {
  Clusters.sort([](unique_ptr<Cluster> &C1, unique_ptr<Cluster> &C2) {
    if (C1->getDensity() == C2->getDensity())
      return C1->Cfgs.front()->getEntryNode()->MappedAddr <
             C2->Cfgs.front()->getEntryNode()->MappedAddr;
    return C1->getDensity() > C2->getDensity();
  });
}

unsigned CCubeAlgorithm::doOrder(list<ELFCfg *>& CfgOrder) {
  mergeClusters();
  sortClusters();
  for (auto &Cptr : Clusters) {
    CfgOrder.insert(CfgOrder.end(), Cptr->Cfgs.begin(), Cptr->Cfgs.end());
  }

  CfgOrder.insert(CfgOrder.end(), ColdCfgs.begin(), ColdCfgs.end());
  return HotCfgs.size();
}

template void CCubeAlgorithm::init<Propeller>(Propeller &);

}
}
