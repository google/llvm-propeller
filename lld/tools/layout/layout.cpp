#include <algorithm>
#include <fstream>
#include <string>
#include <iostream>
#include <stdio.h>
#include <set>
#include <deque>
#include <list>
#include <numeric>

#include "../../ELF/PLOELFCfg.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "lld/Common/Args.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

using llvm::StringRef;

using std::list;
using std::set;
using std::string;
using std::unique_ptr;
using namespace llvm;

namespace opts{

static cl::opt<std::string>
CfgRead(
  "cfg-read",
  cl::desc("File to read the Cfg from."),
  cl::Required);

static cl::opt<std::string>
CfgDump(
  "cfg-dump",
  cl::desc("File to dump the cfg to."),
  cl::ZeroOrMore);

static cl::opt<std::string>
LayoutDump(
  "layout-dump",
  cl::desc("File to dump the layout to."),
  cl::ZeroOrMore);

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

  void Dump() const {
    std::cerr << "Total size: " << Size << "\n";
    for(const ELFCfgNode * N: Nodes){
      std::cerr << N->ShName.str() << "[" << N->ShSize << "] ";
    }
    std::cerr << "\n";
  }
};


/* Base class for incremental chaining of Nodes in a Cfg.*/
class NodeChainBuilder {
 public:
  const ELFCfg * Cfg;

  /* Set of built chains */
  std::map<uint64_t, unique_ptr<NodeChain>> Chains;
  std::unordered_map<const ELFCfgNode *, NodeChain *> NodeToChainMap;
  std::unordered_map<const ELFCfgNode *, uint32_t> NodeOffset;
  std::list<const ELFCfgNode *> Layout;

