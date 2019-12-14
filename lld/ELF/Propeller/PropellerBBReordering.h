//===- PropellerBBReordering.cpp  -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_BB_REORDERING_H
#define LLD_ELF_PROPELLER_BB_REORDERING_H

#include "PropellerConfig.h"
#include "PropellerCFG.h"

#include "lld/Common/LLVM.h"
#include "Heap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <iostream>

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

using llvm::DenseMap;
using llvm::DenseSet;

namespace lld {
namespace propeller {


enum MergeOrder {
  Begin,
  X2X1Y = Begin,
  BeginNext,
  X1YX2 = BeginNext,
  X2YX1,
  YX2X1,
  End
};


// Represents a chain of nodes (basic blocks).
class NodeChain {
public:
  // Representative node of the chain, with which it is initially constructed.
  CFGNode *DelegateNode;
  ControlFlowGraph *CFG;
  std::list<CFGNode *> Nodes;
  std::list<std::list<CFGNode*>::iterator> FunctionEntryIndices;
  std::unordered_map<NodeChain *, std::vector<CFGEdge*>> OutEdges;
  DenseSet<NodeChain *> InEdges;

  // Total binary size of the chain
  uint64_t Size;

  // Total execution frequency of the chain
  uint64_t Freq;

  // Extended TSP score of the chain
  double Score = 0;

  bool DebugChain;

  // Constructor for building a NodeChain from a single Node
  NodeChain(CFGNode *node)
      : DelegateNode(node), CFG(node->CFG), Nodes(1, node), Size(node->ShSize),
        Freq(node->Freq), DebugChain(node->CFG->DebugCFG) {}

  NodeChain(ControlFlowGraph *cfg) : DelegateNode(cfg->getEntryNode()), CFG(cfg), Size(cfg->Size), Freq(0), DebugChain(cfg->DebugCFG) {
    cfg->forEachNodeRef([this] (CFGNode& node) {
      Nodes.push_back(&node);
      Freq += node.Freq;
    });
  }

  template <class Visitor> void forEachOutEdgeToChain(NodeChain* chain, Visitor V) {
    auto it = OutEdges.find(chain);
    if (it == OutEdges.end())
      return;
    for (CFGEdge *E : it->second)
      V(*E, this, chain);
  }

  double execDensity() const {
    return ((double)Freq) / std::max(Size, (uint64_t)1);
  }
};

} //namespace propeller
} //namespace lld

namespace std {

template<>
    struct less<lld::propeller::NodeChain*> {
      bool operator()(const lld::propeller::NodeChain* c1,
                      const lld::propeller::NodeChain* c2) const {
        return c1->DelegateNode->MappedAddr < c2->DelegateNode->MappedAddr;
      }
    };

template<>
    struct less<pair<lld::propeller::NodeChain*, lld::propeller::NodeChain*>> {
      bool operator()(const pair<lld::propeller::NodeChain*, lld::propeller::NodeChain*> p1,
                      const pair<lld::propeller::NodeChain*, lld::propeller::NodeChain*> p2) const {
        if (less<lld::propeller::NodeChain*>()(p1.first, p2.first))
          return true;
        if (less<lld::propeller::NodeChain*>()(p2.first, p1.first))
          return false;
        return less<lld::propeller::NodeChain*>()(p1.second, p2.second);
      }
    };
} //namespace std

namespace lld {
namespace propeller {

class ChainClustering {
 public:
  class Cluster {
    public:
     Cluster(NodeChain*);
     std::vector<NodeChain*> Chains;
     NodeChain * DelegateChain;
     uint64_t Size;
     uint64_t Weight;

     Cluster &mergeWith(Cluster &other) {
       Chains.insert(Chains.end(), other.Chains.begin(), other.Chains.end());
       this->Weight += other.Weight;
       this->Size += other.Size;
       return *this;
     }

     double getDensity() {return ((double)Weight/Size);}
  };

  void mergeTwoClusters(Cluster * predecessorCluster, Cluster * cluster){
    // Join the two clusters into predecessorCluster.
    predecessorCluster->mergeWith(*cluster);

    // Update chain to cluster mapping, because all chains that were
    // previsously in cluster are now in predecessorCluster.
    for (NodeChain * c : cluster->Chains) {
      ChainToClusterMap[c] = predecessorCluster;
    }

    // Delete the defunct cluster
    Clusters.erase(cluster->DelegateChain->DelegateNode->MappedAddr);
  }

  void addChain(std::unique_ptr<NodeChain>&& chain_ptr);

  virtual void doOrder(std::vector<CFGNode*> &hotOrder,
               std::vector<CFGNode*> &coldOrder);

  virtual ~ChainClustering() = default;

