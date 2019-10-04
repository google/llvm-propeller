//===- PropellerFuncReordering.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
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
    Cluster(ELFCFG *cfg, unsigned);
    // All cfgs in this cluster
    std::vector<ELFCFG *> CFGs;
    // Unique id associated with the cluster
    unsigned Id;
    // Total binary size of this cluster (only the hot part if using
    // split-funcs.
    uint64_t Size;
    // Total byte-level execution frequency of the cluster
    uint64_t Weight;

    // Merge the "other" cluster into this cluster.
    Cluster &mergeWith(Cluster &other) {
      CFGs.insert(CFGs.end(), other.CFGs.begin(), other.CFGs.end());
      this->Weight += other.Weight;
      this->Size += other.Size;
      return *this;
    }

    // Returns the per-byte execution density of this cluster
    double getDensity() { return ((double)Weight) / Size; }
  };

  CallChainClustering() {}

  void init(Propeller &propeller);

  unsigned doOrder(std::list<ELFCFG *> &cfgOrder);

private:
  unsigned ClusterCount = 0;

  ELFCFG *getMostLikelyPredecessor(ELFCFG *cfg, Cluster *cluster);

  void mergeClusters();
  void sortClusters(std::vector<Cluster *> &);

  std::vector<ELFCFG *> HotCFGs, ColdCFGs;
  std::map<ELFCFG *, Cluster *> CFGToClusterMap;
  std::map<unsigned, std::unique_ptr<Cluster>> Clusters;
};

} // namespace propeller
} // namespace lld

#endif
