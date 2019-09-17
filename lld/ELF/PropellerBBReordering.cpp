#include "PropellerBBReordering.h"

#include "llvm/Support/CommandLine.h"
#include "Config.h"

#include <stdio.h>

#include <deque>
#include <iostream>
#include <numeric>
#include <set>
#include <unordered_map>
#include <vector>

using lld::elf::config;

using std::deque;
using std::list;
using std::set;
using std::unique_ptr;
using std::unordered_map;
using std::vector;
using namespace llvm;

namespace lld {
namespace propeller {

double GetEdgeExtTSPScore(const ELFCfgEdge *Edge, bool IsEdgeForward,
                          uint32_t SrcSinkDistance) {
  if (Edge->Weight == 0)
    return 0;

  if (SrcSinkDistance == 0 && (Edge->Type == ELFCfgEdge::EdgeType::INTRA_FUNC))
    return Edge->Weight * config->propellerFallthroughWeight;

  if (IsEdgeForward && SrcSinkDistance < config->propellerForwardJumpDistance)
    return Edge->Weight * config->propellerForwardJumpWeight *
           (1.0 - ((double)SrcSinkDistance) / config->propellerForwardJumpDistance);

  if (!IsEdgeForward && SrcSinkDistance < config->propellerBackwardJumpDistance)
    return Edge->Weight * config->propellerBackwardJumpWeight *
           (1.0 - ((double)SrcSinkDistance) / config->propellerBackwardJumpDistance);
  return 0;
}

void NodeChainBuilder::SortChainsByExecutionDensity(
    vector<const NodeChain *> &ChainOrder) {
  for (auto CI = Chains.cbegin(), CE = Chains.cend(); CI != CE; ++CI) {
    ChainOrder.push_back(CI->second.get());
  }

  std::sort(
      ChainOrder.begin(), ChainOrder.end(),
      [this](const NodeChain *C1, const NodeChain *C2) {
        if (C1->GetFirstNode() == this->Cfg->getEntryNode())
          return true;
        if (C2->GetFirstNode() == this->Cfg->getEntryNode())
          return false;
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
  if (Sink == Cfg->getEntryNode())
    return false;
  if (Src->Freq == 0 ^ Sink->Freq == 0)
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

void NodeChainBuilder::MergeChains(std::unique_ptr<NodeChainAssembly> A) {
  /* Merge the Node sequences according to a given NodeChainAssembly.*/
  list<const ELFCfgNode*> NewNodeOrder;
  for (NodeChainSlice &Slice : A->Slices){
    std::copy(Slice.Begin, Slice.End, std::back_inserter(NewNodeOrder));
  }
  A->SplitChain->Nodes = std::move(NewNodeOrder);

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
double NodeChainBuilder::ExtTSPScore(NodeChain *Chain) const {
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
bool NodeChainBuilder::UpdateNodeChainAssembly(NodeChain *SplitChain,
                                                 NodeChain *UnsplitChain) {
  /* Only split the chain if the size of the chain is smaller than a treshold.*/
  bool DoSplit = (SplitChain->Size <= config->propellerChainSplitThreshold);
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
      if ((NCA->SplitChain->GetFirstNode() == EntryNode ||
           NCA->UnsplitChain->GetFirstNode() == EntryNode) &&
          NCA->GetFirstNode() != EntryNode)
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

void NodeChainBuilder::initNodeChains() {
  for (auto &Node : Cfg->Nodes) {
    NodeChain *Chain = new NodeChain(Node.get());
    NodeToChainMap[Node.get()] = Chain;
    NodeOffset[Node.get()] = 0;
    Chains.emplace(std::piecewise_construct, std::forward_as_tuple(Node->Shndx),
                   std::forward_as_tuple(Chain));
  }
}

void NodeChainBuilder::initMutuallyForcedEdges() {
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

void NodeChainBuilder::ComputeChainOrder(
    vector<const NodeChain *> &ChainOrder) {
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
      NodeChainAssemblies.erase(BestCandidate);
      MergeChains(std::move(BestCandidateNCA));
      Merged = true;
    }
  } while (Merged);

  AttachFallThroughs();

  SortChainsByExecutionDensity(ChainOrder);
}

double NodeChainBuilder::NodeChainAssembly::ExtTSPScore() const {
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
        SymbolAlignmentMap.insert(std::make_pair(N->ShName, 16));
      } else
        SymbolAlignmentMap.insert(std::make_pair(N->ShName, 1));
    }
  }
}

} // namespace propeller
} // namespace lld
