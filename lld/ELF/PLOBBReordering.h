#ifndef LLD_ELF_PLO_BB_REORDERING_H
#define LLD_ELF_PLO_BB_REORDERING_H

#include "PLOELFCfg.h"

#include <list>
#include <set>
#include <unordered_map>
#include <unordered_set>

using std::list;
using std::map;
using std::unordered_map;
using std::unordered_set;
using std::set;
using std::unique_ptr;
using std::vector;

namespace lld{
namespace plo {

/* Represents a chain of ELFCfgNodes (basic blocks). */
class NodeChain {
 public:
  const ELFCfgNode * DelegateNode = nullptr;
  list<const ELFCfgNode *> Nodes;

  // Total binary size of the chain
  uint32_t Size{0};

  // Total execution frequency of the chain
  uint64_t  Freq{0};

  double Score{0};

  // Constructor for building a NodeChain with a single Node.
  NodeChain(const ELFCfgNode * Node){
    DelegateNode = Node;
    Nodes.push_back(Node);
    Size = Node->ShSize;
    Freq = Node->Freq;
  }

  double GetExecDensity() const {
    return ((double)Freq)/Size;
  }

  const ELFCfgNode * GetFirstNode() const {
    return Nodes.front();
  }

  const ELFCfgNode * GetLastNode() const {
    return Nodes.back();
  }

  void Dump() const;
};


/* Base class for incremental chaining of Nodes in a Cfg.*/
class NodeChainBuilder {
 protected:
  /* Cfg representing a function.*/
  const ELFCfg * Cfg;
  /* Set of built chains, keyed by Shndx of their Delegate Nodes.*/
  map<uint64_t, unique_ptr<NodeChain>> Chains;
  unordered_map<const ELFCfgNode *, NodeChain *> NodeToChainMap;
  unordered_map<const ELFCfgNode *, uint32_t> NodeOffset;
  list<const ELFCfgNode *> Layout;

  void SortChainsByExecutionDensity(vector<const NodeChain*>& ChainOrder);
  void AttachFallThroughs();
  void MergeChains(NodeChain* LeftChain, NodeChain* RightChain);

  /* This function tries to place two nodes immediately adjacent to
   * each other (used for fallthroughs).
   * Returns true if this can be done. */
  bool AttachNodes(const ELFCfgNode * Src, const ELFCfgNode * Sink);
  void ComputeChainOrder(vector<const NodeChain*>&);

 private:
  void CreateChainForNode(const ELFCfgNode * Node){
    NodeChain * Chain = new NodeChain(Node);
    NodeToChainMap[Node] = Chain;
    NodeOffset[Node] = 0;
    Chains.emplace(Node->Shndx, Chain);
  }

 public:
  uint32_t getNodeOffset(const ELFCfgNode* N) const {
    return NodeOffset.at(N);
  }

  NodeChainBuilder(const ELFCfg * _Cfg): Cfg(_Cfg){
    for(auto& Node: Cfg->Nodes){
      CreateChainForNode(Node.get());
    }
  }

  void doSplitOrder(list<StringRef>& SymbolList,
               list<StringRef>::iterator HotPlaceHolder,
               list<StringRef>::iterator ColdPlaceHolder);

};


/* Chain builder based on ExtTSP metric */
class ExtTSPChainBuilder : public NodeChainBuilder{
 private:
  unordered_map<const ELFCfgNode *, ELFCfgNode *> MutuallyForcedOut;

  class NodeChainAssembly;
  class NodeChainSlice;

  map<std::pair<NodeChain*, NodeChain *>, NodeChainAssembly> NodeChainAssemblies;
  unordered_map<NodeChain*, unordered_set<NodeChain *>> AdjacentChains;

  void MergeChainEdges(NodeChain *SplitChain, NodeChain *UnsplitChain);
  void MergeChains(NodeChainAssembly& A);

  double ExtTSPScore(NodeChain * Chain);
  bool UpdateChainEdge(NodeChain * SplitChain, NodeChain * UnsplitChain);
  void ComputeChainOrder(vector<const NodeChain*>&);

