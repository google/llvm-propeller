//===- NodeChainBuilder.cpp  ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file is part of the Propeller infrastructure for doing code layout
// optimization and includes the implementation of intra-function basic block
// reordering algorithm based on the Extended TSP metric as described in [1].
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
//  * 1 if distance(Src[e], Sink[e]) = 0 (i.e. fallthrough)
//
//  * 0.1 * (1 - distance(Src[e], Sink[e]) / 1024) if Src[e] < Sink[e] and 0 <
//    distance(Src[e], Sink[e]) < 1024 (i.e. short forward jump)
//
//  * 0.1 * (1 - distance(Src[e], Sink[e]) / 640) if Src[e] > Sink[e] and 0 <
//    distance(Src[e], Sink[e]) < 640 (i.e. short backward jump)
//
//  * 0 otherwise
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
// the search space by considering splitting short (less than 1KBs in
// binary size) BB chains into two chains and then merging these two chains with
// the other chain in four ways. After every merge, the new merge gains are
// updated. The algorithm repeats joining BB chains until no additional can be
// achieved. Eventually, it sorts all the existing chains in decreasing order
// of their execution density, i.e., the total profiled frequency of the chain
// divided by its binary size.
//
// The values used by this algorithm are reconfiguriable using lld's propeller
// flags. Specifically, these parameters are:
//
//   * propeller-forward-jump-distance: maximum distance of a forward jump
//     default-set to 1024 in the above equation).
//
//   * propeller-backward-jump-distance: maximum distance of a backward jump
//     (default-set to 640 in the above equation).
//
//   * propeller-fallthrough-weight: weight of a fallthrough (default-set to 1)
//
//   * propeller-forward-jump-weight: weight of a forward jump (default-set to
//     0.1)
//
//   * propeller-backward-jump-weight: weight of a backward jump (default-set to
//     0.1)
//
//   * propeller-chain-split-threshold: maximum binary size of a BB chain which
//     the algorithm will consider for splitting (default-set to 1KB).
//
// References:
//   * [1] A. Newell and S. Pupyrev, Improved Basic Block Reordering, available
//         at https://arxiv.org/abs/1809.04676
//===----------------------------------------------------------------------===//

#include "NodeChainBuilder.h"

#include "llvm/ADT/DenseSet.h"

using llvm::DenseSet;

using llvm::detail::DenseMapPair;

namespace lld {
namespace propeller {

// Initializes the node chains for each cfg and finds all the mutually-forced
// edges.
void NodeChainBuilder::init() {
  for (ControlFlowGraph *cfg : CFGs) {
    initNodeChains(*cfg);
    initMutuallyForcedEdges(*cfg);
  }
}

// NodeChainBuilder calls this function after building all the chains to attach
// as many fall-throughs as possible. Given that the algorithm already optimizes
// the extend TSP score, this function will only affect the cold basic blocks
// and thus we do not need to consider the edge weights.
void NodeChainBuilder::attachFallThroughs() {
  for (ControlFlowGraph *Cfg : CFGs) {
    // First, try to keep the fall-throughs from the original order.
    for (auto &Node : Cfg->Nodes) {
      if (Node->FTEdge != nullptr) {
        attachNodes(Node.get(), Node->FTEdge->Sink);
      }
    }

    // Sometimes, the original fall-throughs cannot be kept. So we try to find
    // new fall-through opportunities which did not exist in the original order.
    for (auto &Edge : Cfg->IntraEdges) {
      if (Edge->Type == CFGEdge::EdgeType::INTRA_FUNC ||
          Edge->Type == CFGEdge::EdgeType::INTRA_DYNA)
        attachNodes(Edge->Src, Edge->Sink);
    }
  }
}

// This function sorts BB chains in decreasing order of their execution density.
// NodeChainBuilder calls this function at the end to ensure that hot BB chains
// are placed at the beginning of the function.
void NodeChainBuilder::coalesceChains() {
  std::vector<NodeChain *> chainOrder;
  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : Chains)
    chainOrder.push_back(elem.second.get());

