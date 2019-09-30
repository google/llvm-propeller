#ifndef LLD_ELF_PROPELLER_BB_REORDERING_H
#define LLD_ELF_PROPELLER_BB_REORDERING_H

#include "PropellerELFCfg.h"

#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

using std::list;
using std::map;
using std::ostream;
using std::set;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;
using std::vector;

using llvm::StringMap;

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

/* Represents a chain of ELFCfgNodes (basic blocks). */
class NodeChain {
public:
  const ELFCFGNode *DelegateNode = nullptr;
  list<const ELFCFGNode *> Nodes;

  // Total binary size of the chain
  uint32_t Size{0};

  // Total execution frequency of the chain
  uint64_t Freq{0};

  double Score{0};

  // Constructor for building a NodeChain with a single Node.
  NodeChain(const ELFCFGNode *Node) {
    DelegateNode = Node;
    Nodes.push_back(Node);
    Size = Node->ShSize;
    Freq = Node->Freq;
  }

  void Dump() const;

  double GetExecDensity() const { return ((double)Freq) / std::max(Size, (uint32_t)1); }

  const ELFCFGNode *GetFirstNode() const { return Nodes.front(); }

  const ELFCFGNode *GetLastNode() const { return Nodes.back(); }
};


/* Chain builder based on ExtTSP metric */
class NodeChainBuilder {
private:
  /* CFG representing a function.*/
  const ELFCFG *CFG;
  /* Set of built chains, keyed by section index of their Delegate Nodes.*/
  map<uint64_t, unique_ptr<NodeChain>> Chains;
  unordered_map<const ELFCFGNode *, NodeChain *> NodeToChainMap;
  unordered_map<const ELFCFGNode *, uint32_t> NodeOffset;

  unordered_map<const ELFCFGNode *, ELFCFGNode *> MutuallyForcedOut;

  class NodeChainAssembly;
  class NodeChainSlice;

  map<std::pair<NodeChain *, NodeChain *>, std::unique_ptr<NodeChainAssembly>>
      NodeChainAssemblies;
  unordered_map<NodeChain *, unordered_set<NodeChain *>> CandidateChains;

  void SortChainsByExecutionDensity(vector<const NodeChain *> &ChainOrder);
  void InitializeExtTSP();
  void AttachFallThroughs();

  /* This function tries to place two nodes immediately adjacent to
   * each other (used for fallthroughs).
   * Returns true if this can be done. */
  bool AttachNodes(const ELFCFGNode *Src, const ELFCFGNode *Sink);

  void MergeChainEdges(NodeChain *SplitChain, NodeChain *UnsplitChain);

  void MergeChains(NodeChain *LeftChain, NodeChain *RightChain);
  void MergeChains(std::unique_ptr<NodeChainAssembly> A);

  double ExtTSPScore(NodeChain *Chain) const;
  bool UpdateNodeChainAssembly(NodeChain *SplitChain, NodeChain *UnsplitChain);
  void ComputeChainOrder(vector<const NodeChain *> &);

  void initMutuallyForcedEdges();

  void initNodeChains();

public:
  virtual ~NodeChainBuilder() = default;
  NodeChainBuilder(NodeChainBuilder &) = delete;

  uint32_t getNodeOffset(const ELFCFGNode *N) const { return NodeOffset.at(N); }

  NodeChainBuilder(const ELFCFG *_Cfg) : CFG(_Cfg) {
    initNodeChains();
    initMutuallyForcedEdges();
  }

  void doSplitOrder(list<StringRef> &SymbolList,
                    list<StringRef>::iterator HotPlaceHolder,
                    list<StringRef>::iterator ColdPlaceHolder);
};

class NodeChainBuilder::NodeChainSlice {
private:
  NodeChain *Chain;
  list<const ELFCFGNode *>::iterator Begin, End;
  uint32_t BeginOffset, EndOffset;

  NodeChainSlice(NodeChain *C, list<const ELFCFGNode *>::iterator _Begin,
                 list<const ELFCFGNode *>::iterator _End,
                 const NodeChainBuilder &ChainBuilder)
      : Chain(C), Begin(_Begin), End(_End) {

    BeginOffset = ChainBuilder.getNodeOffset(*Begin);
    if (End == Chain->Nodes.end())
      EndOffset = Chain->Size;
    else
      EndOffset = ChainBuilder.getNodeOffset(*End);
    assert(EndOffset >= BeginOffset);
  }

