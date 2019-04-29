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
   Cluster *Cluster, ELFCfg *Cfg, uint64_t CfgWeight,
   map<ELFCfg *, CCubeAlgorithm::Cluster *> &ClusterMap) {
  ELFCfgNode *Entry = Cfg->GetEntryNode();
  if (!Entry) return nullptr;
  ELFCfgEdge *E = nullptr;
  for (ELFCfgEdge *CallIn: Entry->CallIns) {
    if (CallIn->Type != ELFCfgEdge::INTER_FUNC_CALL ||
        CallIn->Src->Cfg == Cfg) continue;
    if (!E || E->Weight / (double)CfgWeight
        < CallIn->Weight / (double)CfgWeight ) {
      // Check if caller is in the same Cluster as callee, is so, skip.
      if (ClusterMap[CallIn->Src->Cfg] == Cluster) continue;
      // if (ClusterMap[CallIn->Src->Cfg]->Size > (1 << 21)) continue;
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

    ELFCfg *PredecessorCfg = MostLikelyPredecessor(
        Cluster, Cfg, -P->first, ClusterMap);
    if (!PredecessorCfg) continue;
    assert(PredecessorCfg != Cfg);
    auto *PredecessorCluster = ClusterMap[PredecessorCfg];
    assert(PredecessorCluster);

    if (PredecessorCluster == Cluster) continue;
    if (PredecessorCluster->Size + Cluster->Size > 4096) continue;

    // Join 2 clusters into PredecessorCluster.
    fprintf(stderr, "Before: density: %.3f & %.3f\n",
            PredecessorCluster->Density, Cluster->Density);
    *PredecessorCluster << *Cluster;
    fprintf(stderr, "After: density: %.3f\n", PredecessorCluster->Density);

    // Update Cfg <-> Cluster mapping, because all cfgs that were
    // previsously in Cluster are now in PredecessorCluster.
    for (ELFCfg *Cfg: Cluster->Cfgs) {
      ClusterMap[Cfg] = PredecessorCluster;
    }
    Clusters.erase(Cluster->Handler);
  }
  fprintf(stderr, "Total cluster size after: %lu\n", Clusters.size());
}

void CCubeAlgorithm::SortClusters() {
  Clusters.sort([](unique_ptr<Cluster> &C1,
                   unique_ptr<Cluster> &C2) {
                  return -C1->Density < -C2->Density;
                });
}

list<ELFCfg *> CCubeAlgorithm::DoOrder() {
  MergeClusters();
  SortClusters();
  list<ELFCfg *> L;
  for (auto &Cptr : Clusters) {
    L.insert(L.end(), Cptr->Cfgs.begin(), Cptr->Cfgs.end());
  }
  return L;
}

}
}