 public:
  ExtTSPChainBuilder(const ELFCfg * _Cfg);
};

class ExtTSPChainBuilder::NodeChainSlice{
 private:
  NodeChain * Chain;
  list<const ELFCfgNode *>::iterator Begin, End;
  uint32_t BeginOffset, EndOffset;

  NodeChainSlice(NodeChain * C,
                 list<const ELFCfgNode*>::iterator _Begin,
                 list<const ELFCfgNode*>::iterator _End,
                 const NodeChainBuilder& ChainBuilder):
    Chain(C), Begin(_Begin), End(_End) {
    BeginOffset = ChainBuilder.getNodeOffset(*Begin);
    if(End==Chain->Nodes.end())
      EndOffset = Chain->Size;
    else
      EndOffset = ChainBuilder.getNodeOffset(*End);
    assert(EndOffset >= BeginOffset);
  }

  uint32_t Size() const{
    return EndOffset - BeginOffset;
  }

  friend class ExtTSPChainBuilder;
  friend class NodeChainAssembly;
};

class ExtTSPChainBuilder::NodeChainAssembly{
 private:
  double Score;
  bool ScoreComputed{false};
  const ExtTSPChainBuilder * ChainBuilder;
  NodeChain *SplitChain, *UnsplitChain;
  uint8_t MergeOrder;
  list<const ELFCfgNode*>::iterator SlicePos;

  vector<NodeChainSlice> Slices;

  NodeChainAssembly(NodeChain * ChainX, NodeChain * ChainY, list<const ELFCfgNode*>::iterator _SlicePos, uint8_t _MergeOrder, const ExtTSPChainBuilder* _ChainBuilder) {
    SplitChain = ChainX;
    UnsplitChain = ChainY;
    SlicePos = _SlicePos;
    MergeOrder = _MergeOrder;
    ChainBuilder = _ChainBuilder;
    NodeChainSlice X2(ChainX, SlicePos, ChainX->Nodes.end(), *ChainBuilder);
    NodeChainSlice Y(ChainY, ChainY->Nodes.begin(), ChainY->Nodes.end(), *ChainBuilder);
    NodeChainSlice X1(ChainX, ChainX->Nodes.begin(), SlicePos, *ChainBuilder);

    switch(MergeOrder){
      case 0:
        Slices = {X2, X1, Y};
        break;
      case 1:
        Slices = {X1, Y, X2};
        break;
      case 2:
        Slices = {X2, Y, X1};
        break;
      case 3:
        Slices = {Y, X2, X1};
        break;
      default:
        assert(false);
    }
  }


  double GetExtTSPScore() {
    if(ScoreComputed)
      return Score;
    else{
      ScoreComputed = true;
      return (Score = ExtTSPScore());
    }
  }

  double GetExtTSPGain(){
    return GetExtTSPScore() - SplitChain->Score - UnsplitChain->Score;
  }



  bool FindSliceIndex(const ELFCfgNode * Node, uint8_t& SliceIdx) const {
    NodeChain * Chain = ChainBuilder->NodeToChainMap.at(Node);
    if(SplitChain!= Chain && UnsplitChain!=Chain)
      return false;
    auto Offset = ChainBuilder->NodeOffset.at(Node);
    for(uint8_t Idx = 0; Idx < 3; ++Idx){
      if(Chain == Slices[Idx].Chain){
        if(Offset >= Slices[Idx].BeginOffset && Offset < Slices[Idx].EndOffset){
          SliceIdx = Idx;
          return true;
        }
      }
    }
    return false;
  }

  double ExtTSPScore() const;

  const ELFCfgNode * GetFirstNode() const {
    for(auto& Slice: Slices)
      if(Slice.Begin!=Slice.End)
        return *Slice.Begin;
    return nullptr;
  }

  void Dump() const;
  friend class ExtTSPChainBuilder;

 public:
  NodeChainAssembly(NodeChainAssembly&&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(const NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;



};

}
}
#endif
