//===- PropellerBBReordering.cpp  -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_BB_REORDERING_H
#define LLD_ELF_PROPELLER_BB_REORDERING_H

#include "PropellerCfg.h"

#include "lld/Common/LLVM.h"
#include "Heap.h"
#include "llvm/ADT/DenseMap.h"
#include <iostream>

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

using lld::elf::config;
using llvm::DenseMap;
using llvm::DenseSet;

namespace lld {
namespace propeller {

class ChainClustering;

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
  const CFGNode *DelegateNode;
  std::vector<const CFGNode *> Nodes;
  std::vector<unsigned> FunctionEntryIndices;
  std::unordered_map<NodeChain *, std::list<const CFGEdge*>> OutEdges;
  DenseSet<NodeChain *> InEdges;

  // Total binary size of the chain
  uint32_t Size;

  // Total execution frequency of the chain
  uint64_t Freq;

  // Extended TSP score of the chain
  double Score = 0;

  bool DebugChain;

  // Constructor for building a NodeChain from a single Node
  NodeChain(const CFGNode *node)
      : DelegateNode(node), Nodes(1, node), Size(node->ShSize),
        Freq(node->Freq), DebugChain(node->CFG->DebugCFG) {}

  NodeChain(ControlFlowGraph *cfg) : DelegateNode(cfg->getEntryNode()), Size(cfg->Size), Freq(0), DebugChain(cfg->DebugCFG) {
    cfg->forEachNodeRef([this] (const CFGNode& node) {
      Nodes.push_back(&node);
      Freq += node.Freq;
    });
  }

  template <class Visitor> void forEachOutEdgeToChain(NodeChain* chain, Visitor V) const {
    auto it = OutEdges.find(chain);
    if (it == OutEdges.end())
      return;
    for (const CFGEdge *E : it->second)
      V(*E);
  }

  double execDensity() const {
    return ((double)Freq) / std::max(Size, (uint32_t)1);
  }
};

// BB Chain builder based on the ExtTSP metric
class NodeChainBuilder {
private:
  std::vector<ControlFlowGraph*> CFGs;

  class NodeChainAssembly;
  struct CompareNodeChainAssemblyGain;
  class NodeChainSlice;

  // Cfg representing a single function.
  // const ControlFlowGraph *CFG;

  // Set of built chains, keyed by section index of their Delegate Nodes.
  DenseMap<uint64_t, std::unique_ptr<NodeChain>> Chains;

  // Map from every node to its containing chain.
  // This map will be updated as chains keep merging together during the
  // algorithm.
  DenseMap<const CFGNode *, NodeChain *> NodeToChainMap;

  // Map from every node to its (binary) offset in its containing chain.
  // This map will be updated as chains keep merging together during the
  // algorithm.
  DenseMap<const CFGNode *, uint32_t> NodeOffsetMap;

  // These represent all the edges which are -- based on the profile -- the only
  // (executed) outgoing edges from their source node and the only (executed)
  // incoming edges to their sink nodes. The algorithm will make sure that these
  // edges form fall-throughs in the final order.
  DenseMap<const CFGNode *, CFGNode *> MutuallyForcedOut;

  // This maps every (ordered) pair of chains (with the first chain in the pair
  // potentially splittable) to the highest-gain NodeChainAssembly for those
  // chains.
  //DenseMap<std::pair<NodeChain *, NodeChain *>,
  //         std::unique_ptr<NodeChainAssembly>>
  //    NodeChainAssemblies;
  Heap<std::pair<NodeChain*, NodeChain*>, std::unique_ptr<NodeChainAssembly>, std::less<std::pair<NodeChain*,NodeChain*>> , CompareNodeChainAssemblyGain> NodeChainAssemblies;

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
  void attachFallThroughs();

  // This function tries to place two nodes immediately adjacent to
  // each other (used for fallthroughs).
  // Returns true if this can be done.
  bool attachNodes(const CFGNode *src, const CFGNode *sink);

  void mergeChainEdges(NodeChain *splitChain, NodeChain *unSplitChain);

  void mergeInOutEdges(NodeChain * mergerChain, NodeChain * mergeeChain);
  void mergeChains(NodeChain *leftChain, NodeChain *rightChain);
  void mergeChains(std::unique_ptr<NodeChainAssembly> assembly);

