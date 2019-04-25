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

CCubeAlgorithm::Cluster::Cluster(ELFCfg *Cfg)
  :Cfgs(1, Cfg), Size(Cfg->Size), Density(Cfg->ComputeDensity()) {}

CCubeAlgorithm::Cluster::~Cluster() {}

ELFCfg *CCubeAlgorithm::MostLikelyPredecessor(
   Cluster *Cluster, ELFCfg *Cfg,
   map<ELFCfg *, CCubeAlgorithm::Cluster *> &ClusterMap) {
  ELFCfgNode *Entry = Cfg->GetEntryNode();
  if (!Entry) return nullptr;
  ELFCfgEdge *E = nullptr;
  for (ELFCfgEdge *CallIn: Entry->CallIns) {
    if (CallIn->Type != ELFCfgEdge::INTER_FUNC_CALL ||
        CallIn->Src->Cfg == Cfg) continue;
    if (!E || E->Weight < CallIn->Weight) {
      // Check if caller is in the same Cluster as callee, is so, skip.
      if (ClusterMap[CallIn->Src->Cfg] == Cluster) continue;
      if (ClusterMap[CallIn->Src->Cfg]->Size > 4096) continue;
      E = CallIn;
    }
  }
  return E ? E->Src->Cfg : nullptr;
}

void CCubeAlgorithm::MergeClusters() {
  // Signed integer is used here, because negated weights are used as
  // sorting keys.
  map<int64_t, ELFCfg *> WeightOrder;
  map<ELFCfg *, Cluster *> ClusterMap;
  fprintf(stderr, "Ordering Cfg...\n");
  Plo.ForEachCfgRef([this, &ClusterMap, &WeightOrder](ELFCfg &Cfg) {
                      uint64_t CfgWeight = 0;
                      Cfg.ForEachNodeRef([&CfgWeight](ELFCfgNode &N) {
                                             CfgWeight += N.Weight;
                                           // Use MaxWeight or Sum of weights?
                                           // CfgWeight += N.Weight;
                                         });
                      WeightOrder[-CfgWeight] = &Cfg;
                      Cluster *C = new Cluster(&Cfg);
                      C->Handler = Clusters.emplace(Clusters.end(), C);
                      ClusterMap[&Cfg] = C;
                    });

  fprintf(stderr, "Total cluster size before: %lu\n", Clusters.size());
  for (auto P = WeightOrder.begin(), E = WeightOrder.end();
       P != E; P = WeightOrder.erase(P)) {
    if (P->first == 0) break;
    ELFCfg *Cfg = P->second;
    auto *Cluster = ClusterMap[Cfg];
    assert(Cluster);
    if (Cluster->Size > 4096) continue;

    ELFCfg *PredecessorCfg = MostLikelyPredecessor(Cluster, Cfg, ClusterMap);
    if (!PredecessorCfg) continue;
    assert(PredecessorCfg != Cfg);
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    if (PredecessorCluster->Size > 4096) continue;
    if (PredecessorCluster == Cluster) continue;

    // Join 2 clusters into PredecessorCluster.
    *PredecessorCluster << *Cluster;

    // Update Cfg <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (ELFCfg *Cfg: Cluster->Cfgs) {
      ClusterMap[Cfg] = PredecessorCluster;
    }
    Clusters.erase(Cluster->Handler);
  }
  fprintf(stderr, "Total cluster size after: %lu\n", Clusters.size());
}

map<double, CCubeAlgorithm::Cluster *> CCubeAlgorithm::SortClusters() {
  // Calculate Cluster density
  map<double, Cluster *> ClusterOrder;
  for (auto &Cluster: Clusters) {
    uint64_t TotalExecCnt = 0;
    uint64_t TotalSize = 0;
    for (auto *Cfg: Cluster->Cfgs) {
      TotalExecCnt += Cfg->ComputeDensity() * Cfg->Size;
      TotalSize += Cfg->Size;
    }
    Cluster->Density = TotalExecCnt / (double)TotalSize;
    ClusterOrder[-(Cluster->Density)] = Cluster.get();
  }
  return ClusterOrder;
}

list<ELFCfg *> CCubeAlgorithm::DoOrder() {
  MergeClusters();
  map<double, Cluster *> ClusterOrder = SortClusters();
  list<ELFCfg *> OrderResult;
  for (auto &P: ClusterOrder) {
    OrderResult.insert(OrderResult.end(),
                       P.second->Cfgs.begin(),
                       P.second->Cfgs.end());
  }
  return OrderResult;
}

}
}
