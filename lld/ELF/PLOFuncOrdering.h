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
namespace plo {

class PLO;
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

/*
class PHAlgorithm {
public:
  class Cluster {
  public:
    Cluster(ELFCfg *Cfg);
    ~Cluster();
    list<ELFCfg *> Cfgs;
    uint64_t       Size;
    double         Density;

    // Merge "Other" cluster into this cluster.
    Cluster & operator << (Cluster &Other) {
      Cfgs.insert(Cfgs.end(), Other.Cfgs.begin(), Other.Cfgs.end());
      this->Density = (Density * Size + Other.Density * Other.Size)
          / (this->Size + Other.Size);
      this->Size += Other.Size;
      return *this;
    }

    // Handler is used to remove itself from ownership list without
    // the need to iterate through the list.
    list<unique_ptr<Cluster>>::iterator Handler;
  };

public:
  PHAlgorithm(PLO &P) : Plo(P) {}
  list<ELFCfg *> doOrder();

private:
  ELFCfg *getMostLikelyPredecessor(
      Cluster *Cluster, ELFCfg *Cfg,
      map<ELFCfg *, CCubeAlgorithm::Cluster *> &ClusterMap);

  void mergeClusters();
  void sortClusters();

  PLO &Plo;
  list<unique_ptr<Cluster>> Clusters;
};
*/


template <class ReorderingAlgorithm>
class PLOFuncOrdering {
 public:
  PLOFuncOrdering(PLO &P) :Algo(P) {}
  ~PLOFuncOrdering() {}

  list<const ELFCfg *> doOrder() {
    return Algo.doOrder();
  }

  ReorderingAlgorithm Algo;
};

}
}

#endif
