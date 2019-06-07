#ifndef LLD_ELF_PLO_FUNC_ORDERING_H
#define LLD_ELF_PLO_FUNC_ORDERING_H

#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

using std::list;
using std::map;
using std::unique_ptr;
using std::vector;

namespace lld {
namespace propeller {

class ELFCfg;

template <class CfgContainerTy>
class CCubeAlgorithm {
public:
  class Cluster {
  public:
    Cluster(const ELFCfg *Cfg);
    ~Cluster();
    list<const ELFCfg *> Cfgs;
    uint64_t       Size;
    uint64_t       Weight;

    // Merge "Other" cluster into this cluster.
    Cluster & operator << (Cluster &Other) {
      Cfgs.insert(Cfgs.end(), Other.Cfgs.begin(), Other.Cfgs.end());
      this->Weight += Other.Weight;
      this->Size += Other.Size;
      return *this;
    }

    double getDensity() {return ((double)Weight)/Size;}

    // Handler is used to remove itself from ownership list without
    // the need to iterate through the list.
    typename list<unique_ptr<Cluster>>::iterator Handler;
  };

public:
  CCubeAlgorithm(CfgContainerTy &P);
  list<const ELFCfg *> doOrder();

private:
  const ELFCfg *getMostLikelyPredecessor(
      Cluster *Cluster, const ELFCfg *Cfg,
      map<const ELFCfg *, CCubeAlgorithm::Cluster *> &ClusterMap);

  void mergeClusters();
  void sortClusters();

  CfgContainerTy &CfgContainer;
  vector<const ELFCfg*> HotCfgs, ColdCfgs;
  list<unique_ptr<Cluster>> Clusters;
};

}
}

#endif
