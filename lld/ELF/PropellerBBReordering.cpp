//===- PropellerBBReordering.cpp  -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the Propeller infrastcture for doing code layout
// optimization and includes the implementation of intra-function basic block
// reordering algorithm based on the Extended TSP metric
// (https://arxiv.org/abs/1809.04676).
//
// The Extend TSP metric (ExtTSP) provides a score for every ordering of basic
// blocks in a function, by combining the gains from fall-throughs and short
// jumps.
//
// Given an ordering of the basic blocks, for a function f, the ExtTSP score is
// computed as follows.
//
// sum_{edges e in f} frequency(e) * weight(e)
//
// where frequency(e) is the execution frequency and weight(e) is computed as
// follows:
//   *  1 if distance(Src[e], Sink[e]) = 0 (i.e. fallthrough)
//   *  0.1 * (1 - distance(Src[e], Sink[e]) / 1024) if Src[e] < Sink[e] and
//   0 < distance(Src[e], Sink[e]) < 1024 (i.e. short forward jump)
//   *  0.1 * (1 - distance(Src[e], Sink[e]) / 640) if Src[e] > Sink[e] and
//   0 < distance(Src[e], Sink[e]) < 640 (i.e. short backward jump)
//   *  0 otherwise
//
// In short, it computes a weighted sum of frequencies of all edges in the
// control flow graph. Each edge gets its weight depending on whether the given
// ordering makes the edge a fallthrough, a short forward jump, or a short
// backward jump.
//
// Although this problem is NP-hard like the regular TSP, an iterative greedy
// basic-block-chaining algorithm is used to find a close to optimal solution.
// This algorithm is described as follows.
//
// Starting with one basic block sequence (BB chain) for every basic block, the
// algorithm iteratively joins BB chains together in order to maximize the
// extended TSP score of all the chains.
//
// Initially, it finds all mutually-forced edges in the profiled CFG. These are
// all the edges which are -- based on the profile -- the only (executed)
// outgoing edge from their source node and the only (executed) incoming edges
// to their sink nodes. Next, the source and sink of all mutually-forced edges
// are attached together as fallthrough edges.
//
// Then, at every iteration, the algorithm tries to merge a pair of BB chains
// which leads to the highest gain in the ExtTSP score. The algorithm extends
// the search space by considering splitting short (less than 128 bytes in
// binary size) BB chains into two chains and then merging these two chains with
// the other chain in four ways. After every merge, the new merge gains are
// updated. The algorithm repeats joining BB chains until no additional can be
// achieved. Eventually, it sorts all the existing chains in decreasing order of
// their execution density, i.e., the total profiled frequency of the chain
// divided by its binary size.
//
// The values used by this algorithm are reconfiguriable using lld's propeller
// flags. Specifically, these parameters are:
//    * propeller-forward-jump-distance: maximum distance of a forward jump
//    (default-set to 1024 in the above equation).
//    * propeller-backward-jump-distance: maximum distance of a backward jump
//    (default-set to 640 in the above equation).
//    * propeller-fallthrough-weight: weight of a fallthrough (default-set to 1)
//    * propeller-forward-jump-weight: weight of a forward jump (default-set to
//    0.1)
//    * propeller-backward-jump-weight: weight of a backward jump (default-set
//    to 0.1)
//    * propeller-chain-split-threshold: maximum binary size of a BB chain
//    which the algorithm will consider for splitting (default-set to 128).
//===----------------------------------------------------------------------===//
#include "PropellerBBReordering.h"

#include "Config.h"
#include "llvm/ADT/DenseSet.h"

#include <stdio.h>

#include <numeric>
#include <vector>

using lld::elf::config;

using llvm::DenseSet;
using llvm::detail::DenseMapPair;