  // Recompute the ExtTSP score of a chain
  double computeExtTSPScore(NodeChain *chain) const;

  // Update the related NodeChainAssembly records for two chains, with the
  // assumption that UnsplitChain has been merged into SplitChain.
  bool updateNodeChainAssembly(NodeChain *splitChain, NodeChain *unsplitChain);

  void mergeAllChains();

  void init();

  // Initialize the mutuallyForcedOut map
  void initMutuallyForcedEdges(const ControlFlowGraph &cfg);

  // Initialize basic block chains, with one chain for every node
  void initNodeChains(const ControlFlowGraph &cfg);

  uint32_t getNodeOffset(const CFGNode *node) const {
    auto it = NodeOffsetMap.find(node);
    assert("Node does not exist in the offset map." &&
           it != NodeOffsetMap.end());
    return it->second;
  }

  NodeChain *getNodeChain(const CFGNode *node) const {
    auto it = NodeToChainMap.find(node);
    assert("Node does not exist in the chain map." &&
           it != NodeToChainMap.end());
    return it->second;
  }

public:
  template <class Visitor> void forEachChainRef(Visitor V) const {
    for (const auto &C: Chains) {
      V(*C.second.get());
    }
  }

  // This invokes the Extended TSP algorithm, orders the hot and cold basic
  // blocks and inserts their associated symbols at the corresponding locations
  // specified by the parameters (HotPlaceHolder and ColdPlaceHolder) in the
  // given SymbolList.
  void doOrder(std::unique_ptr<ChainClustering> &CC);

  NodeChainBuilder(std::vector<ControlFlowGraph *>& cfgs): CFGs(cfgs){}

  NodeChainBuilder(ControlFlowGraph *cfg): CFGs(1, cfg){}

  std::string toString(const NodeChainAssembly& assembly) const;
};

class NodeChainBuilder::NodeChainSlice {
private:
  // Chain from which this slice comes from
  NodeChain *Chain;

  // The endpoints of the slice in the corresponding chain
  std::vector<const CFGNode *>::iterator Begin, End;

  // The offsets corresponding to the two endpoints
  uint32_t BeginOffset, EndOffset;

  // Constructor for building a chain slice from a given chain and the two
  // endpoints of the chain.
  NodeChainSlice(NodeChain *c, std::vector<const CFGNode *>::iterator begin,
                 std::vector<const CFGNode *>::iterator end,
                 const NodeChainBuilder &chainBuilder)
      : Chain(c), Begin(begin), End(end) {

    BeginOffset = chainBuilder.getNodeOffset(*begin);
    if (End == Chain->Nodes.end())
      EndOffset = Chain->Size;
    else
      EndOffset = chainBuilder.getNodeOffset(*end);
  }

  // (Binary) size of this slice
  uint32_t size() const { return EndOffset - BeginOffset; }

  friend class NodeChainBuilder;
  friend class NodeChainAssembly;
};

class NodeChainBuilder::NodeChainAssembly {
private:
  // Total ExtTSP score of this NodeChainAssembly
  double Score = 0;

  // Corresponding NodeChainBuilder of the assembled chains
  const NodeChainBuilder *ChainBuilder;

  // The two assembled chains
  NodeChain *SplitChain, *UnsplitChain;

  // The slice position of Splitchain
  std::vector<const CFGNode *>::iterator SlicePosition;

  // The three chain slices
  std::vector<NodeChainSlice> Slices;

  // The merge order
  MergeOrder MOrder;

  NodeChainAssembly(NodeChain *chainX, NodeChain *chainY,
                    std::vector<const CFGNode *>::iterator slicePosition,
                    MergeOrder mOrder, const NodeChainBuilder *chainBuilder)
      : ChainBuilder(chainBuilder), SplitChain(chainX), UnsplitChain(chainY),
        SlicePosition(slicePosition),  MOrder(mOrder) {
    NodeChainSlice x1(chainX, chainX->Nodes.begin(), SlicePosition,
                      *chainBuilder);
    NodeChainSlice x2(chainX, SlicePosition, chainX->Nodes.end(),
                      *chainBuilder);
    NodeChainSlice y(chainY, chainY->Nodes.begin(), chainY->Nodes.end(),
                     *chainBuilder);

    switch (MOrder) {
    case MergeOrder::X2X1Y:
      Slices = {x2, x1, y};
      break;
    case MergeOrder::X1YX2:
      Slices = {x1, y, x2};
      break;
    case MergeOrder::X2YX1:
      Slices = {x2, y, x1};
      break;
    case MergeOrder::YX2X1:
      Slices = {y, x2, x1};
      break;
    default:
      assert("Invalid MergeOrder!" && false);
    }

    Score = computeExtTSPScore();
  }

