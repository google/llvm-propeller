#include "PLOBBReordering.h"

#include "llvm/Support/CommandLine.h"
#include <deque>
#include <stdio.h>

using std::deque;
using std::list;
using std::set;
using std::unique_ptr;
using namespace llvm;

namespace opts{

static cl::opt<bool> SeparateHotCold("separate-hot-cold",
                                     cl::desc("Separate the hot and cold basic blocks."),
                                     cl::init(true),
                                     cl::ZeroOrMore);

static cl::opt<bool> FunctionEntryFirst("func-entry-first",
                                        cl::desc("Force function entry to appear first in the ordering."),
                                        cl::init(true),
                                        cl::ZeroOrMore);

static cl::opt<double> FallthroughWeight("fallthrough-weight",
                                         cl::desc("Fallthrough weight for ExtTSP metric calculation."),
                                         cl::init(1.0),
                                         cl::ZeroOrMore);

static cl::opt<double> ForwardWeight("forward-weight",
                                         cl::desc("Forward branch weight for ExtTSP metric calculation."),
                                         cl::init(0.1),
                                         cl::ZeroOrMore);

static cl::opt<double> BackwardWeight("backward-weight",
                                         cl::desc("Backward branch weight for ExtTSP metric calculation."),
                                         cl::init(0.1),
                                         cl::ZeroOrMore);

static cl::opt<uint32_t> ForwardDistance("forward-distance",
                                       cl::desc("Forward branch distance threshold for ExtTSP metric calculation."),
                                       cl::init(1024),
                                       cl::ZeroOrMore);

static cl::opt<uint32_t> BackwardDistance("backward-distance",
                                       cl::desc("Backward branch distance threshold for ExtTSP metric calculation."),
                                       cl::init(640),
                                       cl::ZeroOrMore);

static cl::opt<uint32_t> ChainSplitThreshold("chain-split-threshold",
                                       cl::desc("Maximum binary size of a code chain that can be split."),
                                       cl::init(128),
                                       cl::ZeroOrMore);
}