namespace lld {
namespace propeller {

ostream &operator<<(ostream &Out, const NodeChain &Chain) {
  Out << "Chain for CFG Node: {" << *(Chain.DelegateNode) << "}: total binary size: " << Chain.Size
      << ", total frequency: " << Chain.Freq << "\n";
  Out << " Nodes: " ;

  for (const ELFCFGNode *N : Chain.Nodes)
    Out << *N << " ---> " ;
  return Out;
}

double GetEdgeExtTSPScore(const ELFCFGEdge *Edge, bool IsEdgeForward,
                          uint32_t SrcSinkDistance) {
  if (Edge->Weight == 0)
    return 0;

  if (SrcSinkDistance == 0 && (Edge->Type == ELFCFGEdge::EdgeType::INTRA_FUNC))
    return Edge->Weight * config->propellerFallthroughWeight;

  if (isEdgeForward && srcSinkDistance < config->propellerForwardJumpDistance)
    return edge->Weight * config->propellerForwardJumpWeight *
           (1.0 -
            ((double)srcSinkDistance) / config->propellerForwardJumpDistance);

  if (!isEdgeForward && srcSinkDistance < config->propellerBackwardJumpDistance)
    return edge->Weight * config->propellerBackwardJumpWeight *
           (1.0 -
            ((double)srcSinkDistance) / config->propellerBackwardJumpDistance);
  return 0;
}

// Sort BB chains in decreasing order of their execution density.
// NodeChainBuilder calls this function at the end to ensure that hot BB chains
// are placed at the beginning of the function.
void NodeChainBuilder::sortChainsByExecutionDensity(
    std::vector<const NodeChain *> &chainOrder) {
  for (auto CI = Chains.begin(), CE = Chains.end(); CI != CE; ++CI) {
    chainOrder.push_back(CI->second.get());
  }

  std::sort(
      ChainOrder.begin(), ChainOrder.end(),
      [this](const NodeChain *C1, const NodeChain *C2) {
        if (C1->GetFirstNode() == this->CFG->getEntryNode())
          return true;
        if (C2->GetFirstNode() == this->CFG->getEntryNode())
          return false;
        double c1ExecDensity = c1->execDensity();
        double c2ExecDensity = c2->execDensity();
        if (c1ExecDensity == c2ExecDensity) {
          if (c1->DelegateNode->MappedAddr == c2->DelegateNode->MappedAddr)
            return c1->DelegateNode->Shndx < c2->DelegateNode->Shndx;
          return c1->DelegateNode->MappedAddr < c2->DelegateNode->MappedAddr;
        }
        return c1ExecDensity > c2ExecDensity;
      });
}

void NodeChainBuilder::AttachFallThroughs() {
  for (auto &Node : CFG->Nodes) {
    if (Node->FTEdge != nullptr) {
      AttachNodes(Node.get(), Node->FTEdge->Sink);
    }
  }

  for (auto &Edge : CFG->IntraEdges) {
    AttachNodes(Edge->Src, Edge->Sink);
  }
}

/* Merge two chains in the specified order. */
void NodeChainBuilder::MergeChains(NodeChain *LeftChain,
                                   NodeChain *RightChain) {
  for (const ELFCFGNode *Node : RightChain->Nodes) {
    LeftChain->Nodes.push_back(Node);
    NodeToChainMap[Node] = LeftChain;
    NodeOffset[Node] += LeftChain->Size;
  }
  leftChain->Size += rightChain->Size;
  leftChain->Freq += rightChain->Freq;
  Chains.erase(rightChain->DelegateNode->Shndx);
}

/* This function tries to place two nodes immediately adjacent to
 * each other (used for fallthroughs).
 * Returns true if this can be done. */
bool NodeChainBuilder::AttachNodes(const ELFCFGNode *Src,
                                   const ELFCFGNode *Sink) {
  if (Sink == CFG->getEntryNode())
    return false;

  // Ignore edges between hot and cold basic blocks.
  if (src->Freq == 0 ^ sink->Freq == 0)
    return false;
  NodeChain *srcChain = getNodeChain(src);
  NodeChain *sinkChain = getNodeChain(sink);
  // Skip this edge if the source and sink are in the same chain
  if (srcChain == sinkChain)
    return false;

  // It's only possible to form a fall-through between src and sink if src is
  // they are respectively located at the end and beginning of their chains.
  if (srcChain->getLastNode() != src || sinkChain->getFirstNode() != sink)
    return false;
  // Attaching is possible. So we merge the chains in the corresponding order.
  mergeChains(srcChain, sinkChain);
  return true;
}

void NodeChainBuilder::MergeChains(std::unique_ptr<NodeChainAssembly> A) {
  /* Merge the Node sequences according to the given NodeChainAssembly.*/
  list<const ELFCFGNode*> NewNodeOrder;
  for (NodeChainSlice &Slice : A->Slices){
    std::copy(Slice.Begin, Slice.End, std::back_inserter(NewNodeOrder));
  }
  A->SplitChain->Nodes = std::move(NewNodeOrder);

  /* Update NodeOffset and NodeToChainMap for all the nodes in the sequence */
  uint32_t RunningOffset = 0;
  for (const ELFCFGNode *Node : A->SplitChain->Nodes) {
    NodeToChainMap[Node] = A->SplitChain;
    NodeOffset[Node] = RunningOffset;
    RunningOffset += Node->ShSize;
  }
  assembly->SplitChain->Size = runningOffset;

  // Update the total frequency and ExtTSP score of the aggregated chain
  assembly->SplitChain->Freq += assembly->UnsplitChain->Freq;
  // We have already computed the gain in the assembly record. So we can just
  // increment the aggregated chain's score by that gain.
  assembly->SplitChain->Score += assembly->extTSPScoreGain();

  // Merge the assembly candidate chains of the two chains into the candidate
  // chains of the remaining NodeChain and remove the records for the defunct
  // NodeChain.
  for (NodeChain *c : CandidateChains[assembly->UnsplitChain]) {
    NodeChainAssemblies.erase(std::make_pair(c, assembly->UnsplitChain));
    NodeChainAssemblies.erase(std::make_pair(assembly->UnsplitChain, c));
    CandidateChains[c].erase(assembly->UnsplitChain);
    if (c != assembly->SplitChain)
      CandidateChains[assembly->SplitChain].insert(c);
  }

  // Update the NodeChainAssembly for all candidate chains of the merged
  // NodeChain. Remove a NodeChain from the merge chain's candidates if the
  // NodeChainAssembly update finds no gain.
  auto &splitChainCandidateChains = CandidateChains[assembly->SplitChain];

  for (auto CI = splitChainCandidateChains.begin(),
            CE = splitChainCandidateChains.end();
       CI != CE;) {
    NodeChain *otherChain = *CI;
    auto &otherChainCandidateChains = CandidateChains[otherChain];
    bool x = updateNodeChainAssembly(otherChain, assembly->SplitChain);
    bool y = updateNodeChainAssembly(assembly->SplitChain, otherChain);
    if (x || y) {
      otherChainCandidateChains.insert(assembly->SplitChain);
      CI++;
    } else {
      otherChainCandidateChains.erase(assembly->SplitChain);
      CI = splitChainCandidateChains.erase(CI);
    }
  }

  // remove all the candidate chain records for the merged-in chain
  CandidateChains.erase(assembly->UnsplitChain);

  // Finally, remove the defunct (merged-in) chain record from the chains
  Chains.erase(assembly->UnsplitChain->DelegateNode->Shndx);
}

/* Calculate the Extended TSP metric for a NodeChain */
double NodeChainBuilder::ExtTSPScore(NodeChain *Chain) const {
  double Score = 0;
  uint32_t SrcOffset = 0;
  for (const ELFCFGNode *Node : Chain->Nodes) {
    for (const ELFCFGEdge *Edge : Node->Outs) {
      if (!Edge->Weight)
        continue;
      NodeChain *sinkChain = getNodeChain(edge->Sink);
      if (sinkChain != chain)
        continue;
      auto sinkOffset = getNodeOffset(edge->Sink);
      bool edgeForward = srcOffset + node->ShSize <= sinkOffset;
      // Calculate the distance between src and sink
      uint32_t distance = edgeForward ? sinkOffset - srcOffset - node->ShSize
                                      : srcOffset - sinkOffset + node->ShSize;
      score += getEdgeExtTSPScore(edge, edgeForward, distance);
    }
    srcOffset += node->ShSize;
  }
  return score;
}

// Updates the best NodeChainAssembly between two NodeChains. The existing
// record will be replaced by the new NodeChainAssembly if a non-zero gain
// is achieved. Otherwise, it will be removed.
// If a nodechain assembly record is (kept) inserted, returns true. Otherwise
// returns false.
bool NodeChainBuilder::updateNodeChainAssembly(NodeChain *splitChain,
                                               NodeChain *unsplitChain) {
  // Remove the assembly record
  NodeChainAssemblies.erase(std::make_pair(splitChain, unsplitChain));

  // Only consider split the chain if the size of the chain is smaller than a
  // treshold.
  bool doSplit = (splitChain->Size <= config->propellerChainSplitThreshold);
  auto slicePosEnd =
      doSplit ? splitChain->Nodes.end() : std::next(splitChain->Nodes.begin());

  SmallVector<unique_ptr<NodeChainAssembly>, 128> candidateNCAs;

  for (auto slicePos = splitChain->Nodes.begin(); slicePos != slicePosEnd;
       ++slicePos) {
    // Do not split the mutually-forced edges in the chain.
    if (slicePos != splitChain->Nodes.begin() &&
        MutuallyForcedOut[*std::prev(slicePos)] == *slicePos)
      continue;

    // If the split position is at the beginning (no splitting), only consider
    // one MergeOrder
    auto mergeOrderEnd = (slicePos == splitChain->Nodes.begin())
                             ? MergeOrder::BeginNext
                             : MergeOrder::End;

    for (uint8_t MI = MergeOrder::Begin; MI != mergeOrderEnd; MI++) {
      MergeOrder mOrder = static_cast<MergeOrder>(MI);

      // Create the NodeChainAssembly representing this particular assembly.
      auto NCA = unique_ptr<NodeChainAssembly>(new NodeChainAssembly(
          splitChain, unsplitChain, slicePos, mOrder, this));

      ELFCFGNode *EntryNode = CFG->getEntryNode();
      if ((NCA->SplitChain->GetFirstNode() == EntryNode ||
           NCA->UnsplitChain->GetFirstNode() == EntryNode) &&
          NCA->GetFirstNode() != EntryNode)
        continue;
      candidateNCAs.push_back(std::move(NCA));
    }
  }

  // Insert the best assembly of the two chains (only if it has positive gain).
  auto bestCandidate = std::max_element(
      candidateNCAs.begin(), candidateNCAs.end(),
      [](unique_ptr<NodeChainAssembly> &c1, unique_ptr<NodeChainAssembly> &c2) {
        return c1->extTSPScoreGain() < c2->extTSPScoreGain();
      });

  if (bestCandidate != candidateNCAs.end() &&
      (*bestCandidate)->extTSPScoreGain() > 0) {
    NodeChainAssemblies.try_emplace(std::make_pair(splitChain, unsplitChain),
                                    std::move(*bestCandidate));
    return true;
  } else
    return false;
}

void NodeChainBuilder::initNodeChains() {
  for (auto &Node : CFG->Nodes) {
    NodeChain *Chain = new NodeChain(Node.get());
    NodeToChainMap[Node.get()] = Chain;
    NodeOffset[Node.get()] = 0;
    Chains.emplace(std::piecewise_construct,
                   std::forward_as_tuple(Node->Shndx),
                   std::forward_as_tuple(Chain));
  }
}

// Find all the mutually-forced edges.
// These are all the edges which are -- based on the profile -- the only
// (executed) outgoing edge from their source node and the only (executed)
// incoming edges to their sink nodes
void NodeChainBuilder::initMutuallyForcedEdges() {
  // Find all the mutually-forced edges.
  // These are all the edges which are -- based on the profile -- the only (executed) outgoing edge
  // from their source node and the only (executed) incoming edges to their sink nodes
  unordered_map<const ELFCFGNode *, vector<ELFCFGEdge *>> ProfiledOuts;
  unordered_map<const ELFCFGNode *, vector<ELFCFGEdge *>> ProfiledIns;

  for (auto &Node : CFG->Nodes) {
    std::copy_if(Node->Outs.begin(), Node->Outs.end(),
                 std::back_inserter(ProfiledOuts[Node.get()]),
                 [](const ELFCFGEdge *Edge) {
                   return Edge->Type == ELFCFGEdge::EdgeType::INTRA_FUNC &&
                          Edge->Weight != 0;
                 });
    std::copy_if(Node->Ins.begin(), Node->Ins.end(),
                 std::back_inserter(ProfiledIns[Node.get()]),
                 [](const ELFCFGEdge *Edge) {
                   return Edge->Type == ELFCFGEdge::EdgeType::INTRA_FUNC &&
                          Edge->Weight != 0;
                 });
  }

  for (auto &Node : CFG->Nodes) {
    if (ProfiledOuts[Node.get()].size() == 1) {
      ELFCFGEdge *Edge = ProfiledOuts[Node.get()].front();
      if (ProfiledIns[Edge->Sink].size() == 1)
        MutuallyForcedOut[Node.get()] = Edge->Sink;
    }
  }

  // Break cycles in the mutually forced edges by cutting the edge sinking to
  // the smallest address in every cycle (hopefully a loop backedge)
  map<const ELFCFGNode *, unsigned> NodeToCycleMap;
  set<const ELFCFGNode *> CycleCutNodes;
  unsigned CycleCount = 0;
  for (auto It = MutuallyForcedOut.begin(); It != MutuallyForcedOut.end();
       ++It) {
    // Check to see if the node (and its cycle) have already been visited.
    if (nodeToPathMap[it->first])
      continue;
    const ELFCFGEdge *VictimEdge = nullptr;
    auto NodeIt = It;
    CycleCount++;
    while (NodeIt != MutuallyForcedOut.end()) {
      const ELFCFGNode *Node = NodeIt->first;
      auto NodeCycle = NodeToCycleMap[Node];
      if (NodeCycle != 0) {
        // If this node is marked with a number, either it is the same number,
        // in which case we have found a cycle. Or it is a different number,
        // which means we have found a path to a previously visited path
        // (non-cycle).
        if (path == pathCount) {
          // We have found a cycle: add the victim edge
          cycleCutNodes.push_back(victimEdge->Src);
        }
        break;
      } else
        NodeToCycleMap[Node] = CycleCount;
      const ELFCFGEdge *Edge = ProfiledOuts[Node].front();
      if (!VictimEdge ||
          (Edge->Sink->MappedAddr < VictimEdge->Sink->MappedAddr)) {
        VictimEdge = Edge;
      }
      nodeIt = MutuallyForcedOut.find(nodeIt->second);
    }
  }

  // Remove the victim edges to break cycles
  for (const ELFCFGNode *Node : CycleCutNodes)
    MutuallyForcedOut.erase(Node);
}

// This function initializes the ExtTSP algorithm's data structures. This
// the NodeChainAssemblies and the CandidateChains maps.
void NodeChainBuilder::initializeExtTSP() {
  // For each chain, compute its ExtTSP score, add its chain assembly records
  // and its merge candidate chain.

  std::set <std::pair<NodeChain*, NodeChain*>> Visited;
  for (auto &C : Chains) {
    NodeChain *Chain = C.second.get();
    Chain->Score = ExtTSPScore(Chain);
    for (const ELFCFGNode *Node : Chain->Nodes) {
      for (const ELFCFGEdge *Edge : Node->Outs) {
        if (!Edge->Weight)
          continue;
        NodeChain *otherChain = getNodeChain(edge->Sink);
        if (chain == otherChain)
          continue;
        if (visited.count(std::make_pair(chain, otherChain)))
          continue;
        bool x = updateNodeChainAssembly(chain, otherChain);
        bool y = updateNodeChainAssembly(otherChain, chain);
        if (x || y) {
          CandidateChains[chain].insert(otherChain);
          CandidateChains[otherChain].insert(chain);
        }
        visited.insert(std::make_pair(chain, otherChain));
        visited.insert(std::make_pair(otherChain, chain));
      }
    }
  }
}

void NodeChainBuilder::computeChainOrder(
    std::vector<const NodeChain *> &chainOrder) {
  // Attach the mutually-foced edges (which will not be split anymore by the
  // Extended TSP algorithm).
  for (auto &kv : MutuallyForcedOut)
    attachNodes(kv.first, kv.second);

  // Initialize the Extended TSP algorithm's data.
  initializeExtTSP();

  // Keep merging the chain assembly record with the highest ExtTSP gain, until
  // no more gain is possible.
  bool merged = false;
  do {
    merged = false;
    auto bestCandidate = std::max_element(
        NodeChainAssemblies.begin(), NodeChainAssemblies.end(),
        [](DenseMapPair<std::pair<NodeChain *, NodeChain *>,
                        unique_ptr<NodeChainAssembly>> &c1,
           DenseMapPair<std::pair<NodeChain *, NodeChain *>,
                        unique_ptr<NodeChainAssembly>> &c2) {
          return c1.second->extTSPScoreGain() < c2.second->extTSPScoreGain();
        });

    if (bestCandidate != NodeChainAssemblies.end() &&
        bestCandidate->second->extTSPScoreGain() > 0) {
      unique_ptr<NodeChainAssembly> bestCandidateNCA =
          std::move(bestCandidate->second);
      NodeChainAssemblies.erase(bestCandidate);
      mergeChains(std::move(bestCandidateNCA));
      merged = true;
    }
  } while (merged);

  // Merge fallthrough basic blocks if we have missed any
  attachFallThroughs();

  sortChainsByExecutionDensity(chainOrder);
}


// This function computes the ExtTSP score for a chain assembly record.
double NodeChainBuilder::NodeChainAssembly::ExtTSPScore() const {
  double Score = 0;
  for (uint8_t SrcSliceIdx = 0; SrcSliceIdx < 3; ++SrcSliceIdx) {
    const NodeChainSlice &SrcSlice = Slices[SrcSliceIdx];
    uint32_t SrcNodeOffset = SrcSlice.BeginOffset;
    for (auto NodeIt = SrcSlice.Begin; NodeIt != SrcSlice.End;
         SrcNodeOffset += (*NodeIt)->ShSize, ++NodeIt) {
      const ELFCFGNode *Node = *NodeIt;
      for (const ELFCFGEdge *Edge : Node->Outs) {
        if (!Edge->Weight)
          continue;

        uint8_t sinkSliceIdx;

        if (findSliceIndex(edge->Sink, sinkSliceIdx)) {
          auto sinkNodeOffset = ChainBuilder->getNodeOffset(edge->Sink);
          bool edgeForward = (srcSliceIdx < sinkSliceIdx) ||
                             (srcSliceIdx == sinkSliceIdx &&
                              (srcNodeOffset + node->ShSize <= sinkNodeOffset));

          uint32_t distance = 0;

          if (srcSliceIdx == sinkSliceIdx) {
            distance = edgeForward
                           ? sinkNodeOffset - srcNodeOffset - node->ShSize
                           : srcNodeOffset - sinkNodeOffset + node->ShSize;
          } else {
            const NodeChainSlice &sinkSlice = Slices[sinkSliceIdx];
            distance = edgeForward
                           ? srcSlice.EndOffset - srcNodeOffset - node->ShSize +
                                 sinkNodeOffset - sinkSlice.BeginOffset
                           : srcNodeOffset - srcSlice.BeginOffset +
                                 node->ShSize + sinkSlice.EndOffset -
                                 sinkNodeOffset;
            // Increment the distance by the size of the middle slice if the src
            // and sink are from the two ends.
            if (std::abs(sinkSliceIdx - srcSliceIdx) == 2)
              distance += Slices[1].size();
          }
          score += getEdgeExtTSPScore(edge, edgeForward, distance);
        }
      }
    }
  }
  return score;
}

void NodeChainBuilder::doSplitOrder(list<StringRef> &SymbolList,
                                    list<StringRef>::iterator HotPlaceHolder,
                                    list<StringRef>::iterator ColdPlaceHolder) {

  vector<const NodeChain *> ChainOrder;
  ComputeChainOrder(ChainOrder);

  std::unordered_map<const ELFCFGNode *, unsigned> Address;
  unsigned CurrentAddress = 0;
  for (const NodeChain *C : ChainOrder) {
    list<StringRef>::iterator InsertPos =
        C->Freq ? HotPlaceHolder : ColdPlaceHolder;
    for (const ELFCFGNode *N : C->Nodes){
      SymbolList.insert(InsertPos, N->ShName);
      if (C->Freq){
        Address[N]=CurrentAddress;
        CurrentAddress+=N->ShSize;
      }
    }
  }

  if (config->propellerAlignBasicBlocks) {

    enum VisitStatus { NONE = 0, DURING, FINISHED };

    std::unordered_map<const ELFCFGNode *, uint64_t> BackEdgeFreq;
    std::unordered_map<const ELFCFGNode *, VisitStatus> Visited;

    std::function<void(const ELFCFGNode *)> visit;
    visit = [&Address, &Visited, &BackEdgeFreq, &visit](const ELFCFGNode * N){
      if (Visited[N]!=NONE)
        return;
      if (!n->Freq)
        return;
      Visited[N] = DURING;
      if(N->FTEdge)
        visit(N->FTEdge->Sink);
      for(const ELFCFGEdge * E: N->Outs){
        if(E->Sink->Freq && Address[E->Sink] > Address[N])
          visit(E->Sink);
      }
      for(const ELFCFGEdge * E: N->Outs){
        if(E->Sink->Freq && Address[E->Sink] <= Address[N]){
          if(Visited[E->Sink]==DURING){
            BackEdgeFreq[E->Sink]+=E->Weight;
          }
        }
      }
      visited[n] = FINISHED;
    };

    for(const NodeChain *C: ChainOrder)
      if (C->Freq != 0){
        for (const ELFCFGNode *N : C->Nodes)
          visit(N);
      }

    for(auto &N: CFG->Nodes){
      if(N.get() == CFG->getEntryNode())
        continue;
      if(N->Freq &&
         (N->Freq >= 10 * CFG->getEntryNode()->Freq) &&
         (BackEdgeFreq[N.get()] * 5 >= N->Freq * 4)){
        config->symbolAlignmentFile.insert(std::make_pair(N->ShName, 16));
      } else
        config->symbolAlignmentFile.insert(std::make_pair(n->ShName, 1));
    }
  }
}

} // namespace propeller
} // namespace lld
