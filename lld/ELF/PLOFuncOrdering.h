#ifndef LLD_ELF_PLO_FUNC_ORDERING_H
#define LLD_ELF_PLO_FUNC_ORDERING_H

#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <utility>

using std::list;
using std::map;
using std::unique_ptr;

namespace lld {
namespace plo {

class PLO;
class ELFCfg;

class CCubeAlgorithm {
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
      this->Size += Other.Size;
      this->Density = (Density * Size + Other.Density * Other.Size)
          / (this->Size + Other.Size);
      return *this;
    }

    // Handler is used to remove itself from ownership list without
    // the need to iterate through the list.
    list<unique_ptr<Cluster>>::iterator Handler;
  };

public:
  CCubeAlgorithm(PLO &P) : Plo(P) {}
  list<ELFCfg *> DoOrder();

private:
  ELFCfg *MostLikelyPredecessor(
      Cluster *Cluster, ELFCfg *Cfg,
      map<ELFCfg *, CCubeAlgorithm::Cluster *> &ClusterMap);

  void MergeClusters();
  void SortClusters();

  PLO &Plo;
  list<unique_ptr<Cluster>> Clusters;
};

template <class ReorderingAlgorithm>
class PLOFuncOrdering {
 public:
  PLOFuncOrdering(PLO &P) :Algo(P) {}
  ~PLOFuncOrdering() {}

  list<ELFCfg *> DoOrder() {
    return Algo.DoOrder();
  }

  ReorderingAlgorithm Algo;
};

}
}

#endif