namespace lld{
namespace plo {

double GetEdgeExtTSPScore(uint64_t EdgeWeight, bool IsEdgeForward, uint32_t SrcSinkDistance){
  if (SrcSinkDistance == 0)
    return EdgeWeight * opts::FallthroughWeight;

  if (IsEdgeForward && SrcSinkDistance < opts::ForwardDistance)
    return EdgeWeight * opts::ForwardWeight * (1.0 - ((double)SrcSinkDistance)/opts::ForwardDistance);

  if (!IsEdgeForward && SrcSinkDistance < opts::BackwardDistance)
    return EdgeWeight * opts::BackwardWeight * (1.0 - ((double)SrcSinkDistance)/opts::BackwardDistance);
  return 0;
}

void NodeChain::Dump() const {
  fprintf(stderr, "Chain for Cfg Node: {%s}: total binary size: %u, total "
          "frequency: %lu:\n",
          DelegateNode->ShName.str().c_str(), Size, Freq);

  for(const ELFCfgNode * N: Nodes)
    fprintf(stderr, "[%s] ", N->ShName.str().c_str());
  fprintf(stderr, "\n");
}

void NodeChainBuilder::SortChainsByExecutionDensity(vector<const NodeChain*>& ChainOrder){
  for(auto CI=Chains.cbegin(), CE=Chains.cend(); CI!=CE; ++CI){
    ChainOrder.push_back(CI->second.get());
  }

  std::sort(ChainOrder.begin(), ChainOrder.end(),
            [this](const NodeChain* C1, const NodeChain* C2) {
              if (opts::FunctionEntryFirst){
                if (C1->GetFirstNode() == this->Cfg->getEntryNode())
                  return true;
                if (C2->GetFirstNode() == this->Cfg->getEntryNode())
                  return false;
              }
              double C1ExecDensity = C1->GetExecDensity();
              double C2ExecDensity = C2->GetExecDensity();
              if(C1ExecDensity == C2ExecDensity){
                if (C1->DelegateNode->MappedAddr == C2->DelegateNode->MappedAddr)
                  return C1->DelegateNode->Shndx < C2->DelegateNode->Shndx;
                return C1->DelegateNode->MappedAddr < C2->DelegateNode->MappedAddr;
              }
              return C1ExecDensity > C2ExecDensity;
            });
}

void NodeChainBuilder::AttachFallThroughs(){
  for(auto& Node: Cfg->Nodes){
    if(Node->FTEdge!=nullptr){
      AttachNodes(Node.get(), Node->FTEdge->Sink);
    }
  }

  for(auto& Node: Cfg->Nodes){
    for(const ELFCfgEdge* E: Node->Outs)
      AttachNodes(E->Src, E->Sink);
  }
}

/* Merge two chains in the specified order. */
void NodeChainBuilder::MergeChains(NodeChain* LeftChain, NodeChain* RightChain){
  for(const ELFCfgNode* Node: RightChain->Nodes){
    LeftChain->Nodes.push_back(Node);
    NodeToChainMap[Node]=LeftChain;
    NodeOffset[Node] += LeftChain->Size;
  }
  LeftChain->Size += RightChain->Size;
  LeftChain->Freq += RightChain->Freq;
  Chains.erase(RightChain->DelegateNode->Shndx);
}

/* This function tries to place two nodes immediately adjacent to
 * each other (used for fallthroughs).
 * Returns true if this can be done. */
bool NodeChainBuilder::AttachNodes(const ELFCfgNode * Src, const ELFCfgNode * Sink){
  if(opts::FunctionEntryFirst && Sink == Cfg->getEntryNode())
    return false;
  if(opts::SeparateHotCold && (Src->Freq==0 ^ Sink->Freq==0))
    return false;
  NodeChain * SrcChain = NodeToChainMap.at(Src);
  NodeChain * SinkChain = NodeToChainMap.at(Sink);
  if(SrcChain == SinkChain)
    return false;
  if(SrcChain->GetLastNode()!=Src || SinkChain->GetFirstNode()!=Sink)
    return false;
  MergeChains(SrcChain, SinkChain);
  return true;
}

void NodeChainBuilder::ComputeChainOrder(vector<const NodeChain*>& ChainOrder){
  deque<const ELFCfgEdge*> CfgEdgeQ;

  for(auto& Edge: Cfg->IntraEdges)
    CfgEdgeQ.push_back(Edge.get());

  std::sort(CfgEdgeQ.begin(), CfgEdgeQ.end(),
            [](const ELFCfgEdge * Edge1, const ELFCfgEdge * Edge2){
              if (Edge1->Weight == Edge2->Weight){
                if (Edge1->Src->MappedAddr == Edge2->Src->MappedAddr)
                  return Edge1->Sink->MappedAddr > Edge2->Sink->MappedAddr;
                return Edge1->Src->MappedAddr > Edge2->Src->MappedAddr;
              }
              return Edge1->Weight < Edge2->Weight;
            });
  while(!CfgEdgeQ.empty()){
    auto * Edge = CfgEdgeQ.back();
    CfgEdgeQ.pop_back();
    AttachNodes(Edge->Src, Edge->Sink);
  }
  SortChainsByExecutionDensity(ChainOrder);
}



void ExtTSPChainBuilder::MergeChains(NodeChainAssembly& A){
  /* Merge the Node sequences according to a given NodeChainAssembly.*/
  for(NodeChainSlice& Slice: A.Slices)
    A.SplitChain->Nodes.splice(A.SplitChain->Nodes.end(), Slice.Chain->Nodes, Slice.Begin, Slice.End);

  /* Update NodeOffset and NodeToChainMap for all the nodes in the sequence */
  uint32_t RunningOffset = 0;
  for(const ELFCfgNode * Node: A.SplitChain->Nodes){
    NodeToChainMap[Node]=A.SplitChain;
    NodeOffset[Node] = RunningOffset;
    RunningOffset += Node->ShSize;
  }

  /* Update the total binary size, frequency, and TSP score of the merged chain */
  A.SplitChain->Size += A.UnsplitChain->Size;
  A.SplitChain->Freq += A.UnsplitChain->Freq;
  A.SplitChain->Score = A.GetExtTSPScore();


  /* Merge the assembly candidate chains of the two chains and remove the records
   * for the defunct NodeChain. */
  for(NodeChain * C: CandidateChains[A.UnsplitChain]){
    NodeChainAssemblies.erase(std::make_pair(C, A.UnsplitChain));
    NodeChainAssemblies.erase(std::make_pair(A.UnsplitChain,C));
    CandidateChains[C].erase(A.UnsplitChain);
    if(C!=A.SplitChain)
      CandidateChains[A.SplitChain].insert(C);
  }

  /* Update the NodeChainAssembly for all candidate chains of the merged
   * NodeChain. Remove a NodeChain from the merge chain's candidates if the
   * NodeChainAssembly update finds no gain.*/
  auto& SplitChainCandidateChains = CandidateChains[A.SplitChain];

  for(auto CI=SplitChainCandidateChains.begin(), CE=SplitChainCandidateChains.end(); CI!=CE;){
    NodeChain * OtherChain = *CI;
    auto& OtherChainCandidateChains=CandidateChains[OtherChain];
    bool OS = UpdateNodeChainAssembly(OtherChain, A.SplitChain);
    bool SO = UpdateNodeChainAssembly(A.SplitChain, OtherChain);
    if(OS || SO){
      OtherChainCandidateChains.insert(A.SplitChain);
      CI++;
    }else{
      OtherChainCandidateChains.erase(A.SplitChain);
      CI = SplitChainCandidateChains.erase(CI);
    }
  }

  /* Remove all the candidate chain records for the merged-in chain.*/
  CandidateChains.erase(A.UnsplitChain);

  /* Finally, remove the merged-in chain record from the Chains.*/
  Chains.erase(A.UnsplitChain->DelegateNode->Shndx);
}

/* Calculate the Extended TSP metric for a NodeChain */
double ExtTSPChainBuilder::ExtTSPScore(NodeChain * Chain) const {
  double Score = 0;
  uint32_t SrcOffset = 0;
  for(const ELFCfgNode * Node: Chain->Nodes){
    for(const ELFCfgEdge * Edge: Node->Outs){
      if(Edge->Weight==0)
        continue;
      NodeChain * SinkChain = NodeToChainMap.at(Edge->Sink);
      if(SinkChain!=Chain)
        continue;
      auto SinkOffset = NodeOffset.at(Edge->Sink);
      bool EdgeForward = SrcOffset < SinkOffset;
      uint32_t Distance = EdgeForward ? SinkOffset - SrcOffset - Node->ShSize : SrcOffset - SinkOffset + Node->ShSize;
      Score += GetEdgeExtTSPScore(Edge->Weight, EdgeForward, Distance);
    }
    SrcOffset += Node->ShSize;
  }
  return Score;
}

/* Updates the best NodeChainAssembly between two NodeChains. The existing
 * record will be replaced by the new NodeChainAssembly if a non-zero gain
 * is achieved. Otherwise, it will be removed. */
bool ExtTSPChainBuilder::UpdateNodeChainAssembly(NodeChain * SplitChain, NodeChain * UnsplitChain){
  /* Only split the chain if the size of the chain is smaller than a treshold.*/
  bool DoSplit = (SplitChain->Size <= opts::ChainSplitThreshold);
  auto SlicePosEnd = DoSplit ? SplitChain->Nodes.end() : std::next(SplitChain->Nodes.begin());

  list<NodeChainAssembly> CandidateNCAs;

  for(auto SlicePos = SplitChain->Nodes.begin(); SlicePos!=SlicePosEnd; ++SlicePos){
    /* Do not split the mutually-forced edges in the chain.*/
    if(SlicePos!=SplitChain->Nodes.begin() && MutuallyForcedOut[*std::prev(SlicePos)]==*SlicePos)
        continue;

    /* If the split position is at the beginning (no splitting), only consider
     * one MergeOrder.*/
    auto MergeOrderEnd = (SlicePos==SplitChain->Nodes.begin()) ? MergeOrder::BeginNext : MergeOrder::End;
    for(uint8_t MI=MergeOrder::Begin; MI!=MergeOrderEnd; MI++){
      MergeOrder MOrder = static_cast<MergeOrder>(MI);
      NodeChainAssembly NCA(SplitChain, UnsplitChain, SlicePos, MOrder, this);
      ELFCfgNode * EntryNode = Cfg->getEntryNode();
      if(opts::FunctionEntryFirst &&
         (NCA.SplitChain->GetFirstNode()==EntryNode || NCA.UnsplitChain->GetFirstNode()==EntryNode) &&
         (NCA.GetFirstNode() != EntryNode))
        continue;
      CandidateNCAs.push_back(std::move(NCA));
    }
  }

  auto BestCandidate = std::max_element(CandidateNCAs.begin(), CandidateNCAs.end(),
                                          [](NodeChainAssembly& C1,
                                             NodeChainAssembly& C2){
                                             return C1.GetExtTSPGain() < C2.GetExtTSPGain();});

  NodeChainAssemblies.erase(std::make_pair(SplitChain, UnsplitChain));

  if(BestCandidate!=CandidateNCAs.end() && BestCandidate->GetExtTSPGain() > 0){
    NodeChainAssemblies.emplace(std::make_pair(SplitChain,UnsplitChain), std::move(*BestCandidate));
    return true;
  }else
    return false;
}

ExtTSPChainBuilder::ExtTSPChainBuilder(const ELFCfg * _Cfg): NodeChainBuilder(_Cfg) {
  unordered_map<const ELFCfgNode *, vector<ELFCfgEdge *>> ProfiledOuts;
  unordered_map<const ELFCfgNode *, vector<ELFCfgEdge *>> ProfiledIns;

  for(auto& Node: Cfg->Nodes){
    std::copy_if(Node->Outs.begin(), Node->Outs.end(),
                 std::back_inserter(ProfiledOuts[Node.get()]),
                 [](const ELFCfgEdge* Edge) {return Edge->Weight!=0;});
    std::copy_if(Node->Ins.begin(), Node->Ins.end(),
                 std::back_inserter(ProfiledIns[Node.get()]),
                 [](const ELFCfgEdge* Edge) {return Edge->Weight!=0;});
  }


  for(auto& Node : Cfg->Nodes){
    if(ProfiledOuts[Node.get()].size()==1){
      ELFCfgEdge * Edge = ProfiledOuts[Node.get()].front();
      if(ProfiledIns[Edge->Sink].size()==1)
        MutuallyForcedOut[Node.get()] = Edge->Sink;
    }
  }

  /* Break cycles in the mutually forced edges by cutting the minimum weight
   * edge in every cycle. */
  map<const ELFCfgNode *, unsigned> NodeToCycleMap;
  set<const ELFCfgNode *> CycleCutNodes;
  unsigned CycleCount = 0;
  for (auto It = MutuallyForcedOut.begin(); It!=MutuallyForcedOut.end(); ++It){
    if(NodeToCycleMap[It->first])
      continue;
    uint64_t MinEdgeWeight = 0;
    const ELFCfgNode * MinEdgeSrc = nullptr;
    auto NodeIt = It;
    CycleCount++;
    while(NodeIt!=MutuallyForcedOut.end()){
      const ELFCfgNode * Node = NodeIt->first;
      auto NodeCycle = NodeToCycleMap[Node];
      if(NodeCycle!=0){
        if(NodeCycle==CycleCount){ /*found a cycle */
          CycleCutNodes.insert(MinEdgeSrc);
        }
        break;
      }else
        NodeToCycleMap[Node] = CycleCount;
      const ELFCfgEdge * Edge = ProfiledOuts[Node].front();
      if(MinEdgeSrc==nullptr || (Edge->Weight < MinEdgeWeight)){
        MinEdgeWeight = Edge->Weight;
        MinEdgeSrc = Node;
      }
      NodeIt = MutuallyForcedOut.find(NodeIt->second);
    }
  }

  for(const ELFCfgNode * Node: CycleCutNodes)
    MutuallyForcedOut.erase(Node);
}

void ExtTSPChainBuilder::ComputeChainOrder(vector<const NodeChain*>& ChainOrder){
  for(auto& KV: MutuallyForcedOut)
    AttachNodes(KV.first, KV.second);

  for(auto& C: Chains){
    NodeChain * Chain = C.second.get();
    Chain->Score = ExtTSPScore(Chain);
    for(const ELFCfgNode * Node: Chain->Nodes){
      for(const ELFCfgEdge * Edge: Node->Outs){
        if(Edge->Weight!=0){
          NodeChain * OtherChain = NodeToChainMap.at(Edge->Sink);
          if(Chain!=OtherChain){
            bool CO = UpdateNodeChainAssembly(Chain, OtherChain);
            bool OC = UpdateNodeChainAssembly(OtherChain, Chain);
            if(CO || OC){
              CandidateChains[Chain].insert(OtherChain);
              CandidateChains[OtherChain].insert(Chain);
            }
          }
        }
      }
    }
  }

  bool Merged;
  do{
    Merged = false;
    auto BestCandidate = std::max_element(NodeChainAssemblies.begin(), NodeChainAssemblies.end(),
                                          [](std::pair<const std::pair<NodeChain*,NodeChain*>, NodeChainAssembly>& C1,
                                             std::pair<const std::pair<NodeChain*,NodeChain*>, NodeChainAssembly>& C2){
                                            return C1.second.GetExtTSPGain() < C2.second.GetExtTSPGain();
                                          });

    if(BestCandidate!=NodeChainAssemblies.end() && BestCandidate->second.GetExtTSPGain() > 0){
      MergeChains(BestCandidate->second);
      Merged = true;
    }
  } while(Merged);

  AttachFallThroughs();
  SortChainsByExecutionDensity(ChainOrder);
}

double ExtTSPChainBuilder::NodeChainAssembly::ExtTSPScore() const {
  double Score = 0;
  for(uint8_t SrcSliceIdx = 0; SrcSliceIdx < 3; ++SrcSliceIdx){
    const NodeChainSlice& SrcSlice = Slices[SrcSliceIdx];
    uint32_t SrcNodeOffset = SrcSlice.BeginOffset;
    for(auto NodeIt = SrcSlice.Begin; NodeIt!=SrcSlice.End; SrcNodeOffset+=(*NodeIt)->ShSize, ++NodeIt){
      const ELFCfgNode * Node = *NodeIt;
      for(const ELFCfgEdge * Edge: Node->Outs){
        if(Edge->Weight==0)
          continue;

        uint8_t SinkSliceIdx;

        if(FindSliceIndex(Edge->Sink, SinkSliceIdx)){
          auto SinkNodeOffset = ChainBuilder->NodeOffset.at(Edge->Sink);
          bool EdgeForward = (SrcSliceIdx < SinkSliceIdx) ||
              (SrcSliceIdx==SinkSliceIdx && SrcNodeOffset < SinkNodeOffset);

          uint32_t Distance = 0;

          if(SrcSliceIdx==SinkSliceIdx) {
            Distance = EdgeForward ?
                (SinkNodeOffset - SrcNodeOffset - Node->ShSize) :
                (SrcNodeOffset - SinkNodeOffset + Node->ShSize);
          } else {
            const NodeChainSlice& SinkSlice = Slices[SinkSliceIdx];
            Distance = EdgeForward ?
                (SrcSlice.EndOffset - SrcNodeOffset - Node->ShSize + SinkNodeOffset - SinkSlice.BeginOffset) :
                (SrcNodeOffset - SrcSlice.BeginOffset + Node->ShSize + SinkSlice.EndOffset - SinkNodeOffset);
            if(std::abs(SinkSliceIdx - SrcSliceIdx) == 2)
              Distance += Slices[1].Size();
          }
          Score += GetEdgeExtTSPScore(Edge->Weight, EdgeForward, Distance);
        }
      }
    }
  }
  return Score;
}

void NodeChainBuilder::doSplitOrder(list<StringRef> &SymbolList,
                                 list<StringRef>::iterator HotPlaceHolder,
                                 list<StringRef>::iterator ColdPlaceHolder){

  vector<const NodeChain*> ChainOrder;
  ComputeChainOrder(ChainOrder);

  for(const NodeChain * C: ChainOrder){
    list<StringRef>::iterator InsertPos = (C->Freq)? HotPlaceHolder : ColdPlaceHolder;
    for(const ELFCfgNode *N : C->Nodes)
      SymbolList.insert(InsertPos, N->ShName);
  }
}

}
}
