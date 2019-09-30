#ifndef LLD_ELF_PROPELLER_BB_REORDERING_H
#define LLD_ELF_PROPELLER_BB_REORDERING_H

#include "PropellerELFCfg.h"

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"

#include <list>
#include <unordered_map>
#include <unordered_set>

using llvm::DenseMap;

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
  const ELFCfgNode *DelegateNode = nullptr;
  std::vector<const ELFCfgNode *> Nodes;

  // Total binary size of the chain
  uint32_t Size = 0;

  // Total execution frequency of the chain
  uint64_t Freq = 0;

  // Extended TSP score of the chain
  double Score = 0;

  // Constructor for building a NodeChain from a single Node
  NodeChain(const ELFCfgNode *Node) {
    DelegateNode = Node;
    Nodes.push_back(Node);
    Size = Node->ShSize;
    Freq = Node->Freq;
  }

  double execDensity() const {
    return ((double)Freq) / std::max(Size, (uint32_t)1);
  }

  const ELFCfgNode *getFirstNode() const { return Nodes.front(); }

  const ELFCfgNode *getLastNode() const { return Nodes.back(); }
};

// BB Chain builder based on the ExtTSP metric
class NodeChainBuilder {
private:
  class NodeChainAssembly;
  class NodeChainSlice;

  // Cfg representing a single function.
  const ELFCfg *CFG;

  // Set of built chains, keyed by section index of their Delegate Nodes.
  DenseMap<uint64_t, unique_ptr<NodeChain>> Chains;

  // Map from every node to its containing chain.
  // This map will be updated as chains keep merging together during the
  // algorithm.
  DenseMap<const ELFCfgNode *, NodeChain *> NodeToChainMap;

  // Map from every node to its (binary) offset in its containing chain.
  // This map will be updated as chains keep merging together during the
  // algorithm.
  DenseMap<const ELFCfgNode *, uint32_t> NodeOffsetMap;

  // These represent all the edges which are -- based on the profile -- the only
  // (executed) outgoing edges from their source node and the only (executed)
  // incoming edges to their sink nodes. The algorithm will make sure that these
  // edges form fall-throughs in the final order.
  DenseMap<const ELFCfgNode *, ELFCfgNode *> MutuallyForcedOut;

  // This maps every (ordered) pair of chains (with the first chain in the pair
  // potentially splittable) to the highest-gain NodeChainAssembly for those chains.
  DenseMap<std::pair<NodeChain *, NodeChain *>,
           std::unique_ptr<NodeChainAssembly>>
      NodeChainAssemblies;

  // This map stores the candidate chains for each chain.
  //
  // For every chain, its candidate chains are the chains which can increase the
  // overall ExtTSP score when merged with that chain. This is used to update
  // the NodeChainAssemblies map whenever chains merge together. The candidate
  // chains of a chain may also be updated as result of a merge.
  std::unordered_map<NodeChain *, std::unordered_set<NodeChain *>>
      CandidateChains;

  void sortChainsByExecutionDensity(std::vector<const NodeChain *> &ChainOrder);

  void initializeExtTSP();
  void attachFallThroughs();

  // This function tries to place two nodes immediately adjacent to
  // each other (used for fallthroughs).
  // Returns true if this can be done.
  bool attachNodes(const ELFCfgNode *src, const ELFCfgNode *sink);

  void mergeChainEdges(NodeChain *splitChain, NodeChain *unSplitChain);

  void mergeChains(NodeChain *leftChain, NodeChain *rightChain);
  void mergeChains(std::unique_ptr<NodeChainAssembly> assembly);

  // Recompute the ExtTSP score of a chain
  double computeExtTSPScore(NodeChain *chain) const;

  // Update the related NodeChainAssembly records for two chains, with the assumption
  // that UnsplitChain has been merged into SplitChain.
  bool updateNodeChainAssembly(NodeChain *splitChain, NodeChain *unsplitChain);

  void computeChainOrder(std::vector<const NodeChain *> &chainOrder);

  // Initialize the mutuallyForcedOut map
  void initMutuallyForcedEdges();

  // Initialize basic block chains, with one chain for every node
  void initNodeChains();