  void SortChainsByExecutionDensity(){
    std::vector<const NodeChain*> ChainsCopy;
    for(auto CI=Chains.cbegin(), CE=Chains.cend(); CI!=CE; ++CI){
      ChainsCopy.push_back(CI->second.get());
    }

    std::sort(ChainsCopy.begin(), ChainsCopy.end(),
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

    for(const NodeChain * Chain: ChainsCopy)
      for(const ELFCfgNode * Node: Chain->Nodes)
        Layout.push_back(Node);
  }

  void AttachFallThroughs(){
    for(auto& Node: Cfg->Nodes){
      if(Node->FTEdge!=nullptr){
        AttachNodes(Node.get(), Node->FTEdge->Sink);
      }
    }
  }

  NodeChain * MergeChains(NodeChain* LeftChain, NodeChain* RightChain){
    for(const ELFCfgNode* Node: RightChain->Nodes){
      LeftChain->Nodes.push_back(Node);
      NodeToChainMap[Node]=LeftChain;
      NodeOffset[Node] += LeftChain->Size;
    }
    LeftChain->Size += RightChain->Size;
    LeftChain->Freq += RightChain->Freq;
    Chains.erase(RightChain->DelegateNode->Shndx);
    return LeftChain;
  }

  /* This function tries to place two nodes immediately adjacent to
   * each other (used for fallthroughs).
   * Returns true if this can be done. */
  bool AttachNodes(const ELFCfgNode * Src, const ELFCfgNode * Sink){
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

  NodeChainBuilder(const ELFCfg * _Cfg): Cfg(_Cfg){
    for(auto& Node: Cfg->Nodes){
      CreateChainForNode(Node.get());
    }
  }

  void ChainAll(){
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
    SortChainsByExecutionDensity();
  }

 private:
  void CreateChainForNode(const ELFCfgNode * Node){
    NodeChain * Chain = new NodeChain(Node);
    NodeToChainMap[Node] = Chain;
    NodeOffset[Node] = 0;
    Chains.emplace(Node->Shndx, std::move(Chain));
  }

};


/* Chain builder based on ExtTSP metric */
class ExtTSPChainBuilder : public NodeChainBuilder{
 private:
  class NodeChainSlice{
   public:
    NodeChain * Chain;
    list<const ELFCfgNode *>::iterator Begin, End;
    uint32_t BeginOffset, EndOffset;

    NodeChainSlice(NodeChain * C, list<const ELFCfgNode*>::iterator _Begin, list<const ELFCfgNode*>::iterator _End, const NodeChainBuilder& ChainBuilder): Chain(C), Begin(_Begin), End(_End) {
      BeginOffset = ChainBuilder.NodeOffset.at(*Begin);
      if(End==Chain->Nodes.end())
        EndOffset = Chain->Size;
      else
        EndOffset = ChainBuilder.NodeOffset.at(*End);
      assert(EndOffset >= BeginOffset);
    }

    uint32_t Size() const{
      return EndOffset - BeginOffset;
    }

  };

  class NodeChainAssembly{
   private:
    double Score;
    bool ScoreComputed{false};
    const ExtTSPChainBuilder * ChainBuilder;

   public:
    NodeChain *SplitChain, *UnsplitChain;
    uint8_t MergeOrder;
    list<const ELFCfgNode*>::iterator SlicePos;

    vector<NodeChainSlice> Slices;


    /* Disable the implicit copy constructor. */
   // void operator=(const NodeChainAssembly& NCA) = delete;

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

    NodeChainAssembly() {}

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

    double ExtTSPScore() const {
      double Score = 0;
      //std::cerr << "Considering NCA: \n";
      //NCA.Dump();
      //NCA.SplitChain->Dump();
      //NCA.UnsplitChain->Dump();
      for(uint8_t SrcSliceIdx = 0; SrcSliceIdx < 3; ++SrcSliceIdx){
        auto& SrcSlice = Slices[SrcSliceIdx];
        uint32_t SrcNodeOffset = SrcSlice.BeginOffset;
        for(auto NodeIt = SrcSlice.Begin; NodeIt!=SrcSlice.End; SrcNodeOffset+=(*NodeIt)->ShSize, ++NodeIt){
          const ELFCfgNode * Node = *NodeIt;
          /*
          auto NodeOutsIt = ProfiledOuts.find(Node);
          if(NodeOutsIt == ProfiledOuts.end())
            continue;
          */
          //for(const ELFCfgEdge * Edge: NodeOutsIt->second){
          for(const ELFCfgEdge * Edge: Node->Outs){
            if(Edge->Weight==0)
              continue;

            uint8_t SinkSliceIdx;

            if(FindSliceIndex(Edge->Sink, SinkSliceIdx)){
              auto SinkNodeOffset = ChainBuilder->NodeOffset.at(Edge->Sink);
              bool EdgeForward = (SrcSliceIdx < SinkSliceIdx) ||
                  (SrcSliceIdx==SinkSliceIdx && SrcNodeOffset < SinkNodeOffset);

              //std::cerr << *Edge << "\n";
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
              //std::cerr << S << "\t" << Distance << "\n";
              Score += S;
            }
          }
        }
      }
      return Score;
    }


    const ELFCfgNode * GetFirstNode() const {
      for(auto& Slice: Slices)
        if(Slice.Begin!=Slice.End)
          return *Slice.Begin;
      return nullptr;
    }

    void Dump() const {
      std::cerr << SplitChain << " <-> " << UnsplitChain << " MergeOrder("<< (unsigned)MergeOrder << ") " << "SlicePos()" << "\n";
    }
  };

  std::unordered_map<const ELFCfgNode *, ELFCfgNode *> MutuallyForcedOut;
  std::unordered_map<const ELFCfgNode *, std::vector<ELFCfgEdge *>> ProfiledOuts;
  std::unordered_map<const ELFCfgNode *, std::vector<ELFCfgEdge *>> ProfiledIns;

  std::map<std::pair<NodeChain*, NodeChain *>, unique_ptr<NodeChainAssembly>> NodeChainAssemblies;
  std::unordered_map<NodeChain*, std::unordered_set<NodeChain *>> AdjacentChains;

  void MergeChainEdges(NodeChain *SplitChain, NodeChain *UnsplitChain){

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


  void MergeChains(NodeChainAssembly& A){
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
  }

  double ExtTSPScore(NodeChain * Chain){
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

  bool UpdateChainEdge(NodeChain * SplitChain, NodeChain * UnsplitChain){
    //std::cerr << "Updating chain edge: " << SplitChain << "\t" << UnsplitChain << "\n";
    NodeChainAssembly BestNCA;
    double BestGain = 0;
    bool DoSplit = (SplitChain->Size <= opts::ChainSplitThreshold);
    auto SlicePosEnd = DoSplit ? SplitChain->Nodes.end() : std::next(SplitChain->Nodes.begin());

    for(auto SlicePos = SplitChain->Nodes.begin(); SlicePos!=SlicePosEnd; ++SlicePos){
      if(SlicePos!=SplitChain->Nodes.begin() && MutuallyForcedOut[*std::prev(SlicePos)]==*SlicePos)
          continue;

      uint8_t MergeOrderEnd = (SlicePos==SplitChain->Nodes.begin()) ? 1 : 4;
      for(uint8_t MergeOrder=0; MergeOrder < MergeOrderEnd; ++MergeOrder){
        NodeChainAssembly NCA(SplitChain, UnsplitChain, SlicePos, MergeOrder, this);
        //std::cerr << "Trying NCA: ";
        //NCA.Dump();
        ELFCfgNode * EntryNode = Cfg->getEntryNode();
        if(opts::FunctionEntryFirst &&
           (NCA.SplitChain->GetFirstNode()==EntryNode || NCA.UnsplitChain->GetFirstNode()==EntryNode) &&
           (NCA.GetFirstNode() != EntryNode))
          continue;
        if(NCA.GetExtTSPGain() > BestGain){
          BestNCA = NCA;
          BestGain = NCA.GetExtTSPGain();
        }
      }
    }
    if(BestGain > 0){
      NodeChainAssemblies[std::make_pair(SplitChain,UnsplitChain)] = llvm::make_unique<NodeChainAssembly>(BestNCA);
      return true;
    }else{
      NodeChainAssemblies.erase(std::make_pair(SplitChain, UnsplitChain));
      return false;
    }
  }

 public:
  ExtTSPChainBuilder(const ELFCfg * _Cfg): NodeChainBuilder(_Cfg) {
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
    std::set<const ELFCfgNode *> CutNodes;
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


  void ChainAll(){
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

    NodeChainAssembly * BestCandidate;
    double BestGain;
    do {
      BestCandidate = nullptr;
      BestGain = 0;
      for(auto& NCA: NodeChainAssemblies){
        if(NCA.second->GetExtTSPGain() > BestGain){
          BestCandidate = NCA.second.get();
          BestGain = BestCandidate->GetExtTSPGain();
        }
      }
      if(BestGain!=0){
        NodeChain * SplitChain = BestCandidate->SplitChain;
        NodeChain * UnsplitChain = BestCandidate->UnsplitChain;
        MergeChains(*BestCandidate);
        MergeChainEdges(SplitChain, UnsplitChain);
        Chains.erase(UnsplitChain->DelegateNode->Shndx);
      }
    } while(BestGain!=0);

    AttachFallThroughs();
    SortChainsByExecutionDensity();

    /*
    std::cerr << "done for function: " << Cfg->Name.str() << "\t" << std::accumulate(Chains.begin(), Chains.end(), 0.0, [] (double sum, const std::pair<const uint64_t, unique_ptr<NodeChain>>& C){ return C.second->Score + sum;}) << "\n";
    */

  }
};

}
}

int main(int Argc, const char **Argv) {
  cl::ParseCommandLineOptions(Argc, Argv, "Layout");
  StringRef CfgFile = opts::CfgRead.getValue();
  auto CfgReader = lld::plo::ELFCfgReader(CfgFile);
  CfgReader.readCfgs();
  fprintf(stderr, "Read all Cfgs\n");

  if (!opts::CfgDump.empty()){
    std::ofstream OS;
    OS.open(opts::CfgDump.getValue(), std::ios::out);

    if (!OS.good()) {
      fprintf(stderr, "File is not good for writing: <%s>\n", opts::CfgDump.getValue().c_str());
      exit(0);
    } else {
        for (auto &Cfg: CfgReader.Cfgs) {
          Cfg->dumpToOS(OS);
        }
      OS.close();
    }
  }

  if(!opts::LayoutDump.empty()){
    std::ofstream LOS;
    LOS.open(opts::LayoutDump.getValue(), std::ios::out);
    for(auto& Cfg: CfgReader.Cfgs){
      if (Cfg->isHot()){
        /*
        auto ChainBuilder = lld::plo::NodeChainBuilder(Cfg.get());
        ChainBuilder.GreedyChain();
        ChainBuilder.ChainSort();
        */
        auto ChainBuilder = lld::plo::ExtTSPChainBuilder(Cfg.get());
        ChainBuilder.ChainAll();
        for(auto * Node: ChainBuilder.Layout)
          LOS << Node->ShName.str() << "\n";
      } else {
        for(auto& Node: Cfg->Nodes){
          LOS << Node->ShName.str() << "\n";
        }
      }
    }
    LOS.close();
  }
}