 protected:
  virtual void mergeClusters() {};
  void sortClusters(std::vector<Cluster *> &);

  void initClusters() {
    for(auto& c_ptr: HotChains){
      NodeChain * chain = c_ptr.get();
      Cluster *cl = new Cluster(chain);
      cl->Weight = chain->Freq;
      cl->Size = std::max(chain->Size, (uint64_t)1);
      ChainToClusterMap[chain] = cl;
      Clusters.try_emplace(cl->DelegateChain->DelegateNode->MappedAddr, cl);
    }
  }

  std::vector<std::unique_ptr<NodeChain>> HotChains, ColdChains;
  DenseMap<uint64_t, std::unique_ptr<Cluster>> Clusters;
  DenseMap<NodeChain*, Cluster*> ChainToClusterMap;
};

} //namespace propeller
} //namespace lld

namespace std {
template<>
    struct less<lld::propeller::ChainClustering::Cluster*> {
      bool operator()(const lld::propeller::ChainClustering::Cluster* c1,
                      const lld::propeller::ChainClustering::Cluster* c2) const {
        return less<lld::propeller::NodeChain*>()(c1->DelegateChain, c2->DelegateChain);
      }
    };

} //namespace std


namespace lld {
namespace propeller {

class NodeChainSlice {
private:
  // Chain from which this slice comes from
  NodeChain *Chain;

  // The endpoints of the slice in the corresponding chain
  std::list<CFGNode *>::iterator Begin, End;

  // The offsets corresponding to the two endpoints
  uint64_t BeginOffset, EndOffset;

  // Constructor for building a chain slice from a given chain and the two
  // endpoints of the chain.
  NodeChainSlice(NodeChain *c, std::list<CFGNode *>::iterator begin,
                 std::list<CFGNode *>::iterator end)
      : Chain(c), Begin(begin), End(end) {

    BeginOffset = (*begin)->ChainOffset;
    if (End == Chain->Nodes.end())
      EndOffset = Chain->Size;
    else
      EndOffset = (*end)->ChainOffset;
  }

  // (Binary) size of this slice
  uint64_t size() const { return EndOffset - BeginOffset; }

  friend class NodeChainBuilder;
  friend class NodeChainAssembly;
};


class NodeChainAssembly {
private:
  // The gain in ExtTSP score achieved by this NodeChainAssembly once it
  // is accordingly applied to the two chains.
  // This is effectively equal to "Score - splitChain->Score - unsplitChain->Score".
  double ScoreGain = 0;

  // The two chains, the first being the splitChain and the second being the
  // unsplitChain.
  std::pair<NodeChain*, NodeChain*> ChainPair;

  // The slice position of splitchain
  std::list<CFGNode *>::iterator SlicePosition;

  // The three chain slices
  std::vector<NodeChainSlice> Slices;

  // The merge order
  MergeOrder MOrder;

  NodeChainAssembly(NodeChain *chainX, NodeChain *chainY,
                    std::list<CFGNode *>::iterator slicePosition,
                    MergeOrder mOrder)
      : ChainPair(chainX, chainY),
        SlicePosition(slicePosition),  MOrder(mOrder) {
    NodeChainSlice x1(chainX, chainX->Nodes.begin(), SlicePosition);
    NodeChainSlice x2(chainX, SlicePosition, chainX->Nodes.end());
    NodeChainSlice y(chainY, chainY->Nodes.begin(), chainY->Nodes.end());

    switch (MOrder) {
    case MergeOrder::X2X1Y:
      Slices = {std::move(x2), std::move(x1), std::move(y)};
      break;
    case MergeOrder::X1YX2:
      Slices = {std::move(x1), std::move(y), std::move(x2)};
      break;
    case MergeOrder::X2YX1:
      Slices = {std::move(x2), std::move(y), std::move(x1)};
      break;
    case MergeOrder::YX2X1:
      Slices = {std::move(y), std::move(x2), std::move(x1)};
      break;
    default:
      assert("Invalid MergeOrder!" && false);
    }

    ScoreGain = computeExtTSPScore() - splitChain()->Score - unsplitChain()->Score;
  }

  bool isValid() {
    return ScoreGain > 0.0001 && (propellerConfig.optReorderIP || (!splitChain()->Nodes.front()->isEntryNode() && !unsplitChain()->Nodes.front()->isEntryNode()) || getFirstNode()->isEntryNode());
  }