  uint32_t getNodeOffset(const ELFCfgNode *node) const {
    auto it = NodeOffsetMap.find(node);
    assert("Node does not exist in the offset map." && it != NodeOffsetMap.end());
    return it->second;
  }

  NodeChain *getNodeChain(const ELFCfgNode *node) const {
    auto it = NodeToChainMap.find(node);
    assert("Node does not exist in the chain map." && it != NodeToChainMap.end());
    return it->second;
  }

public:
  NodeChainBuilder(const ELFCfg *_CFG) : CFG(_CFG) {
    initNodeChains();
    initMutuallyForcedEdges();
  }

  void doSplitOrder(std::list<StringRef> &SymbolList,
                    std::list<StringRef>::iterator HotPlaceHolder,
                    std::list<StringRef>::iterator ColdPlaceHolder);
};

class NodeChainBuilder::NodeChainSlice {
private:
  // Chain from which this slice comes from
  NodeChain *Chain;

  // The endpoints of the slice in the corresponding chain
  std::vector<const ELFCfgNode *>::iterator Begin, End;

  // The offsets corresponding to the two endpoints
  uint32_t BeginOffset, EndOffset;

  // Constructor for building a chain slice from a given chain and the two
  // endpoints of the chain.
  NodeChainSlice(NodeChain *c,
                 std::vector<const ELFCfgNode *>::iterator begin,
                 std::vector<const ELFCfgNode *>::iterator end,
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

  // Whether the ExtTSP score has been computed for this NodeChainAssembly
  bool ScoreComputed = false;

  // Corresponding NodeChainBuilder of the assembled chains
  const NodeChainBuilder *ChainBuilder;

  // The two assembled chains
  NodeChain *SplitChain, *UnsplitChain;

  // The slice position of Splitchain
  std::vector<const ELFCfgNode *>::iterator SlicePosition;

  // The three chain slices
  std::vector<NodeChainSlice> Slices;

  NodeChainAssembly(NodeChain *chainX, NodeChain *chainY,
                    std::vector<const ELFCfgNode *>::iterator slicePosition,
                    MergeOrder mergeOrder,
                    const NodeChainBuilder *chainBuilder) : ChainBuilder(chainBuilder), SplitChain(chainX), UnsplitChain(chainY), SlicePosition(slicePosition) {
    NodeChainSlice x1(chainX, chainX->Nodes.begin(), SlicePosition, *chainBuilder);
    NodeChainSlice x2(chainX, SlicePosition, chainX->Nodes.end(), *chainBuilder);
    NodeChainSlice y(chainY, chainY->Nodes.begin(), chainY->Nodes.end(), *chainBuilder);

    switch (mergeOrder) {
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
  }

  double extTSPScore() {
    if (ScoreComputed)
      return Score;
    else {
      ScoreComputed = true;
      return (Score = computeExtTSPScore());
    }
  }

  // Return the gain in ExtTSP score achieved by this NodeChainAssembly once it
  // is accordingly applied to the two chains.
  double extTSPScoreGain() {
    return extTSPScore() - SplitChain->Score - UnsplitChain->Score;
  }

  // Find the NodeChainSlice in this NodeChainAssembly which contains the given node. If the node is not
  // contained in this NodeChainAssembly, then return false. Otherwise, set idx equal to the index
  // of the corresponding slice and return true.
  bool findSliceIndex(const ELFCfgNode *node, uint8_t &idx) const {
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
          if (!(*nodeIt)->ShSize)
            break;
          if (*nodeIt == node)
            return true;
        }
      }
      if (offset == Slices[idx].BeginOffset) {
        // If offset is at the beginning of the slice, iterate forwards over the
        // slice to find the node.
        for (auto nodeIt = Slices[idx].Begin; nodeIt != Slices[idx].End; nodeIt++) {
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
  const ELFCfgNode *getFirstNode() const {
    for (auto &slice : Slices)
      if (slice.Begin != slice.End)
        return *slice.Begin;
    return nullptr;
  }

  friend class NodeChainBuilder;

public:
  NodeChainAssembly(NodeChainAssembly &&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(const NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;
};

} // namespace propeller
} // namespace lld
#endif
