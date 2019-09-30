#ifndef LLD_ELF_PROPELLER_FUNC_ORDERING_H
#define LLD_ELF_PROPELLER_FUNC_ORDERING_H

#include "Propeller.h"

#include <list>
#include <map>
#include <memory>
#include <vector>

namespace lld {
namespace propeller {

class ELFCfg;

class CCubeAlgorithm {
public:

  class Cluster {
  public:
    Cluster(ELFCfg *cfg, unsigned);
    std::vector<ELFCfg *> CFGs;
    unsigned       Id;
    uint64_t       Size;
    uint64_t       Weight;

    // Merge "other" cluster into this cluster.
    Cluster& mergeWith(Cluster &other) {
      CFGs.insert(CFGs.end(), other.CFGs.begin(), other.CFGs.end());
      this->Weight += other.Weight;
      this->Size += other.Size;
      return *this;
    }

    double getDensity() {return ((double)Weight)/Size;}
  };

  CCubeAlgorithm() {}

  void init(Propeller &propeller);

  unsigned doOrder(std::list<ELFCfg*>& cfgOrder);

private:
  unsigned ClusterCount = 0;

  ELFCfg *getMostLikelyPredecessor(ELFCfg *cfg, Cluster *cluster);

  void mergeClusters();
  void sortClusters(std::vector<Cluster*>&);

  std::vector<ELFCfg *> HotCFGs, ColdCFGs;
  std::map<ELFCfg *, Cluster*> CFGToClusterMap;
  std::map<unsigned, std::unique_ptr<Cluster>> Clusters;
};

}
}

#endif
