//===- PropellerFuncReordering.h
//--------------------------------------------===//
////
//// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
//// See https://llvm.org/LICENSE.txt for license information.
//// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
////
////===----------------------------------------------------------------------===//

#ifndef LLD_ELF_PROPELLER_FUNC_ORDERING_H
#define LLD_ELF_PROPELLER_FUNC_ORDERING_H

#include "Propeller.h"

#include <list>
#include <map>
#include <vector>

namespace lld {
namespace propeller {

class ELFCFG;

class CallChainClustering {
public:
  class Cluster {
  public:
    Cluster(ELFCFG *CFG);
    ~Cluster();
    list<ELFCFG *> CFGs;
    uint64_t       Size;
    uint64_t       Weight;

    // Merge "Other" cluster into this cluster.
    Cluster & operator << (Cluster &Other) {
      CFGs.insert(CFGs.end(), Other.CFGs.begin(), Other.CFGs.end());
      this->Weight += Other.Weight;
      this->Size += Other.Size;
      return *this;
    }

    // Returns the per-byte execution density of this cluster
    double getDensity() { return ((double)Weight) / Size; }
  };

  CallChainClustering() {}

  unsigned doOrder(list<ELFCFG*>& CfgOrder);

private:
  ELFCFG *getMostLikelyPredecessor(
      Cluster *Cluster, ELFCFG *CFG,
      map<ELFCFG *, CCubeAlgorithm::Cluster *> &ClusterMap);

  void mergeClusters();
  void sortClusters(std::vector<Cluster *> &);

  vector<ELFCFG *> HotCfgs, ColdCfgs;
  list<unique_ptr<Cluster>> Clusters;
};

} // namespace propeller
} // namespace lld

#endif
