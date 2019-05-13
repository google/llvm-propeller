#include "PLOBBReordering.h"

#include "llvm/Support/CommandLine.h"
#include <deque>
#include <iostream>

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

void NodeChain::Dump() const {
  std::cerr << "Total size: " << Size << "\n";
  for(const ELFCfgNode * N: Nodes){
    std::cerr << N->ShName.str() << "[" << N->ShSize << "] ";
  }
  std::cerr << "\n";
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
}

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
  NodeChain * SrcChain = NodeToChainMap[Src];
  NodeChain * SinkChain = NodeToChainMap[Sink];
  if(SrcChain == SinkChain)
    return false;
  if(SrcChain->GetLastNode()!=Src || SinkChain->GetFirstNode()!=Sink)
    return false;
  MergeChains(SrcChain, SinkChain);
  return true;
}

void NodeChainBuilder::ComputeChainOrder(vector<const NodeChain*>& ChainOrder){
  std::deque<const ELFCfgEdge*> CfgEdgeQ;

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


void ExtTSPChainBuilder::MergeChainEdges(NodeChain *SplitChain, NodeChain *UnsplitChain){
  for(NodeChain * C: AdjacentChains[UnsplitChain]){
    NodeChainAssemblies.erase(std::make_pair(C,UnsplitChain));
    NodeChainAssemblies.erase(std::make_pair(UnsplitChain,C));
    AdjacentChains[C].erase(UnsplitChain);
    if(C!=SplitChain)
      AdjacentChains[SplitChain].insert(C);
  }

  for(auto CI=AdjacentChains[SplitChain].begin(), CE=AdjacentChains[SplitChain].end(); CI!=CE;){
    NodeChain * C = *CI;
    bool CS = UpdateChainEdge(C, SplitChain);
    bool SC = UpdateChainEdge(SplitChain, C);
    if(CS || SC){
      AdjacentChains[C].insert(SplitChain);
      CI++;
    }else{
      AdjacentChains[C].erase(SplitChain);
      CI = AdjacentChains[SplitChain].erase(CI);
    }
  }

  AdjacentChains.erase(UnsplitChain);
}


void ExtTSPChainBuilder::MergeChains(NodeChainAssembly& A){
  for(auto& Slice: A.Slices)
    A.SplitChain->Nodes.splice(A.SplitChain->Nodes.end(), Slice.Chain->Nodes, Slice.Begin, Slice.End);

  uint32_t RunningOffset = 0;
  for(const ELFCfgNode * Node: A.SplitChain->Nodes){
    NodeToChainMap[Node]=A.SplitChain;
    NodeOffset[Node] = RunningOffset;
    RunningOffset += Node->ShSize;
  }
  A.SplitChain->Size += A.UnsplitChain->Size;
  A.SplitChain->Freq += A.UnsplitChain->Freq;

  A.SplitChain->Score = A.GetExtTSPScore();

  MergeChainEdges(A.SplitChain, A.UnsplitChain);
  Chains.erase(A.UnsplitChain->DelegateNode->Shndx);
}

double ExtTSPChainBuilder::ExtTSPScore(NodeChain * Chain){
  double Score = 0;
  uint32_t SrcOffset = 0;
  //std::cerr << "Considering Chain: " << Chain << "\n";
  for(const ELFCfgNode * Node: Chain->Nodes){
    /*
    auto ProfiledOutsIt = ProfiledOuts.find(Node);
    if(ProfiledOutsIt == ProfiledOuts.end())
      continue;
      */
    //for(const ELFCfgEdge * Edge: ProfiledOutsIt->second){
    for(const ELFCfgEdge * Edge: Node->Outs){
      if(Edge->Weight==0)
        continue;
      NodeChain * SinkChain = NodeToChainMap[Edge->Sink];
      if(SinkChain!=Chain)
        continue;
      //std::cerr << *Edge << "\n";
      auto SinkOffset = NodeOffset[Edge->Sink];
      bool EdgeForward = SrcOffset < SinkOffset;
      uint32_t Distance = EdgeForward ? SinkOffset - SrcOffset - Node->ShSize : SrcOffset - SinkOffset + Node->ShSize;
      double S = 0;
      if (Distance == 0)
        S = Edge->Weight * opts::FallthroughWeight;
      else {
        if(EdgeForward && Distance < opts::ForwardDistance)
          S = Edge->Weight * opts::ForwardWeight * (1.0 - ((double)Distance)/opts::ForwardDistance);
        if(!EdgeForward && Distance < opts::BackwardDistance)
          S = Edge->Weight * opts::BackwardWeight * (1.0 - ((double)Distance)/opts::BackwardDistance);
      }
      //std::cerr << S << "\t" << Distance << "\n";
      Score += S;
    }
    SrcOffset += Node->ShSize;
  }
  return Score;
}

bool ExtTSPChainBuilder::UpdateChainEdge(NodeChain * SplitChain, NodeChain * UnsplitChain){
  bool DoSplit = (SplitChain->Size <= opts::ChainSplitThreshold);
  auto SlicePosEnd = DoSplit ? SplitChain->Nodes.end() : std::next(SplitChain->Nodes.begin());

  std::list<NodeChainAssembly> NCAs;


  for(auto SlicePos = SplitChain->Nodes.begin(); SlicePos!=SlicePosEnd; ++SlicePos){
    if(SlicePos!=SplitChain->Nodes.begin() && MutuallyForcedOut[*std::prev(SlicePos)]==*SlicePos)
        continue;

    uint8_t MergeOrderEnd = (SlicePos==SplitChain->Nodes.begin()) ? 1 : 4;
    for(uint8_t MergeOrder=0; MergeOrder < MergeOrderEnd; ++MergeOrder){
      NodeChainAssembly NCA(SplitChain, UnsplitChain, SlicePos, MergeOrder, this);
      ELFCfgNode * EntryNode = Cfg->getEntryNode();
      if(opts::FunctionEntryFirst &&
         (NCA.SplitChain->GetFirstNode()==EntryNode || NCA.UnsplitChain->GetFirstNode()==EntryNode) &&
         (NCA.GetFirstNode() != EntryNode))
        continue;
      NCAs.push_back(std::move(NCA));
    }
  }

  auto BestCandidate = std::max_element(NCAs.begin(), NCAs.end(),
                                          [](NodeChainAssembly& C1,
                                             NodeChainAssembly& C2){
                                             return C1.GetExtTSPGain() < C2.GetExtTSPGain();});

  NodeChainAssemblies.erase(std::make_pair(SplitChain, UnsplitChain));

  if(BestCandidate!=NCAs.end() && BestCandidate->GetExtTSPGain() > 0){
    NodeChainAssemblies.emplace(std::make_pair(SplitChain,UnsplitChain), std::move(*BestCandidate));
    return true;
  }else
    return false;
}

ExtTSPChainBuilder::ExtTSPChainBuilder(const ELFCfg * _Cfg): NodeChainBuilder(_Cfg) {
  std::unordered_map<const ELFCfgNode *, std::vector<ELFCfgEdge *>> ProfiledOuts;
  std::unordered_map<const ELFCfgNode *, std::vector<ELFCfgEdge *>> ProfiledIns;

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
  std::map<const ELFCfgNode *, unsigned> VisitedNodes;
  set<const ELFCfgNode *> CutNodes;
  unsigned Color = 0;
  for (auto It = MutuallyForcedOut.begin(); It!=MutuallyForcedOut.end(); ++It){
    if(VisitedNodes[It->first])
      continue;
    uint64_t MinWeight = 0;
    const ELFCfgNode * MinNode = nullptr;
    auto NodeIt = It;
    Color++;
    while(NodeIt!=MutuallyForcedOut.end()){
      const ELFCfgNode * Node = NodeIt->first;
      auto NodeColor = VisitedNodes[Node];
      if(NodeColor!=0){
        if(NodeColor==Color){ /*found a cycle */
          CutNodes.insert(MinNode);
        }
        break;
      }else
        VisitedNodes[Node] = Color;
      const ELFCfgEdge * Edge = ProfiledOuts[Node].front();
      if(MinNode==nullptr || (Edge->Weight < MinWeight)){
        MinWeight = Edge->Weight;
        MinNode = Node;
      }
      NodeIt = MutuallyForcedOut.find(NodeIt->second);
    }
  }

  for(const ELFCfgNode * Node: CutNodes)
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
          NodeChain * OtherChain = NodeToChainMap[Edge->Sink];
          if(Chain!=OtherChain){
            bool CO = UpdateChainEdge(Chain, OtherChain);
            bool OC = UpdateChainEdge(OtherChain, Chain);
            if(CO || OC){
              AdjacentChains[Chain].insert(OtherChain);
              AdjacentChains[OtherChain].insert(Chain);
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
  /*
  std::cerr << "done for function: " << Cfg->Name.str() << "\t" << std::accumulate(Chains.begin(), Chains.end(), 0.0, [] (double sum, const std::pair<const uint64_t, unique_ptr<NodeChain>>& C){ return C.second->Score + sum;}) << "\n";
  */
}

void ExtTSPChainBuilder::NodeChainAssembly::Dump() const {
    std::cerr << SplitChain << " <-> " << UnsplitChain << " MergeOrder("<< (unsigned)MergeOrder << ") " << "SlicePos()" << "\n";
}

double ExtTSPChainBuilder::NodeChainAssembly::ExtTSPScore() const {
  double Score = 0;
  for(uint8_t SrcSliceIdx = 0; SrcSliceIdx < 3; ++SrcSliceIdx){
    auto& SrcSlice = Slices[SrcSliceIdx];
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
            auto& SinkSlice = Slices[SinkSliceIdx];
            Distance = EdgeForward ?
                (SrcSlice.EndOffset - SrcNodeOffset - Node->ShSize + SinkNodeOffset - SinkSlice.BeginOffset) :
                (SrcNodeOffset - SrcSlice.BeginOffset + Node->ShSize + SinkSlice.EndOffset - SinkNodeOffset);
            if(std::abs(SinkSliceIdx - SrcSliceIdx) == 2)
              Distance += Slices[1].Size();
          }
          double S = 0;
          if (Distance == 0){
            S = Edge->Weight * opts::FallthroughWeight;
          } else {
            if(EdgeForward && Distance < opts::ForwardDistance)
              S = Edge->Weight * opts::ForwardWeight * (1.0 - ((double)Distance)/opts::ForwardDistance);
            if(!EdgeForward && Distance < opts::BackwardDistance)
              S = Edge->Weight * opts::BackwardWeight * (1.0 - ((double)Distance)/opts::BackwardDistance);
          }
          Score += S;
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
