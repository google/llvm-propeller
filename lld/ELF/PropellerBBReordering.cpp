#include "PropellerBBReordering.h"

#include "llvm/Support/CommandLine.h"

#include <stdio.h>

#include <deque>
#include <iostream>
#include <numeric>
#include <set>
#include <unordered_map>
#include <vector>

#define NEGATE(X) (-(int64_t)X)

using std::deque;
using std::list;
using std::set;
using std::unique_ptr;
using std::unordered_map;
using std::vector;
using namespace llvm;

namespace opts {

static cl::opt<bool> PrintStats("print-stats",
                                cl::desc("Print stats for the new layout."),
                                cl::init(false), cl::ZeroOrMore);

static cl::opt<bool>
    UsePathCover("use-path-cover",
                 cl::desc("Use the path cover algorithm to find covers."),
                 cl::init(false), cl::ZeroOrMore);

static cl::opt<bool>
    SeparateHotCold("separate-hot-cold",
                    cl::desc("Separate the hot and cold basic blocks."),
                    cl::init(true), cl::ZeroOrMore);

static cl::opt<bool> FunctionEntryFirst(
    "func-entry-first",
    cl::desc("Force function entry to appear first in the ordering."),
    cl::init(true), cl::ZeroOrMore);

static cl::opt<double> FallthroughWeight(
    "fallthrough-weight",
    cl::desc("Fallthrough weight for ExtTSP metric calculation."),
    cl::init(1.0), cl::ZeroOrMore);

static cl::opt<double> ForwardWeight(
    "forward-weight",
    cl::desc("Forward branch weight for ExtTSP metric calculation."),
    cl::init(0.1), cl::ZeroOrMore);

static cl::opt<double> BackwardWeight(
    "backward-weight",
    cl::desc("Backward branch weight for ExtTSP metric calculation."),
    cl::init(0.1), cl::ZeroOrMore);

static cl::opt<uint32_t> ForwardDistance(
    "forward-distance",
    cl::desc(
        "Forward branch distance threshold for ExtTSP metric calculation."),
    cl::init(1024), cl::ZeroOrMore);

static cl::opt<uint32_t> BackwardDistance(
    "backward-distance",
    cl::desc(
        "Backward branch distance threshold for ExtTSP metric calculation."),
    cl::init(640), cl::ZeroOrMore);

static cl::opt<uint32_t> ChainSplitThreshold(
    "chain-split-threshold",
    cl::desc("Maximum binary size of a code chain that can be split."),
    cl::init(128), cl::ZeroOrMore);
} // namespace opts