  bool isValid() {
    return config->propellerReorderIP || (!SplitChain->Nodes.front()->isEntryNode() && !UnsplitChain->Nodes.front()->isEntryNode()) || getFirstNode()->isEntryNode();
  }

  // Return the gain in ExtTSP score achieved by this NodeChainAssembly once it
  // is accordingly applied to the two chains.
  double extTSPScoreGain() {
    return this->Score - SplitChain->Score - UnsplitChain->Score;
  }

  // Find the NodeChainSlice in this NodeChainAssembly which contains the given
  // node. If the node is not contained in this NodeChainAssembly, then return
  // false. Otherwise, set idx equal to the index of the corresponding slice and
  // return true.
  bool findSliceIndex(const CFGNode *node, uint8_t &idx) const {
    // First find the chain containing the given node.
    NodeChain *chain = ChainBuilder->getNodeChain(node);

    // Return false if the found chain is not used in this NodeChainAssembly.
    if (SplitChain != chain && UnsplitChain != chain)
      return false;

    // Find the slice containing the node using the node's offset in the chain
    auto offset = ChainBuilder->getNodeOffset(node);

    for (idx = 0; idx < 3; ++idx) {
      if (chain != Slices[idx].Chain)
        continue;
      if (offset < Slices[idx].BeginOffset)
        continue;
      if (offset > Slices[idx].EndOffset)
        continue;
      if (offset < Slices[idx].EndOffset && offset > Slices[idx].BeginOffset)
        return true;
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
  const CFGNode *getFirstNode() const {
    for (auto &slice : Slices)
      if (slice.Begin != slice.End)
        return *slice.Begin;
    return nullptr;
  }

  struct CompareNodeChainAssembly {
    bool operator () (const std::unique_ptr<NodeChainAssembly> &a1,
                      const std::unique_ptr<NodeChainAssembly> &a2) const {
      if (a1->extTSPScoreGain() == a2->extTSPScoreGain()){
        if (a1->chainPair() < a2->chainPair())
            return true;
        if (a2->chainPair() < a1->chainPair())
            return false;
        return a1->assemblyStrategy() < a2->assemblyStrategy();
      }
      return a1->extTSPScoreGain() < a2->extTSPScoreGain();
    }
  };


  friend class NodeChainBuilder;

public:
  // We delete the copy constructor to make sure NodeChainAssembly is moved
  // rather than copied.
  NodeChainAssembly(NodeChainAssembly &&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(const NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;

  std::pair<NodeChain*, NodeChain*> chainPair() const {
    return std::make_pair(SplitChain, UnsplitChain);
  }

  std::pair<uint8_t, size_t> assemblyStrategy() const {
    return std::make_pair(MOrder, SlicePosition - SplitChain->Nodes.begin());
  }

  bool split() const {
    return SlicePosition != SplitChain->Nodes.begin();
  }
};

struct NodeChainBuilder::CompareNodeChainAssemblyGain {
  bool operator() (const std::unique_ptr<NodeChainAssembly> &a1,
                   const std::unique_ptr<NodeChainAssembly> &a2) const {
    return a1->extTSPScoreGain() < a2->extTSPScoreGain();
  }
};

class ChainClustering {
 public:
  class Cluster {
    public:
     Cluster(const NodeChain*);
     std::vector<const NodeChain*> Chains;
     const NodeChain * DelegateChain;
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
    for (const NodeChain * c : cluster->Chains) {
      ChainToClusterMap[c] = predecessorCluster;
    }

    // Delete the defunct cluster
    Clusters.erase(cluster->DelegateChain->DelegateNode->MappedAddr);
  }

  void addChain(std::unique_ptr<const NodeChain>&& chain_ptr);

  virtual void doOrder(std::vector<const CFGNode*> &hotOrder,
               std::vector<const CFGNode*> &coldOrder);

  virtual ~ChainClustering() = default;

 protected:
  virtual void mergeClusters() {};
  void sortClusters(std::vector<Cluster *> &);

  void initClusters() {
    for(auto& c_ptr: HotChains){
      const NodeChain * chain = c_ptr.get();
      Cluster *cl = new Cluster(chain);
      cl->Weight = chain->Freq;
      cl->Size = std::max(chain->Size, (uint32_t)1);
      ChainToClusterMap[chain] = cl;
      Clusters.try_emplace(cl->DelegateChain->DelegateNode->MappedAddr, cl);
    }
  }

  std::vector<std::unique_ptr<const NodeChain>> HotChains, ColdChains;
  DenseMap<uint64_t, std::unique_ptr<Cluster>> Clusters;
  DenseMap<const CFGNode *, const NodeChain *> NodeToChainMap;
  DenseMap<const NodeChain*, Cluster*> ChainToClusterMap;
};

class NoOrdering : public ChainClustering {
 public:
  void doOrder(std::vector<const CFGNode*> &hotOrder,
               std::vector<const CFGNode*> &coldOrder);
};

class CallChainClustering: public ChainClustering {
 private:
  Cluster* getMostLikelyPredecessor(const NodeChain *chain,
                                            Cluster *cluster);
  void mergeClusters();
};

class PropellerBBReordering {
 private:
  std::vector<ControlFlowGraph *> HotCFGs, ColdCFGs;
  std::vector<const CFGNode*> HotOrder, ColdOrder;
  std::unique_ptr<ChainClustering> CC;

 public:
  template <class CfgContainerTy>
  PropellerBBReordering(CfgContainerTy &cfgContainer) {
    cfgContainer.forEachCfgRef([this](ControlFlowGraph &cfg){
      if(cfg.isHot()){
        HotCFGs.push_back(&cfg);
        if (config->propellerPrintStats){
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

      if (config->propellerReorderIP)
        CC.reset(new CallChainClustering());
      else if (config->propellerReorderFuncs)
        CC.reset(new CallChainClustering());
      else
        CC.reset(new NoOrdering());

      if (config->propellerReorderIP)
        NodeChainBuilder(HotCFGs).doOrder(CC);
      else if (config->propellerReorderBlocks){
        for(ControlFlowGraph *cfg: HotCFGs)
          NodeChainBuilder(cfg).doOrder(CC);
      } else {
        for(ControlFlowGraph *cfg: HotCFGs)
          CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));
      }
      for(ControlFlowGraph *cfg: ColdCFGs)
        CC->addChain(std::unique_ptr<NodeChain>(new NodeChain(cfg)));

      CC->doOrder(HotOrder, ColdOrder);

      for(const CFGNode *n: HotOrder){
        symbolList.insert(hotPlaceHolder, n->ShName);
        //warn("[PROPELLER] IN HOT ORDER: " + n->ShName);
      }
      for(const CFGNode *n: ColdOrder) {
        symbolList.insert(coldPlaceHolder, n->ShName);
        //warn("[PROPELLER] IN COLD ORDER: " + n->ShName);
      }

      std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
      warn("[Propeller]: BB reordering took: " + Twine(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()));

      if (config->propellerPrintStats){
        printStats();
      }
  }


  void printStats();
};

} // namespace propeller
} // namespace lld

namespace std {

template<>
    struct less<lld::propeller::NodeChain*> {
      bool operator()(const lld::propeller::NodeChain* c1,
                      const lld::propeller::NodeChain* c2) const {
        return c1->DelegateNode->MappedAddr < c2->DelegateNode->MappedAddr;
      }
    };

template<>
    struct less<lld::propeller::ChainClustering::Cluster*> {
      bool operator()(const lld::propeller::ChainClustering::Cluster* c1,
                      const lld::propeller::ChainClustering::Cluster* c2) const {
        return less<lld::propeller::NodeChain*>()(c1->DelegateChain, c2->DelegateChain);
      }
    };

} //namespace std
#endif