  std::sort(
      chainOrder.begin(), chainOrder.end(), [](NodeChain *c1, NodeChain *c2) {
        if (!c1->isSameCFG(*c2))
          error("Attempting to coalesce chains belonging to different "
                "functions.");
        // Always place the hot chains before cold ones
        if (c1->Freq == 0 ^ c2->Freq == 0)
          return c1->Freq != 0;

        // Place the entry node's chain before other chains
        auto *entryNode = c1->CFG->getEntryNode();
        if (entryNode->Chain == c1)
          return true;
        if (entryNode->Chain == c2)
          return false;

        // Sort chains in decreasing order of execution density, and break ties
        // according to the initial ordering.
        double c1ExecDensity = c1->execDensity();
        double c2ExecDensity = c2->execDensity();
        if (c1ExecDensity == c2ExecDensity)
          return c1->DelegateNode->MappedAddr < c2->DelegateNode->MappedAddr;
        return c1ExecDensity > c2ExecDensity;
      });

  // We will merge all chains into at most two chains; a hot and cold one.
  NodeChain *mergerChain = nullptr;

  for (NodeChain *c : chainOrder) {
    if (!mergerChain) {
      mergerChain = c;
      continue;
    }
    // Create a cold partition when -propeller-split-funcs is set.
    if (propellerConfig.optSplitFuncs &&
        (mergerChain->Freq != 0 && c->Freq == 0)) {
      mergerChain = c;
      continue;
    }
    // Merge this chain into the merger chain
    mergeChains(mergerChain, c);
  }
}

// Merge two chains in the specified order.
void NodeChainBuilder::mergeChains(NodeChain *leftChain,
                                   NodeChain *rightChain) {
  if (leftChain->Freq == 0 ^ rightChain->Freq == 0)
    error("Attempting to merge hot and cold chains: \n" + toString(*leftChain) +
          "\nAND\n" + toString(*rightChain));

  if (leftChain->DebugChain || rightChain->DebugChain)
    fprintf(stderr, "MERGING chains:\n%s\nAND%s\n",
                toString(*leftChain).c_str(),
                toString(*rightChain).c_str());

  mergeInOutEdges(leftChain, rightChain);

  auto rightChainBegin = rightChain->Nodes.begin();

  leftChain->Nodes.splice(leftChain->Nodes.end(), rightChain->Nodes);

  for (auto it = rightChainBegin; it != leftChain->Nodes.end(); ++it) {
    (*it)->Chain = leftChain;
    (*it)->ChainOffset += leftChain->Size;
  }

  leftChain->Size += rightChain->Size;
  leftChain->Freq += rightChain->Freq;

  leftChain->DebugChain |= rightChain->DebugChain;
  if (leftChain->CFG && leftChain->CFG != rightChain->CFG)
    leftChain->CFG = nullptr;

  Chains.erase(rightChain->DelegateNode->MappedAddr);
}

// This function tries to place two basic blocks immediately adjacent to each
// other (used for fallthroughs). Returns true if the basic blocks have been
// attached this way.
bool NodeChainBuilder::attachNodes(CFGNode *src, CFGNode *sink) {
  if (sink->isEntryNode())
    return false;

  // Ignore edges between hot and cold basic blocks.
  if (src->Freq == 0 ^ sink->Freq == 0)
    return false;
  NodeChain *srcChain = src->Chain;
  NodeChain *sinkChain = sink->Chain;
  // Skip this edge if the source and sink are in the same chain
  if (srcChain == sinkChain)
    return false;

  // It's possible to form a fall-through between src and sink only if
  // they are respectively located at the end and beginning of their chains.
  if (srcChain->Nodes.back() != src || sinkChain->Nodes.front() != sink)
    return false;
  // Attaching is possible. So we merge the chains in the corresponding order.
  mergeChains(srcChain, sinkChain);
  return true;
}

// This function merges the in-and-out chain-edges of one chain (mergeeChain) into those of another (mergerChain).
void NodeChainBuilder::mergeInOutEdges(NodeChain *mergerChain,
                                       NodeChain *mergeeChain) {
  // Add out-edges of mergeeChain to the out-edges of mergerChain
  for (auto &elem : mergeeChain->OutEdges) {
    NodeChain *c = (elem.first == mergeeChain) ? mergerChain : elem.first;
    auto res = mergerChain->OutEdges.emplace(c, elem.second);
    // If the chain-edge is already present, just add the CFG edges, otherwise,
    // add mergerChain to in-edges of the chain.
    if (!res.second)
      res.first->second.insert(res.first->second.end(), elem.second.begin(),
                               elem.second.end());
    else
      c->InEdges.insert(mergerChain);

    // Remove mergeeChain from in-edges
    c->InEdges.erase(mergeeChain);
  }

  // Add in-edges of mergeeChain to in-edges of mergerChain
  for (auto *c : mergeeChain->InEdges) {
    // Self edges were already handled above.
    if (c == mergeeChain)
      continue;
    // Move all CFG edges from being mapped to mergeeChain to being mapped to
    // mergerChain.
    auto &mergeeChainEdges = c->OutEdges[mergeeChain];
    auto &mergerChainEdges = c->OutEdges[mergerChain];

    mergerChainEdges.insert(mergerChainEdges.end(), mergeeChainEdges.begin(),
                            mergeeChainEdges.end());
    mergerChain->InEdges.insert(c);
    // Remove the defunct chain from out edges
    c->OutEdges.erase(mergeeChain);
  }
}

// Merge two BB sequences according to the given NodeChainAssembly. A
// NodeChainAssembly is an ordered triple of three slices from two chains.
void NodeChainBuilder::mergeChains(
    std::unique_ptr<NodeChainAssembly> assembly) {
  if (assembly->splitChain()->Freq == 0 ^ assembly->unsplitChain()->Freq == 0)
    error("Attempting to merge hot and cold chains: \n" +
          toString(*assembly.get()));

  if (assembly->splitChain()->Freq == 0 ^ assembly->unsplitChain()->Freq == 0)
    error("Attempting to merge hot and cold chains: \n" +
          toString(*assembly.get()));

  // Decide which chain gets merged into the other chain, to make the reordering
  // more efficient.
  NodeChain *mergerChain = (assembly->MOrder == US2S1)
                               ? assembly->unsplitChain()
                               : assembly->splitChain();
  NodeChain *mergeeChain = (assembly->MOrder == US2S1)
                               ? assembly->splitChain()
                               : assembly->unsplitChain();

  // Merge in and out edges of the two chains
  mergeInOutEdges(mergerChain, mergeeChain);

  // Does S2 mark a function transition?
  bool S2FuncTransition = assembly->splitsAtFunctionTransition();

  // Create the new node order according the given assembly
  auto S1Begin = assembly->splitChain()->Nodes.begin();
  auto S2Begin = assembly->SlicePosition;
  auto UBegin = assembly->unsplitChain()->Nodes.begin();

  // Reorder S1S2 to S2S1 if needed. This operation takes O(1) because we're
  // splicing from and into the same list.
  if (assembly->needsSplitChainRotation())
    assembly->splitChain()->Nodes.splice(S1Begin,
                                         assembly->splitChain()->Nodes,
                                         S2Begin,
                                         assembly->splitChain()->Nodes.end());

  switch (assembly->MOrder) {
  case S2S1U:
    // Splice U at the end of S.
    assembly->splitChain()->Nodes.splice(assembly->splitChain()->Nodes.end(),
                                         assembly->unsplitChain()->Nodes);
    break;
  case S1US2:
    // Splice U in the middle
    assembly->splitChain()->Nodes.splice(S2Begin,
                                         assembly->unsplitChain()->Nodes);
    break;
  case S2US1:
    // Splice U in the middle
    assembly->splitChain()->Nodes.splice(S1Begin,
                                         assembly->unsplitChain()->Nodes);
    break;
  case US2S1:
    // Splice S at the end of U
    assembly->unsplitChain()->Nodes.splice(
        assembly->unsplitChain()->Nodes.end(), assembly->splitChain()->Nodes);
    break;
  default:
    break;
  }

  if (propellerConfig.optReorderIP && !mergerChain->isSameCFG(*mergeeChain)) {
    // Merge function transitions of mergerChain with those of mergeeChain.
    mergerChain->FunctionTransitions.splice(
        mergerChain->FunctionTransitions.end(),
        mergeeChain->FunctionTransitions);

    // Add the beginnings of slices if they now mark a function transition.
    std::vector<std::list<CFGNode *>::iterator> funcTransitionCandids;
    // UBegin should always be examined.
    funcTransitionCandids.push_back(UBegin);
    // If S2Begin was a function transition point before, we don't examine it
    // again as it will remain in the transition points (We never remove entries
    // from FunctionTransitions).
    if (!S2FuncTransition)
      funcTransitionCandids.push_back(S2Begin);
    // S1Begin will only be examined if this is a splitting assembly. Otherwise,
    // S1 is empty.
    if (assembly->splits())
      funcTransitionCandids.push_back(S1Begin);

    // Check if any of the candidates mark a function transition position and add
    // those to the function transition list of the mergerChain.
    for (auto it : funcTransitionCandids)
      if (it != mergerChain->Nodes.begin() &&
          (*std::prev(it))->CFG != (*it)->CFG)
        mergerChain->FunctionTransitions.push_back(it);
  }

  // Set the starting and ending point for updating the nodes's chain and offset
  // in the new chain. This helps to reduce the computation time.
  auto chainBegin = mergerChain->Nodes.begin();
  uint64_t chainBeginOffset = 0;

  if (assembly->MOrder == S1US2 || assembly->MOrder == US2S1) {
    // We can skip the first slice (Slices[0]) in these cases.
    chainBegin = assembly->Slices[1].Begin;
    chainBeginOffset = assembly->Slices[0].size();
  }

  if (!assembly->splits()) {
    // We can skip the S chain in this case.
    chainBegin = UBegin;
    chainBeginOffset = assembly->splitChain()->Size;
  }

  uint64_t runningOffset = chainBeginOffset;

  // Update chainOffset and the chain pointer for all the nodes in the sequence.
  for (auto it = chainBegin; it != mergerChain->Nodes.end(); ++it) {
    CFGNode *node = *it;
    node->Chain = mergerChain;
    node->ChainOffset = runningOffset;
    runningOffset += node->ShSize;
  }

  mergerChain->Size += mergeeChain->Size;
  assert(mergerChain->Size == runningOffset && "Mismatch of merger chain's size and running offset!");

  // Update the total frequency the aggregated chain
  mergerChain->Freq += mergeeChain->Freq;

  // We have already computed the new score in the assembly record. So we can
  // update the score based on that and the other chain's score.
  mergerChain->Score += mergeeChain->Score + assembly->ScoreGain;

  mergerChain->DebugChain |= mergeeChain->DebugChain;

  if (mergerChain->CFG && mergerChain->CFG!=mergeeChain->CFG)
    mergerChain->CFG = nullptr;

  // Merge the assembly candidate chains of the two chains into the candidate
  // chains of the remaining NodeChain and remove the records for the defunct
  // NodeChain.
  for (NodeChain *c : CandidateChains[mergeeChain]) {
    NodeChainAssemblies.erase(std::make_pair(c, mergeeChain));
    NodeChainAssemblies.erase(std::make_pair(mergeeChain, c));
    CandidateChains[c].erase(mergeeChain);
    if (c != mergerChain)
      CandidateChains[mergerChain].insert(c);
  }

  // Update the NodeChainAssembly for all candidate chains of the merged
  // NodeChain. Remove a NodeChain from the merge chain's candidates if the
  // NodeChainAssembly update finds no gain.
  auto &mergerChainCandidateChains = CandidateChains[mergerChain];

  for (auto CI = mergerChainCandidateChains.begin(),
            CE = mergerChainCandidateChains.end();
       CI != CE;) {
    NodeChain *otherChain = *CI;
    auto &otherChainCandidateChains = CandidateChains[otherChain];

    bool x = updateNodeChainAssembly(otherChain, mergerChain);

    if (!x)
      NodeChainAssemblies.erase(std::make_pair(otherChain, mergerChain));

    bool y = updateNodeChainAssembly(mergerChain, otherChain);

    if (!y)
      NodeChainAssemblies.erase(std::make_pair(mergerChain, otherChain));

    if (x || y) {
      otherChainCandidateChains.insert(mergerChain);
      CI++;
    } else {
      otherChainCandidateChains.erase(mergerChain);
      CI = mergerChainCandidateChains.erase(CI);
    }
  }

  // remove all the candidate chain records for the merged-in chain
  CandidateChains.erase(mergeeChain);

  // Finally, remove the defunct (merged-in) chain record from the chains
  Chains.erase(mergeeChain->DelegateNode->MappedAddr);
}

// Calculate the Extended TSP metric for a BB chain.
// This function goes over all the BBs in the chain and for BB chain and
// aggregates the score of all edges which are contained in the same chain.
double NodeChainBuilder::computeExtTSPScore(NodeChain *chain) const {
  double score = 0;

  auto visit = [&score](CFGEdge &edge, NodeChain *srcChain,
                        NodeChain *sinkChain) {
    assert(srcChain == sinkChain);
    auto srcOffset = edge.Src->ChainOffset;
    auto sinkOffset = edge.Sink->ChainOffset;
    bool edgeForward = srcOffset + edge.Src->ShSize <= sinkOffset;
    // Calculate the distance between src and sink
    uint64_t distance = edgeForward ? sinkOffset - srcOffset - edge.Src->ShSize
                                    : srcOffset - sinkOffset + edge.Src->ShSize;
    score += getEdgeExtTSPScore(edge, edgeForward, distance);
  };

  chain->forEachOutEdgeToChain(chain, visit);

  return score;
}

// Updates the best NodeChainAssembly between two NodeChains. The existing
// record will be replaced by the new NodeChainAssembly if a non-zero gain
// is achieved. Otherwise, it will be removed.
// If a nodechain assembly record is (kept) inserted, returns true. Otherwise
// returns false.
bool NodeChainBuilder::updateNodeChainAssembly(NodeChain *splitChain,
                                               NodeChain *unsplitChain) {
  // Only consider splitting the chain if the size of the chain is smaller than
  // a threshold.
  bool doSplit = (splitChain->Size <= propellerConfig.optChainSplitThreshold);
  auto slicePosEnd =
      doSplit ? splitChain->Nodes.end() : std::next(splitChain->Nodes.begin());

  std::unique_ptr<NodeChainAssembly> bestAssembly(nullptr);

  for (auto slicePos = splitChain->Nodes.begin(); slicePos != slicePosEnd;
       ++slicePos) {
    // Do not split the mutually-forced edges in the chain.
    if (slicePos != splitChain->Nodes.begin() &&
        MutuallyForcedOut.count(*std::prev(slicePos)))
      continue;

    // If the split position is at the beginning (no splitting), only consider
    // one MergeOrder
    auto mergeOrderEnd = (slicePos == splitChain->Nodes.begin())
                             ? MergeOrder::BeginNext
                             : MergeOrder::End;

    for (uint8_t MI = MergeOrder::Begin; MI != mergeOrderEnd; MI++) {
      MergeOrder mOrder = static_cast<MergeOrder>(MI);

      // Create the NodeChainAssembly representing this particular assembly.
      auto NCA = std::unique_ptr<NodeChainAssembly>(
          new NodeChainAssembly(splitChain, unsplitChain, slicePos, mOrder));

      if (!NCA->isValid())
        continue;

      if (!bestAssembly || NodeChainAssemblyComparator(bestAssembly, NCA))
        bestAssembly = std::move(NCA);
    }
  }

  if (propellerConfig.optReorderIP && !doSplit) {
    // For inter-procedural layout, we always try splitting at the function
    // transition positions regardless of the split-chain's size.
    for (auto FTIt = splitChain->FunctionTransitions.begin(); FTIt!=splitChain->FunctionTransitions.end();) {
      auto slicePos = *FTIt;
      if (slicePos == splitChain->Nodes.begin() || (*std::prev(slicePos))->CFG == (*slicePos)->CFG){
        FTIt = splitChain->FunctionTransitions.erase(FTIt);
        continue;
      }
      for (uint8_t MI = MergeOrder::Begin; MI != MergeOrder::End; MI++) {
        MergeOrder mOrder = static_cast<MergeOrder>(MI);

        // Create the NodeChainAssembly representing this particular assembly.
        auto NCA = std::unique_ptr<NodeChainAssembly>(
            new NodeChainAssembly(splitChain, unsplitChain, slicePos, mOrder));

        if (!NCA->isValid())
          continue;

        // Update bestAssembly if needed
        if (!bestAssembly || NodeChainAssemblyComparator(bestAssembly, NCA))
          bestAssembly = std::move(NCA);
      }
      FTIt++;
    }
  }

  // Insert the best assembly of the two chains.
  if (bestAssembly) {
    if (splitChain->DebugChain || unsplitChain->DebugChain)
      fprintf(stderr, "INSERTING ASSEMBLY: %s\n",
              toString(*bestAssembly).c_str());

    NodeChainAssemblies.insert(bestAssembly->ChainPair,
                               std::move(bestAssembly));
    return true;
  } else
    return false;
}

void NodeChainBuilder::initNodeChains(ControlFlowGraph &cfg) {
  for (auto &node : cfg.Nodes) {
    NodeChain *chain = new NodeChain(node.get());
    node->Chain = chain;
    node->ChainOffset = 0;
    Chains.try_emplace(node->MappedAddr, chain);
  }
}

// Find all the mutually-forced edges.
// These are all the edges which are -- based on the profile -- the only
// (executed) outgoing edge from their source node and the only (executed)
// incoming edges to their sink nodes
void NodeChainBuilder::initMutuallyForcedEdges(ControlFlowGraph &cfg) {
  DenseMap<CFGNode *, CFGNode *> mutuallyForcedOut;
  DenseSet<CFGNode *> mutuallyForcedIn;

  auto l = prop->BBLayouts.find(cfg.Name);
  if (l != prop->BBLayouts.end()) {
    CFGNode *lastNode = nullptr;
    for (auto ordinal : l->second) {
      auto r = Chains.find(ordinal);
      if (r == Chains.end()) {
        lastNode = nullptr;
        continue;
      }
      CFGNode *thisNode = r->second->DelegateNode;
      if (lastNode) {
        mutuallyForcedOut.try_emplace(lastNode, thisNode);
        mutuallyForcedIn.insert(thisNode);
      }
      lastNode = thisNode;
    }
  }

  DenseMap<CFGNode *, std::vector<CFGEdge *>> profiledOuts;
  DenseMap<CFGNode *, std::vector<CFGEdge *>> profiledIns;

  for (auto &node : cfg.Nodes) {
    if (!mutuallyForcedOut.count(node.get())) {
      std::copy_if(node->Outs.begin(), node->Outs.end(),
                   std::back_inserter(profiledOuts[node.get()]),
                   [&mutuallyForcedIn](CFGEdge *edge) {
                     return !mutuallyForcedIn.count(edge->Sink) &&
                            (edge->Type == CFGEdge::EdgeType::INTRA_FUNC ||
                             edge->Type == CFGEdge::EdgeType::INTRA_DYNA) &&
                            edge->Weight != 0;
                   });
    }
    if (!mutuallyForcedIn.count(node.get())) {
      std::copy_if(node->Ins.begin(), node->Ins.end(),
                   std::back_inserter(profiledIns[node.get()]),
                   [&mutuallyForcedOut](CFGEdge *edge) {
                     return !mutuallyForcedOut.count(edge->Src) &&
                            (edge->Type == CFGEdge::EdgeType::INTRA_FUNC ||
                             edge->Type == CFGEdge::EdgeType::INTRA_DYNA) &&
                            edge->Weight != 0;
                   });
    }
  }

  for (auto &node : cfg.Nodes) {
    if (profiledOuts[node.get()].size() == 1) {
      CFGEdge *edge = profiledOuts[node.get()].front();
      if (edge->Type != CFGEdge::EdgeType::INTRA_FUNC &&
          edge->Type != CFGEdge::EdgeType::INTRA_DYNA)
        continue;
      if (profiledIns[edge->Sink].size() == 1)
        mutuallyForcedOut.try_emplace(node.get(), edge->Sink);
    }
  }

  // Break cycles in the mutually forced edges by cutting the edge sinking to
  // the smallest address in every cycle (hopefully a loop backedge)
  DenseMap<CFGNode *, unsigned> nodeToPathMap;
  SmallVector<CFGNode *, 16> cycleCutNodes;
  unsigned pathCount = 0;
  for (auto it = mutuallyForcedOut.begin(); it != mutuallyForcedOut.end();
       ++it) {
    // Check to see if the node (and its cycle) have already been visited.
    if (nodeToPathMap[it->first])
      continue;
    CFGEdge *victimEdge = nullptr;
    auto nodeIt = it;
    pathCount++;
    while (nodeIt != mutuallyForcedOut.end()) {
      CFGNode *node = nodeIt->first;
      unsigned path = nodeToPathMap[node];
      if (path != 0) {
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
        nodeToPathMap[node] = pathCount;
      if (!profiledOuts[node].empty()) {
        CFGEdge *edge = profiledOuts[node].front();
        if (!victimEdge ||
            (edge->Sink->MappedAddr < victimEdge->Sink->MappedAddr)) {
          victimEdge = edge;
        }
      }
      nodeIt = mutuallyForcedOut.find(nodeIt->second);
    }
  }

  // Remove the victim edges to break cycles in the mutually forced edges
  for (CFGNode *node : cycleCutNodes)
    mutuallyForcedOut.erase(node);

  for (auto &elem : mutuallyForcedOut)
    MutuallyForcedOut.insert(elem);
}

// This function initializes the ExtTSP algorithm's data structures;
// the NodeChainAssemblies and the CandidateChains maps.
void NodeChainBuilder::initializeExtTSP() {
  // For each chain, compute its ExtTSP score, add its chain assembly records
  // and its merge candidate chain.
  CandidateChains.clear();
  for (NodeChain *chain : Components[CurrentComponent])
    chain->Score = chain->Freq ? computeExtTSPScore(chain) : 0;

  DenseSet<std::pair<NodeChain *, NodeChain *>> visited;

  for (NodeChain *chain : Components[CurrentComponent]) {
    auto& thisCandidateChains = CandidateChains[chain];
    for (auto &chainEdge : chain->OutEdges) {
      NodeChain *otherChain = chainEdge.first;
      if (chain == otherChain)
        continue;
      std::pair<NodeChain*, NodeChain*> chainPair1(chain, otherChain), chainPair2(otherChain, chain);
      auto p = std::less<std::pair<NodeChain*, NodeChain*>>()(chainPair1, chainPair2) ? chainPair1 : chainPair2;
      if (!visited.insert(p).second)
        continue;

      bool x = updateNodeChainAssembly(chain, otherChain);
      bool y = updateNodeChainAssembly(otherChain, chain);

      if (x || y) {
        thisCandidateChains.insert(otherChain);
        CandidateChains[otherChain].insert(chain);
      }
    }
  }
}

void NodeChainBuilder::initializeChainComponents() {

  DenseMap<NodeChain *, unsigned> chainToComponentMap;
  unsigned componentId = 0;
  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : Chains) {
    NodeChain *chain = elem.second.get();
    // Cold chains will not be placed in any component.
    if (!chain->Freq)
      continue;
    // Skip if this chain has already been assigned to a component.
    if (!chainToComponentMap.try_emplace(chain, componentId).second)
      continue;
    // Start creating the component containing this chain and its connected
    // chains.
    std::vector<NodeChain *> toVisit(1, chain);
    unsigned index = 0;

    // Find the connected component using BFS.
    while (index != toVisit.size()) {
      auto *tchain = toVisit[index++];
      for (auto *c : tchain->InEdges) {
        if (chainToComponentMap.try_emplace(c, componentId).second)
          toVisit.push_back(c);
      }
      for (auto &e : tchain->OutEdges) {
        if (chainToComponentMap.try_emplace(e.first, componentId).second)
          toVisit.push_back(e.first);
      }
    }
    Components.push_back(std::move(toVisit));
    componentId++;
  }
}

void NodeChainBuilder::mergeAllChains() {
  // Attach the mutually-foced edges (which will not be split anymore by the
  // Extended TSP algorithm).
  for (auto &elem : MutuallyForcedOut)
    attachNodes(elem.first, elem.second);

  // Set up the outgoing edges for every chain
  for (auto &elem : Chains) {
    auto *chain = elem.second.get();
    // Ignore cold chains as they cannot have any hot edges to other nodes
    if (!chain->Freq)
      continue;
    auto addEdge = [chain](CFGEdge &edge) {
      // Ignore returns and zero-frequency edges as these edges will not be used
      // by the ExtTSP score algorithm.
      if (!edge.Weight || edge.isReturn())
        return;
      auto *sinkNodeChain = edge.Sink->Chain;
      chain->OutEdges[sinkNodeChain].push_back(&edge);
      sinkNodeChain->InEdges.insert(chain);
    };

    if (propellerConfig.optReorderIP) {
      for (CFGNode *node : chain->Nodes)
        node->forEachOutEdgeRef(addEdge);
    } else {
      for (CFGNode *node : chain->Nodes)
        node->forEachIntraOutEdgeRef(addEdge);
    }
  }

  initializeChainComponents();

  for (CurrentComponent = 0; CurrentComponent < Components.size();
       ++CurrentComponent) {
    if (propellerConfig.optPrintStats)
      fprintf(stderr, "COMPONENT: %u -> SIZE: %zu\n", CurrentComponent,
              Components[CurrentComponent].size());
    // Initialize the Extended TSP algorithm's data.
    initializeExtTSP();

    // Keep merging the chain assembly record with the highest ExtTSP gain,
    // until no more gain is possible.
    while (!NodeChainAssemblies.empty()) {
      auto bestAssembly = NodeChainAssemblies.top();
      NodeChainAssemblies.pop();
      if (bestAssembly->splitChain()->DebugChain ||
          bestAssembly->unsplitChain()->DebugChain)
        fprintf(stderr, "MERGING for %s\n",
                toString(*bestAssembly.get()).c_str());
      mergeChains(std::move(bestAssembly));
    }
  }
}

void NodeChainBuilder::doOrder(std::unique_ptr<ChainClustering> &CC) {
  init();

  mergeAllChains();

  // Merge fallthrough basic blocks if we have missed any
  attachFallThroughs();

  if (!propellerConfig.optReorderIP) {
    // If this is not inter-procedural, coalesce all hot chains into a single
    // hot chain and all cold chains into a single cold chain.
    coalesceChains();
    assert(CFGs.size() == 1 && Chains.size() <= 2);

#ifdef PROPELLER_PROTOBUF
    ControlFlowGraph *cfg = CFGs.back();
    if (prop->protobufPrinter) {
      std::list<CFGNode *> nodeOrder;
      for (auto &elem : Chains) {
        auto *chain = elem.second.get();
        nodeOrder.insert(chain->Freq ? nodeOrder.begin() : nodeOrder.end(),
                         chain->Nodes.begin(), chain->Nodes.end());
      }
      prop->protobufPrinter->addCFG(*cfg, &nodeOrder);
    }
#endif
  }

  // Hand in the built chains to the chain clustering algorithm
  for (auto &elem : Chains)
    CC->addChain(std::move(elem.second));
}

} // namespace propeller
} // namespace lld