  uint32_t Size() const { return EndOffset - BeginOffset; }

  friend class NodeChainBuilder;
  friend class NodeChainAssembly;
};

class NodeChainBuilder::NodeChainAssembly {
private:
  double Score;
  bool ScoreComputed{false};
  const NodeChainBuilder *ChainBuilder;
  NodeChain *SplitChain, *UnsplitChain;
  MergeOrder MOrder;
  list<const ELFCFGNode *>::iterator SlicePos;

  vector<NodeChainSlice> Slices;

  NodeChainAssembly(NodeChain *ChainX, NodeChain *ChainY,
                    list<const ELFCFGNode *>::iterator _SlicePos,
                    MergeOrder _MOrder,
                    const NodeChainBuilder *_ChainBuilder) {
    SplitChain = ChainX;
    UnsplitChain = ChainY;
    SlicePos = _SlicePos;
    MOrder = _MOrder;
    ChainBuilder = _ChainBuilder;
    NodeChainSlice X1(ChainX, ChainX->Nodes.begin(), SlicePos, *ChainBuilder);
    NodeChainSlice X2(ChainX, SlicePos, ChainX->Nodes.end(), *ChainBuilder);
    NodeChainSlice Y(ChainY, ChainY->Nodes.begin(), ChainY->Nodes.end(),
                     *ChainBuilder);

    switch (MOrder) {
    case MergeOrder::X2X1Y:
      Slices = {X2, X1, Y};
      break;
    case MergeOrder::X1YX2:
      Slices = {X1, Y, X2};
      break;
    case MergeOrder::X2YX1:
      Slices = {X2, Y, X1};
      break;
    case MergeOrder::YX2X1:
      Slices = {Y, X2, X1};
      break;
    default:
      assert("Invalid MergeOrder!" && false);
    }
  }

  double GetExtTSPScore() {
    if (ScoreComputed)
      return Score;
    else {
      ScoreComputed = true;
      return (Score = ExtTSPScore());
    }
  }

  double GetExtTSPGain() {
    return GetExtTSPScore() - SplitChain->Score - UnsplitChain->Score;
  }

  bool FindSliceIndex(const ELFCFGNode *Node, uint8_t &Idx) const {
    NodeChain *Chain = ChainBuilder->NodeToChainMap.at(Node);
    if (SplitChain != Chain && UnsplitChain != Chain)
      return false;
    auto Offset = ChainBuilder->NodeOffset.at(Node);
    for (Idx = 0; Idx < 3; ++Idx) {
      if (Chain != Slices[Idx].Chain)
        continue;
      if (Offset < Slices[Idx].BeginOffset)
        continue;
      if (Offset > Slices[Idx].EndOffset)
        continue;
      if (Offset < Slices[Idx].EndOffset && Offset > Slices[Idx].BeginOffset)
        return true;
      if (Offset == Slices[Idx].EndOffset) {
        for(auto NI = std::prev(Slices[Idx].End);
            NI != std::prev(Slices[Idx].Begin) && !(*NI)->ShSize ;
            NI--){
          if(*NI == Node)
            return true;
        }
      }
      if (Offset == Slices[Idx].BeginOffset) {
        for(auto NI = Slices[Idx].Begin;
            NI != Slices[Idx].End;
            NI++){
          if(*NI == Node)
            return true;
          if((*NI)->ShSize)
            break;
        }
      }
    }
    return false;
  }

  double ExtTSPScore() const;

  const ELFCFGNode *GetFirstNode() const {
    for (auto &Slice : Slices)
      if (Slice.Begin != Slice.End)
        return *Slice.Begin;
    return nullptr;
  }

  friend class NodeChainBuilder;

public:
  NodeChainAssembly(NodeChainAssembly &&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(const NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;
};


ostream & operator << (ostream &Out, const NodeChain &Chain);

} // namespace propeller
} // namespace lld
#endif
