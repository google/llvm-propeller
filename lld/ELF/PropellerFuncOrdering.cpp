#include "PropellerFuncOrdering.h"

#include "Config.h"
#include "PropellerELFCfg.h"

#include <algorithm>
#include <map>

using lld::elf::config;
using std::map;

namespace lld {
namespace propeller {

void CCubeAlgorithm::init(Propeller &propeller) {
  propeller.forEachCfgRef([this](ELFCfg &cfg) {
    if (cfg.isHot())
      this->HotCFGs.push_back(&cfg);
    else
      this->ColdCFGs.push_back(&cfg);
  });

  auto cfgComparator = [](ELFCfg *cfg1, ELFCfg *cfg2) ->bool {
    return cfg1->getEntryNode()->MappedAddr < cfg2->getEntryNode()->MappedAddr;
  };
  auto sortCFGs = [&cfgComparator](std::vector<ELFCfg *> &cfgs) {
    std::sort(cfgs.begin(), cfgs.end(), cfgComparator);
  };
  sortCFGs(HotCFGs);
  sortCFGs(ColdCFGs);
}

CCubeAlgorithm::Cluster::Cluster(ELFCfg *cfg, unsigned id)
    : CFGs(1, cfg),  Id(id) {}

ELFCfg *CCubeAlgorithm::getMostLikelyPredecessor(ELFCfg *cfg, Cluster *cluster) {
  ELFCfgNode *entry = cfg->getEntryNode();
  if (!entry)
    return nullptr;
  ELFCfgEdge *e = nullptr;
  for (ELFCfgEdge *callIn : entry->CallIns) {
    auto *caller = callIn->Src->Cfg;
    auto *callerCluster = CFGToClusterMap[caller];
    assert(caller->isHot());
    if (caller == cfg || callerCluster == cluster)
      continue;
    // Ignore calls which are cold relative to the callee
    if (callIn->Weight * 10 < entry->Freq)
      continue;
    // Do not merge if the caller cluster's density would degrade by more than
    // 1/8.
    if (8 * callerCluster->Size * (cluster->Weight * callerCluster->Weight) <
        callerCluster->Weight * (cluster->Size + callerCluster->Size))
      continue;
    if (!e || e->Weight < callIn->Weight) {
      if (CFGToClusterMap[callIn->Src->Cfg]->Size > (1 << 21)) continue;
      e = callIn;
    }
  }
  return e ? e->Src->Cfg : nullptr;
}

void CCubeAlgorithm::mergeClusters() {
  map<ELFCfg *, double> cfgWeightMap;
  for(ELFCfg * cfg: HotCFGs){
    uint64_t cfgWeight = 0;
    uint64_t cfgSize = config->propellerSplitFuncs ? 0 : (double)cfg->Size;
    cfg->forEachNodeRef([&cfgSize, &cfgWeight](ELFCfgNode &n) {
      cfgWeight += n.Freq * n.ShSize;
      if (config->propellerSplitFuncs && n.Freq)
        cfgSize += n.ShSize;
    });

    Cluster *c = new Cluster(cfg, ClusterCount++);
    c->Weight = cfgWeight;
    c->Size = std::max(cfgSize, (uint64_t)1);
    cfgWeightMap.emplace(cfg, c->getDensity());
    Clusters.emplace(c->Id, c);
    CFGToClusterMap[cfg] = c;
  }

  std::stable_sort(HotCFGs.begin(), HotCFGs.end(),
            [&cfgWeightMap] (ELFCfg* cfg1, ELFCfg* cfg2){
              return cfgWeightMap[cfg1] > cfgWeightMap[cfg2];
            });
  for (ELFCfg* cfg : HotCFGs){
    if (cfgWeightMap[cfg] <= 0.005)
      break;
    auto *cluster = CFGToClusterMap[cfg];
    if (cluster->Size > (1 << 21))
      continue;
    assert(cluster);

    ELFCfg *predecessorCfg = getMostLikelyPredecessor(cfg, cluster);
    if (!predecessorCfg)
      continue;
    assert(predecessorCfg != cfg);
    auto *predecessorCluster = CFGToClusterMap[predecessorCfg];
    assert(predecessorCluster);

    if (predecessorCluster == cluster)
      continue;

    // Join 2 clusters into PredecessorCluster.
    predecessorCluster->mergeWith(*cluster);

    // Update Cfg <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (ELFCfg *cfg : cluster->CFGs) {
      CFGToClusterMap[cfg] = predecessorCluster;
    }
    Clusters.erase(cluster->Id);
  }
}

void CCubeAlgorithm::sortClusters(std::vector<Cluster*> &clusterOrder) {
  for(auto &p: Clusters)
    clusterOrder.push_back(p.second.get());
  std::sort(clusterOrder.begin(), clusterOrder.end(),
            [](Cluster * c1, Cluster * c2) {
    if (c1->getDensity() == c2->getDensity())
      return c1->CFGs.front()->getEntryNode()->MappedAddr <
             c2->CFGs.front()->getEntryNode()->MappedAddr;
    return c1->getDensity() > c2->getDensity();
  });
}

unsigned CCubeAlgorithm::doOrder(std::list<ELFCfg *>& cfgOrder) {
  mergeClusters();
  std::vector<Cluster*> clusterOrder;
  sortClusters(clusterOrder);
  for (Cluster *c : clusterOrder) {
    cfgOrder.insert(cfgOrder.end(), c->CFGs.begin(), c->CFGs.end());
  }

  cfgOrder.insert(cfgOrder.end(), ColdCFGs.begin(), ColdCFGs.end());
  return HotCFGs.size();
}

}
}