namespace lld {
namespace propeller {

double GetEdgeExtTSPScore(const ELFCfgEdge *Edge, bool IsEdgeForward,
                          uint32_t SrcSinkDistance) {
  if (Edge->Weight == 0)
    return 0;

  if (SrcSinkDistance == 0 && (Edge->Type == ELFCfgEdge::EdgeType::INTRA_FUNC))
    return Edge->Weight * opts::FallthroughWeight;

  if (IsEdgeForward && SrcSinkDistance < opts::ForwardDistance)
    return Edge->Weight * opts::ForwardWeight *
           (1.0 - ((double)SrcSinkDistance) / opts::ForwardDistance);

  if (!IsEdgeForward && SrcSinkDistance < opts::BackwardDistance)
    return Edge->Weight * opts::BackwardWeight *
           (1.0 - ((double)SrcSinkDistance) / opts::BackwardDistance);
  return 0;
}

void NodeChain::Dump() const {
  fprintf(stderr,
          "Chain for Cfg Node: {%s}: total binary size: %u, total "
          "frequency: %lu:\n",
          DelegateNode->ShName.str().c_str(), Size, Freq);

  for (const ELFCfgNode *N : Nodes)
    fprintf(stderr, "[%s] ", N->ShName.str().c_str());
  fprintf(stderr, "\n");
}

void NodeChainBuilder::SortChainsByExecutionDensity(
    vector<const NodeChain *> &ChainOrder) {
  for (auto CI = Chains.cbegin(), CE = Chains.cend(); CI != CE; ++CI) {
    ChainOrder.push_back(CI->second.get());
  }

  std::sort(
      ChainOrder.begin(), ChainOrder.end(),
      [this](const NodeChain *C1, const NodeChain *C2) {
        if (opts::FunctionEntryFirst) {
          if (C1->GetFirstNode() == this->Cfg->getEntryNode())
            return true;
          if (C2->GetFirstNode() == this->Cfg->getEntryNode())
            return false;
        }
        double C1ExecDensity = C1->GetExecDensity();
        double C2ExecDensity = C2->GetExecDensity();
        if (C1ExecDensity == C2ExecDensity) {
          if (C1->DelegateNode->MappedAddr == C2->DelegateNode->MappedAddr)
            return C1->DelegateNode->Shndx < C2->DelegateNode->Shndx;
          return C1->DelegateNode->MappedAddr < C2->DelegateNode->MappedAddr;
        }
        return C1ExecDensity > C2ExecDensity;
      });
}

void NodeChainBuilder::AttachFallThroughs() {
  /*
  for(auto& Node: Cfg->Nodes){
    if(Node->Outs.size()==1){
      const ELFCfgEdge * E = Node->Outs.front();
      if(E->Type != ELFCfgEdge::EdgeType::INTRA_FUNC)
        continue;
      if(E->Sink->Ins.size()==1)
        AttachNodes(E->Src, E->Sink);
    }
  }
  */

  for (auto &Node : Cfg->Nodes) {
    if (Node->FTEdge != nullptr) {
      AttachNodes(Node.get(), Node->FTEdge->Sink);
    }
  }

  for (auto &Edge : Cfg->IntraEdges) {
    AttachNodes(Edge->Src, Edge->Sink);
  }
}

/* Merge two chains in the specified order. */
void NodeChainBuilder::MergeChains(NodeChain *LeftChain,
                                   NodeChain *RightChain) {
  for (const ELFCfgNode *Node : RightChain->Nodes) {
    LeftChain->Nodes.push_back(Node);
    NodeToChainMap[Node] = LeftChain;
    NodeOffset[Node] += LeftChain->Size;
  }
  LeftChain->Size += RightChain->Size;
  LeftChain->Freq += RightChain->Freq;
  Chains.erase(RightChain->DelegateNode->Shndx);
}

/* This function tries to place two nodes immediately adjacent to
 * each other (used for fallthroughs).
 * Returns true if this can be done. */
bool NodeChainBuilder::AttachNodes(const ELFCfgNode *Src,
                                   const ELFCfgNode *Sink) {
  if (opts::FunctionEntryFirst && Sink == Cfg->getEntryNode())
    return false;
  if (opts::SeparateHotCold && (Src->Freq == 0 ^ Sink->Freq == 0))
    return false;
  NodeChain *SrcChain = NodeToChainMap.at(Src);
  NodeChain *SinkChain = NodeToChainMap.at(Sink);
  if (SrcChain == SinkChain)
    return false;
  if (SrcChain->GetLastNode() != Src || SinkChain->GetFirstNode() != Sink)
    return false;
  log("Attaching nodes " + Twine(Src->ShName) + "[" + Twine(Src->Freq) + "] -> " + Twine(Sink->ShName) + "[" + Twine(Sink->Freq) + "]");
  MergeChains(SrcChain, SinkChain);
  return true;
}

void NodeChainBuilder::ComputeChainOrder(
    vector<const NodeChain *> &ChainOrder) {
  if (opts::UsePathCover)
    BuildPathCovers();

  deque<const ELFCfgEdge *> CfgEdgeQ;

  for (auto &Edge : Cfg->IntraEdges)
    CfgEdgeQ.push_back(Edge.get());

  std::sort(CfgEdgeQ.begin(), CfgEdgeQ.end(),
            [](const ELFCfgEdge *Edge1, const ELFCfgEdge *Edge2) {
              if (Edge1->Weight == Edge2->Weight) {
                if (Edge1->Src->MappedAddr == Edge2->Src->MappedAddr)
                  return Edge1->Sink->MappedAddr > Edge2->Sink->MappedAddr;
                return Edge1->Src->MappedAddr > Edge2->Src->MappedAddr;
              }
              return Edge1->Weight < Edge2->Weight;
            });
  while (!CfgEdgeQ.empty()) {
    auto *Edge = CfgEdgeQ.back();
    CfgEdgeQ.pop_back();
    AttachNodes(Edge->Src, Edge->Sink);
  }
  SortChainsByExecutionDensity(ChainOrder);
  if (opts::PrintStats)
    PrintStats();
}

void ExtTSPChainBuilder::MergeChains(std::unique_ptr<NodeChainAssembly> A) {
  /* Merge the Node sequences according to a given NodeChainAssembly.*/
  list<const ELFCfgNode*> NewNodeOrder;
  for (NodeChainSlice &Slice : A->Slices){
    std::copy(Slice.Begin, Slice.End, std::back_inserter(NewNodeOrder));
  }
  A->SplitChain->Nodes = std::move(NewNodeOrder);
  //fprintf(stderr, "Now chain is:\n");
  //A->SplitChain->Dump();

  /* Update NodeOffset and NodeToChainMap for all the nodes in the sequence */
  uint32_t RunningOffset = 0;
  for (const ELFCfgNode *Node : A->SplitChain->Nodes) {
    NodeToChainMap[Node] = A->SplitChain;
    NodeOffset[Node] = RunningOffset;
    RunningOffset += Node->ShSize;
  }

  /* Update the total binary size, frequency, and TSP score of the merged chain
   */
  A->SplitChain->Size += A->UnsplitChain->Size;
  A->SplitChain->Freq += A->UnsplitChain->Freq;
  A->SplitChain->Score = A->GetExtTSPScore();

  /* Merge the assembly candidate chains of the two chains and remove the
   * records for the defunct NodeChain. */
  for (NodeChain *C : CandidateChains[A->UnsplitChain]) {
    // std::cerr << "Candidate chain removal: " << C << " " << A->UnsplitChain
    // << " " << A->SplitChain << "\n";
    NodeChainAssemblies.erase(std::make_pair(C, A->UnsplitChain));
    NodeChainAssemblies.erase(std::make_pair(A->UnsplitChain, C));
    CandidateChains[C].erase(A->UnsplitChain);
    if (C != A->SplitChain)
      CandidateChains[A->SplitChain].insert(C);
  }

  /* Update the NodeChainAssembly for all candidate chains of the merged
   * NodeChain. Remove a NodeChain from the merge chain's candidates if the
   * NodeChainAssembly update finds no gain.*/
  auto &SplitChainCandidateChains = CandidateChains[A->SplitChain];

  for (auto CI = SplitChainCandidateChains.begin(),
            CE = SplitChainCandidateChains.end();
       CI != CE;) {
    NodeChain *OtherChain = *CI;
    auto &OtherChainCandidateChains = CandidateChains[OtherChain];
    bool OS = UpdateNodeChainAssembly(OtherChain, A->SplitChain);
    bool SO = UpdateNodeChainAssembly(A->SplitChain, OtherChain);
    if (OS || SO) {
      OtherChainCandidateChains.insert(A->SplitChain);
      CI++;
    } else {
      OtherChainCandidateChains.erase(A->SplitChain);
      CI = SplitChainCandidateChains.erase(CI);
    }
  }

  /* Remove all the candidate chain records for the merged-in chain.*/
  CandidateChains.erase(A->UnsplitChain);

  /* Finally, remove the merged-in chain record from the Chains.*/
  Chains.erase(A->UnsplitChain->DelegateNode->Shndx);
}

/* Calculate the Extended TSP metric for a NodeChain */
double ExtTSPChainBuilder::ExtTSPScore(NodeChain *Chain) const {
  double Score = 0;
  uint32_t SrcOffset = 0;
  for (const ELFCfgNode *Node : Chain->Nodes) {
    for (const ELFCfgEdge *Edge : Node->Outs) {
      if (Edge->Weight == 0)
        continue;
      NodeChain *SinkChain = NodeToChainMap.at(Edge->Sink);
      if (SinkChain != Chain)
        continue;
      auto SinkOffset = NodeOffset.at(Edge->Sink);
      bool EdgeForward = SrcOffset < SinkOffset;
      uint32_t Distance = EdgeForward ? SinkOffset - SrcOffset - Node->ShSize
                                      : SrcOffset - SinkOffset + Node->ShSize;
      Score += GetEdgeExtTSPScore(Edge, EdgeForward, Distance);
    }
    SrcOffset += Node->ShSize;
  }
  return Score;
}

/* Updates the best NodeChainAssembly between two NodeChains. The existing
 * record will be replaced by the new NodeChainAssembly if a non-zero gain
 * is achieved. Otherwise, it will be removed. */
bool ExtTSPChainBuilder::UpdateNodeChainAssembly(NodeChain *SplitChain,
                                                 NodeChain *UnsplitChain) {
  /* Only split the chain if the size of the chain is smaller than a treshold.*/
  bool DoSplit = (SplitChain->Size <= opts::ChainSplitThreshold);
  auto SlicePosEnd =
      DoSplit ? SplitChain->Nodes.end() : std::next(SplitChain->Nodes.begin());

  list<unique_ptr<NodeChainAssembly>> CandidateNCAs;

  for (auto SlicePos = SplitChain->Nodes.begin(); SlicePos != SlicePosEnd;
       ++SlicePos) {
    /* Do not split the mutually-forced edges in the chain.*/
    if (SlicePos != SplitChain->Nodes.begin() &&
        MutuallyForcedOut[*std::prev(SlicePos)] == *SlicePos)
      continue;

    /* If the split position is at the beginning (no splitting), only consider
     * one MergeOrder.*/
    auto MergeOrderEnd = (SlicePos == SplitChain->Nodes.begin())
                             ? MergeOrder::BeginNext
                             : MergeOrder::End;

    for (uint8_t MI = MergeOrder::Begin; MI != MergeOrderEnd; MI++) {
      MergeOrder MOrder = static_cast<MergeOrder>(MI);
      auto NCA = unique_ptr<NodeChainAssembly>(new NodeChainAssembly(
          SplitChain, UnsplitChain, SlicePos, MOrder, this));
      ELFCfgNode *EntryNode = Cfg->getEntryNode();
      if (opts::FunctionEntryFirst &&
          (NCA->SplitChain->GetFirstNode() == EntryNode ||
           NCA->UnsplitChain->GetFirstNode() == EntryNode) &&
          (NCA->GetFirstNode() != EntryNode))
        continue;
      CandidateNCAs.push_back(std::move(NCA));
    }
  }

  auto BestCandidate = std::max_element(
      CandidateNCAs.begin(), CandidateNCAs.end(),
      [](unique_ptr<NodeChainAssembly> &C1, unique_ptr<NodeChainAssembly> &C2) {
        return C1->GetExtTSPGain() < C2->GetExtTSPGain();
      });

  NodeChainAssemblies.erase(std::make_pair(SplitChain, UnsplitChain));

  if (BestCandidate != CandidateNCAs.end() &&
      (*BestCandidate)->GetExtTSPGain() > 0) {
    NodeChainAssemblies.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(SplitChain, UnsplitChain),
        std::forward_as_tuple(std::move(*BestCandidate)));
    return true;
  } else
    return false;
}

ExtTSPChainBuilder::ExtTSPChainBuilder(const ELFCfg *_Cfg)
    : NodeChainBuilder(_Cfg) {
  unordered_map<const ELFCfgNode *, vector<ELFCfgEdge *>> ProfiledOuts;
  unordered_map<const ELFCfgNode *, vector<ELFCfgEdge *>> ProfiledIns;

  for (auto &Node : Cfg->Nodes) {
    std::copy_if(Node->Outs.begin(), Node->Outs.end(),
                 std::back_inserter(ProfiledOuts[Node.get()]),
                 [](const ELFCfgEdge *Edge) {
                   return Edge->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
                          Edge->Weight != 0;
                 });
    std::copy_if(Node->Ins.begin(), Node->Ins.end(),
                 std::back_inserter(ProfiledIns[Node.get()]),
                 [](const ELFCfgEdge *Edge) {
                   return Edge->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
                          Edge->Weight != 0;
                 });
  }

  for (auto &Node : Cfg->Nodes) {
    if (ProfiledOuts[Node.get()].size() == 1) {
      ELFCfgEdge *Edge = ProfiledOuts[Node.get()].front();
      if (ProfiledIns[Edge->Sink].size() == 1)
        MutuallyForcedOut[Node.get()] = Edge->Sink;
    }
  }

  /* Break cycles in the mutually forced edges by cutting the edge sinking to
   * the smallest address in every cycle (hopefully a loop backedge).*/
  map<const ELFCfgNode *, unsigned> NodeToCycleMap;
  set<const ELFCfgNode *> CycleCutNodes;
  unsigned CycleCount = 0;
  for (auto It = MutuallyForcedOut.begin(); It != MutuallyForcedOut.end();
       ++It) {
    if (NodeToCycleMap[It->first])
      continue;
    const ELFCfgEdge *VictimEdge = nullptr;
    auto NodeIt = It;
    CycleCount++;
    while (NodeIt != MutuallyForcedOut.end()) {
      const ELFCfgNode *Node = NodeIt->first;
      auto NodeCycle = NodeToCycleMap[Node];
      if (NodeCycle != 0) {
        if (NodeCycle == CycleCount) { /*found a cycle */
          CycleCutNodes.insert(VictimEdge->Src);
        }
        break;
      } else
        NodeToCycleMap[Node] = CycleCount;
      const ELFCfgEdge *Edge = ProfiledOuts[Node].front();
      if (!VictimEdge ||
          (VictimEdge->Sink->MappedAddr < VictimEdge->Sink->MappedAddr)) {
        VictimEdge = Edge;
      }
      NodeIt = MutuallyForcedOut.find(NodeIt->second);
    }
  }

  for (const ELFCfgNode *Node : CycleCutNodes)
    MutuallyForcedOut.erase(Node);
}

void ExtTSPChainBuilder::ComputeChainOrder(
    vector<const NodeChain *> &ChainOrder) {
  if (opts::UsePathCover)
    BuildPathCovers();

  for (auto &KV : MutuallyForcedOut)
    AttachNodes(KV.first, KV.second);

  for (auto &C : Chains) {
    NodeChain *Chain = C.second.get();
    Chain->Score = ExtTSPScore(Chain);
    for (const ELFCfgNode *Node : Chain->Nodes) {
      for (const ELFCfgEdge *Edge : Node->Outs) {
        if (Edge->Weight != 0) {
          NodeChain *OtherChain = NodeToChainMap.at(Edge->Sink);
          if (Chain != OtherChain) {
            bool CO = UpdateNodeChainAssembly(Chain, OtherChain);
            bool OC = UpdateNodeChainAssembly(OtherChain, Chain);
            if (CO || OC) {
              CandidateChains[Chain].insert(OtherChain);
              CandidateChains[OtherChain].insert(Chain);
            }
          }
        }
      }
    }
  }

  bool Merged;
  do {
    Merged = false;
    auto BestCandidate = std::max_element(
        NodeChainAssemblies.begin(), NodeChainAssemblies.end(),
        [](std::pair<const std::pair<NodeChain *, NodeChain *>,
                     unique_ptr<NodeChainAssembly>> &C1,
           std::pair<const std::pair<NodeChain *, NodeChain *>,
                     unique_ptr<NodeChainAssembly>> &C2) {
          return C1.second->GetExtTSPGain() < C2.second->GetExtTSPGain();
        });

    if (BestCandidate != NodeChainAssemblies.end() &&
        BestCandidate->second->GetExtTSPGain() > 0) {
      unique_ptr<NodeChainAssembly> BestCandidateNCA =
          std::move(BestCandidate->second);
      /*
      fprintf(stderr, "Best candidate was merging the following chains: %f\n", BestCandidateNCA->GetExtTSPGain());
      for(auto& S: BestCandidateNCA->Slices){
        for(auto SI=S.Begin, SE=S.End; SI!=SE; ++SI)
          fprintf(stderr, "%s -> ", (*SI)->ShName.str().c_str());
        fprintf(stderr, "\n");
      }
      */
      NodeChainAssemblies.erase(BestCandidate);
      MergeChains(std::move(BestCandidateNCA));
      Merged = true;
    }
  } while (Merged);

  AttachFallThroughs();
  /*
  for(auto& C: Chains){
    C.second->Dump();
  }
  */
  SortChainsByExecutionDensity(ChainOrder);
  if (opts::PrintStats)
    PrintStats();
}

double ExtTSPChainBuilder::NodeChainAssembly::ExtTSPScore() const {
  double Score = 0;
  for (uint8_t SrcSliceIdx = 0; SrcSliceIdx < 3; ++SrcSliceIdx) {
    const NodeChainSlice &SrcSlice = Slices[SrcSliceIdx];
    uint32_t SrcNodeOffset = SrcSlice.BeginOffset;
    for (auto NodeIt = SrcSlice.Begin; NodeIt != SrcSlice.End;
         SrcNodeOffset += (*NodeIt)->ShSize, ++NodeIt) {
      const ELFCfgNode *Node = *NodeIt;
      for (const ELFCfgEdge *Edge : Node->Outs) {
        if (Edge->Weight == 0)
          continue;

        uint8_t SinkSliceIdx;

        if (FindSliceIndex(Edge->Sink, SinkSliceIdx)) {
          auto SinkNodeOffset = ChainBuilder->NodeOffset.at(Edge->Sink);
          bool EdgeForward =
              (SrcSliceIdx < SinkSliceIdx) ||
              (SrcSliceIdx == SinkSliceIdx && SrcNodeOffset < SinkNodeOffset);

          uint32_t Distance = 0;

          if (SrcSliceIdx == SinkSliceIdx) {
            Distance = EdgeForward
                           ? SinkNodeOffset - SrcNodeOffset - Node->ShSize
                           : SrcNodeOffset - SinkNodeOffset + Node->ShSize;
          } else {
            const NodeChainSlice &SinkSlice = Slices[SinkSliceIdx];
            Distance = EdgeForward
                           ? SrcSlice.EndOffset - SrcNodeOffset - Node->ShSize +
                                 SinkNodeOffset - SinkSlice.BeginOffset
                           : SrcNodeOffset - SrcSlice.BeginOffset +
                                 Node->ShSize + SinkSlice.EndOffset -
                                 SinkNodeOffset;
            if (std::abs(SinkSliceIdx - SrcSliceIdx) == 2)
              Distance += Slices[1].Size();
          }
          Score += GetEdgeExtTSPScore(Edge, EdgeForward, Distance);
        }
      }
    }
  }
  return Score;
}

void NodeChainBuilder::doSplitOrder(list<StringRef> &SymbolList,
                                    list<StringRef>::iterator HotPlaceHolder,
                                    list<StringRef>::iterator ColdPlaceHolder,
                                    bool AlignBasicBlocks,
                                    StringMap<unsigned>& SymbolAlignmentMap) {

  vector<const NodeChain *> ChainOrder;
  ComputeChainOrder(ChainOrder);

  std::unordered_map<const ELFCfgNode *, unsigned> Address;
  unsigned CurrentAddress = 0;
  for (const NodeChain *C : ChainOrder) {
    list<StringRef>::iterator InsertPos =
        C->Freq ? HotPlaceHolder : ColdPlaceHolder;
    for (const ELFCfgNode *N : C->Nodes){
      SymbolList.insert(InsertPos, N->ShName);
      if (C->Freq){
        Address[N]=CurrentAddress;
        CurrentAddress+=N->ShSize;
      }
    }
  }

  if(AlignBasicBlocks) {

    enum VisitStatus {
      NONE = 0,
      DURING,
      FINISHED };

    std::unordered_map<const ELFCfgNode *, uint64_t> BackEdgeFreq;
    std::unordered_map<const ELFCfgNode *, unsigned> MinLoopSize;
    std::unordered_map<const ELFCfgNode *, unsigned> MaxLoopSize;
    std::unordered_map<const ELFCfgNode *, VisitStatus> Visited;

    std::function<void(const ELFCfgNode *)> visit;
    visit = [&Address, &Visited, &BackEdgeFreq, &MinLoopSize, &MaxLoopSize, &visit](const ELFCfgNode * N){
      if (Visited[N]!=NONE)
        return;
      if (!N->Freq)
        return;
      Visited[N] = DURING;
      if(N->FTEdge)
        visit(N->FTEdge->Sink);
      for(const ELFCfgEdge * E: N->Outs){
        if(E->Sink->Freq && Address[E->Sink] > Address[N])
          visit(E->Sink);
      }
      for(const ELFCfgEdge * E: N->Outs){
        if(E->Sink->Freq && Address[E->Sink] <= Address[N]){
          if(Visited[E->Sink]==DURING){
            BackEdgeFreq[E->Sink]+=E->Weight;
            MaxLoopSize[E->Sink]=std::max(MaxLoopSize[E->Sink], Address[N] - Address[E->Sink]);
            MinLoopSize[E->Sink]=std::min(MinLoopSize[E->Sink], Address[N] - Address[E->Sink]);
          }
        }
      }
      Visited[N] = FINISHED;
    };

    for(const NodeChain *C: ChainOrder)
      if (C->Freq != 0){
        for (const ELFCfgNode *N : C->Nodes)
          visit(N);
      }

    for(auto &N: Cfg->Nodes){
      if(N.get() == Cfg->getEntryNode())
        continue;
      if(N->Freq && (N->Freq >= 10 * Cfg->getEntryNode()->Freq) && (BackEdgeFreq[N.get()] * 5 >= N->Freq * 4)){
        //fprintf(stderr, "Loop alignment for node: %s\n", N->ShName.str().c_str());
        SymbolAlignmentMap.insert(std::make_pair(N->ShName, 16));
      } else
        SymbolAlignmentMap.insert(std::make_pair(N->ShName, 1));
    }
  }
}

void NodeChainBuilder::BuildPathCovers() {
  unordered_map<const ELFCfgNode *, const ELFCfgNode *> PathCoverInv;
  unordered_map<const ELFCfgNode *, const ELFCfgEdge *> PathCoverEdge;

  unordered_map<const ELFCfgNode *, int64_t> AugmentPathDistance;
  unordered_map<const ELFCfgNode *, const ELFCfgNode *> AugmentPathParent;
  vector<const ELFCfgNode *> HotNodes;

  for (auto &N : Cfg->Nodes) {
    if (N->Freq != 0)
      HotNodes.push_back(N.get());
  }

  for (auto *N : HotNodes) {
    PathCoverInv[N] = nullptr;
    PathCoverEdge[N] = nullptr;
  }

  bool PathCoverUpdated = true;
  while (PathCoverUpdated) {

    set<const ELFCfgNode *> FreeOutNodes;
    set<const ELFCfgNode *> FreeInNodes;

    for (auto *N : HotNodes) {
      AugmentPathDistance[N] = 0;
      AugmentPathParent[N] = nullptr;
      if (PathCoverEdge[N] == nullptr)
        FreeOutNodes.insert(N);
      if (PathCoverInv[N] == nullptr)
        FreeInNodes.insert(N);
    }

    set<const ELFCfgNode *> ChangedNodes;
    deque<const ELFCfgNode *> ChangedNodesQueue;

    for (auto *N : FreeOutNodes) {
      for (auto *E : N->Outs) {
        if (E->Sink == E->Src)
          continue;
        if (E->Weight && E->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
            AugmentPathDistance[E->Sink] > NEGATE(E->Weight)) {
          // std::cerr << "AugmentPath: " << *E->Src << " TO " << *E->Sink <<
          // "\t" << E->Weight << "\n";
          AugmentPathDistance[E->Sink] = NEGATE(E->Weight);
          AugmentPathParent[E->Sink] = E->Src;
          if (ChangedNodes.insert(E->Sink).second)
            ChangedNodesQueue.push_back(E->Sink);
        }
      }
    }

    while (!ChangedNodesQueue.empty()) {
      const ELFCfgNode *CN = ChangedNodesQueue.front();
      // std::cerr << "Changed Node is " << *CN << "\n";
      ChangedNodesQueue.pop_front();
      ChangedNodes.erase(CN);

      auto *PrevNode = PathCoverInv[CN];
      if (PrevNode != nullptr) {
        // std::cerr << "\t\tAnd its PathCoverInv is " << *PathCoverIt->second
        // << "\n";
        int64_t DistanceWithPathCoverEdge =
            AugmentPathDistance[CN] + PathCoverEdge[PrevNode]->Weight;
        for (const ELFCfgEdge *E : PrevNode->Outs) {
          if (E->Sink == E->Src)
            continue;
          if (E->Weight && E->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
              AugmentPathDistance[E->Sink] >
                  NEGATE(E->Weight) + DistanceWithPathCoverEdge) {
            // std::cerr << "Found better path for: " << *E->Sink << "\t Via "
            // << *E->Src << "\t " << NEGATE(E->Weight) +
            // DistanceWithPathCoverEdge << "\n";
            AugmentPathDistance[E->Sink] =
                NEGATE(E->Weight) + DistanceWithPathCoverEdge;
            AugmentPathParent[E->Sink] = PrevNode;
            if (ChangedNodes.insert(E->Sink).second)
              ChangedNodesQueue.push_back(E->Sink);
          }
        }
      }
    }

    auto ClosestFreeInNode = std::min_element(
        FreeInNodes.begin(), FreeInNodes.end(),
        [&AugmentPathDistance](const ELFCfgNode *N1, const ELFCfgNode *N2) {
          return AugmentPathDistance[N1] < AugmentPathDistance[N2];
        });
    /*std::cerr << "[BEFORE] Path Cover is as follows{\n";
      for(auto& KV: PathCover){
        std::cerr << *KV.first << " -> " << *KV.second << " [" <<
      PathCoverWeight[KV.first] << "]\n";
      }
      std::cerr << "}\n";
      */

    if (ClosestFreeInNode != FreeInNodes.end() &&
        AugmentPathDistance[*ClosestFreeInNode] != 0) {
      const ELFCfgNode *Node = *ClosestFreeInNode;
      while (Node != nullptr) {
        const ELFCfgNode *NodeParent = AugmentPathParent[Node];
        // std::cerr << *NodeParent << " ---> " << *Node << "\t";
        auto *NodeParentOldPCEdge = PathCoverEdge[NodeParent];
        bool EdgeFound = false;
        for (auto *E : NodeParent->Outs)
          if (E->Weight != 0 && E->Type == ELFCfgEdge::EdgeType::INTRA_FUNC &&
              E->Sink == Node) {
            PathCoverEdge[NodeParent] = E;
            EdgeFound = true;
            break;
          }
        assert(EdgeFound && "Edge wasn't found\n");
        PathCoverInv[Node] = NodeParent;
        Node = (NodeParentOldPCEdge != nullptr) ? NodeParentOldPCEdge->Sink
                                                : nullptr;
      }
    } else
      PathCoverUpdated = false;

    /*
    std::cerr << "[AFTER] Path Cover is as follows{\n";
  for(auto& KV: PathCover){
    std::cerr << *KV.first << " -> " << *KV.second << " [" <<
  PathCoverWeight[KV.first] << "]\n";
  }
  std::cerr << "}\n";
  */
  }

  /*
  std::cerr << "Function: " << Cfg->Name.str() << ", Path Cover is as
  follows{\n"; for(auto& KV: PathCoverEdge){ if(KV.second!=nullptr) std::cerr <<
  *KV.first << " -> " << *KV.second << "\n";
  }
  */

  unordered_map<const ELFCfgNode *, unsigned> PathCoverIndex;
  std::map<unsigned, const ELFCfgEdge *> MinCutEdge;
  unsigned Index = 0;
  for (auto &KV : PathCoverEdge) {
    if (!PathCoverIndex[KV.first]) {
      auto *N = KV.first;

      while (PathCoverInv[N] != nullptr && PathCoverInv[N] != KV.first) {
        N = PathCoverInv[N];
      }

      Index++;
      const ELFCfgEdge *MinEdge = nullptr;
      while (!PathCoverIndex[N]) {
        PathCoverIndex[N] = Index;
        if (PathCoverEdge[N] != nullptr) {
          if (MinEdge == nullptr || PathCoverEdge[N]->Weight < MinEdge->Weight)
            MinEdge = PathCoverEdge[N];
          N = PathCoverEdge[N]->Sink;
        } else {
          MinEdge = nullptr;
          break;
        }
      }
      MinCutEdge[Index] = MinEdge;
    }
  }

  /*
  for(auto& KV: MinCutEdge){
    if(KV.second!=nullptr)
      std::cerr << "Min cut edge for cycle cover(" << KV.first << ") is: " <<
  *KV.second << "\n";
  }
  */

  std::list<std::pair<const ELFCfgEdge *, int64_t>> GainVector;

  for (auto *N : HotNodes) {
    for (auto *E : N->Outs) {
      if (E->Weight == 0 || E->Type != ELFCfgEdge::EdgeType::INTRA_FUNC)
        continue;
      if (PathCoverIndex[E->Src] == PathCoverIndex[E->Sink])
        continue;
      if (MinCutEdge[PathCoverIndex[E->Src]] == nullptr &&
          MinCutEdge[PathCoverIndex[E->Sink]] == nullptr)
        continue;

      int64_t Gain = E->Weight;

      if (PathCoverEdge[E->Src] != nullptr)
        Gain -= PathCoverEdge[E->Src]->Weight;
      if (PathCoverInv[E->Sink] != nullptr)
        Gain -= PathCoverEdge[PathCoverInv[E->Sink]]->Weight;

      if (MinCutEdge[PathCoverIndex[E->Src]] != nullptr)
        Gain += MinCutEdge[PathCoverIndex[E->Src]]->Weight;

      if (MinCutEdge[PathCoverIndex[E->Sink]] != nullptr)
        Gain += MinCutEdge[PathCoverIndex[E->Sink]]->Weight;

      GainVector.push_back(std::make_pair(E, Gain));
    }
  }

  while (true) {
    auto BestGain =
        std::max_element(GainVector.begin(), GainVector.end(),
                         [](const std::pair<const ELFCfgEdge *, int64_t> &P1,
                            const std::pair<const ELFCfgEdge *, int64_t> &P2) {
                           return P1.second < P2.second;
                         });

    if (BestGain != GainVector.end() && BestGain->second > 0) {
      // std::cerr << "One gain: " << *BestGain->first << " ----> " <<
      // BestGain->second << "\t";
      auto *E = BestGain->first;
      if (MinCutEdge[PathCoverIndex[E->Src]] == nullptr &&
          MinCutEdge[PathCoverIndex[E->Sink]] == nullptr) {
        // std::cerr << "NOT APPLIED\n";
        continue;
      }
      // std::cerr << "APPLIED\n";

      if (PathCoverEdge[E->Src] != nullptr)
        PathCoverInv[PathCoverEdge[E->Src]->Sink] = nullptr;
      if (PathCoverInv[E->Sink] != nullptr)
        PathCoverEdge[PathCoverInv[E->Sink]] = nullptr;
      PathCoverEdge[E->Src] = E;
      PathCoverInv[E->Sink] = E->Src;

      MinCutEdge[PathCoverIndex[E->Src]] = nullptr;
      MinCutEdge[PathCoverIndex[E->Sink]] = nullptr;

      for (auto PI = GainVector.begin(); PI != GainVector.end();) {
        auto *F = PI->first;
        if (MinCutEdge[PathCoverIndex[F->Src]] == nullptr &&
            MinCutEdge[PathCoverIndex[F->Sink]] == nullptr) {
          PI = GainVector.erase(PI);
          continue;
        }
        int64_t Gain = F->Weight;
        if (PathCoverEdge[F->Src] != nullptr)
          Gain -= PathCoverEdge[F->Src]->Weight;
        if (PathCoverInv[F->Sink] != nullptr)
          Gain -= PathCoverEdge[PathCoverInv[F->Sink]]->Weight;

        if (MinCutEdge[PathCoverIndex[F->Src]] != nullptr)
          Gain += MinCutEdge[PathCoverIndex[F->Src]]->Weight;

        if (MinCutEdge[PathCoverIndex[F->Sink]] != nullptr)
          Gain += MinCutEdge[PathCoverIndex[F->Sink]]->Weight;

        PI->second = Gain;
        PI++;
      }

    } else
      break;
  }

  for (auto &KV : MinCutEdge) {
    auto *E = KV.second;
    if (E != nullptr) {
      PathCoverEdge[E->Src] = nullptr;
      PathCoverInv[E->Sink] = nullptr;
    }
  }

  /*
  std::cerr << "Function: " << Cfg->Name.str() << ", Path Cover is as
  follows{\n"; for(auto& KV: PathCoverEdge){ if(KV.second!=nullptr) std::cerr <<
  *KV.first << " -> " << *KV.second << "\n";
  }
  std::cerr << "}\n";
  */

  for (auto &KV : PathCoverEdge) {
    if (KV.second != nullptr)
      AttachNodes(KV.second->Src, KV.second->Sink);
  }
}

void ExtTSPChainBuilder::PrintStats() const {
  NodeChainBuilder::PrintStats();
  double TotalScore = std::accumulate(
      Chains.begin(), Chains.end(), 0.0,
      [this](double D,
             const std::pair<const uint64_t, unique_ptr<NodeChain>> &C) {
        return ExtTSPScore(C.second.get()) + D;
      });
  if (TotalScore != 0)
    std::cerr << "Ext TSP Score: " << TotalScore << "\t" << Cfg->Name.str()
              << "\n";
}

void NodeChainBuilder::PrintStats() const {
  uint32_t FTs = 0;
  for (auto &N : Cfg->Nodes)
    for (auto *E : N->Outs) {
      if (E->Weight && (E->Type == ELFCfgEdge::EdgeType::INTRA_FUNC ||
                        E->Type == ELFCfgEdge::EdgeType::INTRA_DYNA)) {
        if (NodeOffset.at(E->Src) + E->Src->ShSize == NodeOffset.at(E->Sink)) {
          FTs += E->Weight;
          std::cerr << "fallthrough:\tFROM: " << E->Src->ShName.str()
                    << " TO: " << E->Sink->ShName.str() << "(" << E->Weight
                    << ")\n";
        }
      }
    }

  if (FTs != 0)
    std::cerr << "fallthrough: " << FTs << "\t" << Cfg->Name.str() << "\n";
}

} // namespace propeller
} // namespace lld