  // Find the NodeChainSlice in this NodeChainAssembly which contains the given
  // node. If the node is not contained in this NodeChainAssembly, then return
  // false. Otherwise, set idx equal to the index of the corresponding slice and
  // return true.
  inline bool findSliceIndex(CFGNode *node, NodeChain * chain, uint64_t offset, uint8_t &idx) const {
    for (idx = 0; idx < 3; ++idx) {
      if (chain != Slices[idx].Chain)
        continue;
      if (offset < Slices[idx].EndOffset && offset > Slices[idx].BeginOffset)
        return true;
      if (offset < Slices[idx].BeginOffset)
        continue;
      if (offset > Slices[idx].EndOffset)
        continue;
      // A node can have zero size, which means multiple nodes may be associated
      // with the same offset. This means that if the node's offset is at the
      // beginning or the end of the slice, the node may reside in either slices
      // of the chain.
      if (offset == Slices[idx].EndOffset) {
        // If offset is at the end of the slice, iterate backwards over the
        // slice to find a zero-sized node.
        for (auto nodeIt = std::prev(Slices[idx].End);
             nodeIt != std::prev(Slices[idx].Begin); nodeIt--) {
          // Stop iterating if the node's size is non-zero as this would change
          // the offset.
          if ((*nodeIt)->ShSize)
            break;
          if (*nodeIt == node)
            return true;
        }
      }
      if (offset == Slices[idx].BeginOffset) {
        // If offset is at the beginning of the slice, iterate forwards over the
        // slice to find the node.
        for (auto nodeIt = Slices[idx].Begin; nodeIt != Slices[idx].End;
             nodeIt++) {
          if (*nodeIt == node)
            return true;
          // Stop iterating if the node's size is non-zero as this would change
          // the offset.
          if ((*nodeIt)->ShSize)
            break;
        }
      }
    }
    return false;
  }

  // Total Extended TSP score of this NodeChainAssembly once it is assembled
  // accordingly.
  double computeExtTSPScore() const;

  // First node in the resulting assembled chain.
  CFGNode *getFirstNode() const {
    for (auto &slice : Slices)
      if (slice.Begin != slice.End)
        return *slice.Begin;
    return nullptr;
  }

  struct CompareNodeChainAssembly {
    bool operator () (const std::unique_ptr<NodeChainAssembly> &a1,
                      const std::unique_ptr<NodeChainAssembly> &a2) const;
  };

  friend class NodeChainBuilder;

public:
  // We delete the copy constructor to make sure NodeChainAssembly is moved
  // rather than copied.
  NodeChainAssembly(NodeChainAssembly &&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;


  std::pair<uint8_t, size_t> assemblyStrategy() const {
    return std::make_pair(MOrder, (*SlicePosition)->MappedAddr);
  }

  inline bool split() const {
    return SlicePosition != splitChain()->Nodes.begin();
  }

  inline NodeChain * splitChain() const {
    return ChainPair.first;
  }

  inline NodeChain * unsplitChain() const {
    return ChainPair.second;
  }
};


// BB Chain builder based on the ExtTSP metric
class NodeChainBuilder {
private:
 NodeChainAssembly::CompareNodeChainAssembly NodeChainAssemblyComparator;
  // Cfgs repreresenting the functions that are reordered
  std::vector<ControlFlowGraph*> CFGs;

  // Set of built chains, keyed by section index of their Delegate Nodes.
  // Chains are removed from this Map once they are merged into other chains.
  DenseMap<uint64_t, std::unique_ptr<NodeChain>> Chains;

  // All the initial chains, seperated into connected components
  std::vector<std::vector<NodeChain*>> Components;

  // NodeChainBuilder performs BB ordering component by component.
  // This is the component number that the chain builder is currently working
  // on.
  unsigned CurrentComponent;

  // These represent all the edges which are -- based on the profile -- the only
  // (executed) outgoing edges from their source node and the only (executed)
  // incoming edges to their sink nodes. The algorithm will make sure that these
  // edges form fall-throughs in the final order.
  DenseMap<CFGNode *, CFGNode *> MutuallyForcedOut;


  // This maps every (ordered) pair of chains (with the first chain in the pair
  // potentially splittable) to the highest-gain NodeChainAssembly for those
  // chains. The Heap data structure allows fast retrieval of the maximum gain
  // NodeChainAssembly, along with fast update.
  Heap<std::pair<NodeChain*, NodeChain*>, std::unique_ptr<NodeChainAssembly>, std::less<std::pair<NodeChain*,NodeChain*>> , NodeChainAssembly::CompareNodeChainAssembly> NodeChainAssemblies;

  // This map stores the candidate chains for each chain.
  //
  // For every chain, its candidate chains are the chains which can increase the
  // overall ExtTSP score when merged with that chain. This is used to update
  // the NodeChainAssemblies map whenever chains merge together. The candidate
  // chains of a chain may also be updated as result of a merge.
  std::unordered_map<NodeChain *, std::unordered_set<NodeChain *>>
      CandidateChains;

