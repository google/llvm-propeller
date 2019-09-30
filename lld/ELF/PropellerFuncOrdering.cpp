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
  CfgContainer.forEachCfgRef([this](ELFCFG &CFG) {
    if (CFG.isHot())
      this->HotCfgs.push_back(&CFG);
    else
      this->ColdCfgs.push_back(&CFG);
  });

  auto CfgComparator = [](ELFCFG *Cfg1, ELFCFG *Cfg2) ->bool {
    return Cfg1->getEntryNode()->MappedAddr < Cfg2->getEntryNode()->MappedAddr;
  };
  auto sortCfg = [&CfgComparator](vector<ELFCFG *> &CFG) {
    std::sort(CFG.begin(), CFG.end(), CfgComparator);
  };
  sortCfg(HotCfgs);
  sortCfg(ColdCfgs);
}

CCubeAlgorithm::Cluster::Cluster(ELFCFG *CFG)
    : CFGs(1, CFG) {}

CCubeAlgorithm::Cluster::~Cluster() {}

ELFCFG *CCubeAlgorithm::getMostLikelyPredecessor(
    Cluster *Cluster, ELFCFG *CFG,
    map<ELFCFG *, CCubeAlgorithm::Cluster *>
        &ClusterMap) {
  ELFCFGNode *Entry = CFG->getEntryNode();
  if (!Entry)
    return nullptr;
  ELFCFGEdge *E = nullptr;
  for (ELFCFGEdge *CallIn : Entry->CallIns) {
    auto *Caller = CallIn->Src->CFG;
    auto *CallerCluster = ClusterMap[Caller];
    assert(Caller->isHot());
    if (Caller == CFG || CallerCluster == Cluster)
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
      if (ClusterMap[CallIn->Src->CFG]->Size > (1 << 21)) continue;
      E = CallIn;
    }
  }
  return E ? E->Src->CFG : nullptr;
}

void CCubeAlgorithm::mergeClusters() {
  // Signed key is used here, because negated density are used as
  // sorting keys.
  map<ELFCFG *, double> CfgWeightMap;
  map<ELFCFG *, Cluster *> ClusterMap;
  for(ELFCFG * CFG: HotCfgs){
    uint64_t CfgWeight = 0;
    uint64_t CfgSize = config->propellerSplitFuncs ? 0 : (double)CFG->Size;
    CFG->forEachNodeRef([&CfgSize, &CfgWeight](ELFCFGNode &N) {
      CfgWeight += N.Freq * N.ShSize;
      if (config->propellerSplitFuncs && N.Freq)
        CfgSize += N.ShSize;
    });

    assert(CfgSize!=0);
    Cluster *C = new Cluster(CFG);
    C->Weight = CfgWeight;
    C->Size = std::max(CfgSize, (uint64_t)1);
    CfgWeightMap.emplace(CFG, C->getDensity());
    C->Handler = Clusters.emplace(Clusters.end(), C);
    ClusterMap[CFG] = C;
  }

  std::stable_sort(HotCfgs.begin(), HotCfgs.end(),
            [&CfgWeightMap] (ELFCFG* Cfg1, ELFCFG* Cfg2){
              return CfgWeightMap[Cfg1] > CfgWeightMap[Cfg2];
            });
  for (ELFCFG* CFG : HotCfgs){
    if (CfgWeightMap[CFG] <= 0.005)
      break;
    auto *Cluster = ClusterMap[CFG];
    if (Cluster->Size > (1 << 21))
      continue;
    assert(Cluster);

    ELFCFG *PredecessorCfg =
        getMostLikelyPredecessor(Cluster, CFG, ClusterMap);
    if (!PredecessorCfg)
      continue;
    assert(PredecessorCfg != CFG);
    // log("propeller: most-likely caller of " + Twine(CFG->Name) + " -> " + Twine(PredecessorCfg->Name));
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    assert(PredecessorCluster);

    if (PredecessorCluster == Cluster)
      continue;
    // if (PredecessorCluster->Size + Cluster->Size > (1 << 21))
    //  continue;

    // Join 2 clusters into PredecessorCluster.
    *PredecessorCluster << *Cluster;

    // Update CFG <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (ELFCFG *CFG : Cluster->CFGs) {
      ClusterMap[CFG] = PredecessorCluster;
    }
    Clusters.erase(Cluster->Handler);
  }
}

void CCubeAlgorithm::sortClusters() {
  Clusters.sort([](unique_ptr<Cluster> &C1, unique_ptr<Cluster> &C2) {
    if (C1->getDensity() == C2->getDensity())
      return C1->CFGs.front()->getEntryNode()->MappedAddr <
             C2->CFGs.front()->getEntryNode()->MappedAddr;
    return C1->getDensity() > C2->getDensity();
  });
}

unsigned CCubeAlgorithm::doOrder(list<ELFCFG *>& CfgOrder) {
  mergeClusters();
  sortClusters();
  for (auto &Cptr : Clusters) {
    CfgOrder.insert(CfgOrder.end(), Cptr->CFGs.begin(), Cptr->CFGs.end());
  }

  CfgOrder.insert(CfgOrder.end(), ColdCfgs.begin(), ColdCfgs.end());
  return HotCfgs.size();
}

template void CCubeAlgorithm::init<Propeller>(Propeller &);

}
}