  void coalesceChains();
  void initializeExtTSP();
  void initializeComponents();
  void attachFallThroughs();

  // This function tries to place two nodes immediately adjacent to
  // each other (used for fallthroughs).
  // Returns true if this can be done.
  bool attachNodes(CFGNode *src, CFGNode *sink);

  void mergeChainEdges(NodeChain *splitChain, NodeChain *unsplitChain);

  void mergeInOutEdges(NodeChain * mergerChain, NodeChain * mergeeChain);
  void mergeChains(NodeChain *leftChain, NodeChain *rightChain);
  void mergeChains(std::unique_ptr<NodeChainAssembly> assembly);

  // Recompute the ExtTSP score of a chain
  double computeExtTSPScore(NodeChain *chain) const;

  // Update the related NodeChainAssembly records for two chains, with the
  // assumption that unsplitChain has been merged into splitChain.
  bool updateNodeChainAssembly(NodeChain *splitChain, NodeChain *unsplitChain);

  void mergeAllChains();

  void init();

  // Initialize the mutuallyForcedOut map
  void initMutuallyForcedEdges(ControlFlowGraph &cfg);

  // Initialize basic block chains, with one chain for every node
  void initNodeChains(ControlFlowGraph &cfg);

public:
  // This invokes the Extended TSP algorithm, orders the hot and cold basic
  // blocks and inserts their associated symbols at the corresponding locations
  // specified by the parameters (HotPlaceHolder and ColdPlaceHolder) in the
  // given SymbolList.
  void doOrder(std::unique_ptr<ChainClustering> &CC);


  NodeChainBuilder(std::vector<ControlFlowGraph *>& cfgs): CFGs(cfgs){}

  NodeChainBuilder(ControlFlowGraph *cfg): CFGs(1, cfg){}

  std::string toString(NodeChainAssembly& assembly) const;
};


class NoOrdering : public ChainClustering {
 public:
  void doOrder(std::vector<CFGNode*> &hotOrder,
               std::vector<CFGNode*> &coldOrder);
};

class CallChainClustering: public ChainClustering {
 private:
  Cluster* getMostLikelyPredecessor(NodeChain *chain,
                                            Cluster *cluster);
  void mergeClusters();
};

class PropellerBBReordering {
 private:
  std::vector<ControlFlowGraph *> HotCFGs, ColdCFGs;
  std::vector<CFGNode*> HotOrder, ColdOrder;
  std::unique_ptr<ChainClustering> CC;

 public:
  PropellerBBReordering() {
    prop->forEachCfgRef([this](ControlFlowGraph &cfg){
      if (cfg.isHot()){
        HotCFGs.push_back(&cfg);
        if (propellerConfig.optPrintStats){
          unsigned hotBBs = 0;
          unsigned allBBs = 0;
          cfg.forEachNodeRef([&hotBBs, &allBBs] (CFGNode &node) {
            if (node.Freq)
              hotBBs++;
            allBBs++;
          });
          fprintf(stderr, "HISTOGRAM: %s,%u,%u\n", cfg.Name.str().c_str(), allBBs, hotBBs);
        }
      } else
        ColdCFGs.push_back(&cfg);
    });
  }

  void doSplitOrder(std::list<StringRef> &symbolList,
                      std::list<StringRef>::iterator hotPlaceHolder,
                      std::list<StringRef>::iterator coldPlaceHolder) {

      std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

      if (propellerConfig.optReorderIP)
        CC.reset(new CallChainClustering());
      else if (propellerConfig.optReorderFuncs)
        CC.reset(new CallChainClustering());
      else
        CC.reset(new NoOrdering());

      if (propellerConfig.optReorderIP)
        NodeChainBuilder(HotCFGs).doOrder(CC);
      else if (propellerConfig.optReorderBlocks){
        for(ControlFlowGraph *cfg: HotCFGs)
          NodeChainBuilder(cfg).doOrder(CC);
      } else {
        for(ControlFlowGraph *cfg: HotCFGs)
          CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));
      }
      for(ControlFlowGraph *cfg: ColdCFGs)
        CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));

      CC->doOrder(HotOrder, ColdOrder);

      for(CFGNode *n: HotOrder)
        symbolList.insert(hotPlaceHolder, n->ShName);

      for(CFGNode *n: ColdOrder)
        symbolList.insert(coldPlaceHolder, n->ShName);

      std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
      warn("[Propeller]: BB reordering took: " + Twine(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()));

      if (propellerConfig.optPrintStats)
        printStats();
  }

  void printStats();
};

} // namespace propeller
} // namespace lld

#endif
