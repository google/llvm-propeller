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
//  * 1 if distance(src[e], sink[e]) = 0 (i.e. fallthrough)
//
//  * 0.1 * (1 - distance(src[e], sink[e]) / 1024) if src[e] < sink[e] and 0 <
//    distance(src[e], sink[e]) < 1024 (i.e. short forward jump)
//
//  * 0.1 * (1 - distance(src[e], sink[e]) / 640) if src[e] > sink[e] and 0 <
//    distance(src[e], sink[e]) < 640 (i.e. short backward jump)
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
// Starting with one basic block sequence (bb chain) for every basic block, the
// algorithm iteratively joins bb chains together in order to maximize the
// extended TSP score of all the chains.
//
// Initially, it finds all mutually-forced edges in the profiled
// controlFlowGraph. These are all the edges which are -- based on the profile
// -- the only (executed) outgoing edge from their source node and the only
// (executed) incoming edges to their sink nodes. Next, the source and sink of
// all mutually-forced edges are attached together as fallthrough edges.
//
// Then, at every iteration, the algorithm tries to merge a pair of bb chains
// which leads to the highest gain in the ExtTSP score. The algorithm extends
// the search space by considering splitting short (less than 1KBs in
// binary size) bb chains into two chains and then merging these two chains with
// the other chain in four ways. After every merge, the new merge gains are
// updated. The algorithm repeats joining bb chains until no additional can be
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
//   * propeller-chain-split-threshold: maximum binary size of a bb chain which
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
  for (ControlFlowGraph *cfg : cfgs) {
    std::vector<std::vector<CFGNode *>> paths;
    initMutuallyForcedEdges(*cfg, paths);
    initNodeChains(*cfg, paths);
  }
}

// NodeChainBuilder calls this function after building all the chains to attach
// as many fall-throughs as possible. Given that the algorithm already optimizes
// the extend TSP score, this function will only affect the cold basic blocks
// and thus we do not need to consider the edge weights.
void NodeChainBuilder::attachFallThroughs() {
  for (ControlFlowGraph *Cfg : cfgs) {
    // First, try to keep the fall-throughs from the original order.
    for (auto &Node : Cfg->nodes) {
      if (Node->ftEdge != nullptr) {
        attachNodes(Node.get(), Node->ftEdge->sink);
      }
    }

    // Sometimes, the original fall-throughs cannot be kept. So we try to find
    // new fall-through opportunities which did not exist in the original order.
    for (auto &Edge : Cfg->intraEdges) {
      if (Edge->type == CFGEdge::EdgeType::INTRA_FUNC ||
          Edge->type == CFGEdge::EdgeType::INTRA_DYNA)
        attachNodes(Edge->src, Edge->sink);
    }
  }
}

// This function sorts bb chains in decreasing order of their execution density.
// NodeChainBuilder calls this function at the end to ensure that hot bb chains
// are placed at the beginning of the function.
void NodeChainBuilder::coalesceChains() {
  std::vector<NodeChain *> chainOrder;
  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : chains)
    chainOrder.push_back(elem.second.get());

  std::sort(
      chainOrder.begin(), chainOrder.end(), [](NodeChain *c1, NodeChain *c2) {
        if (!c1->isSameCFG(*c2))
          error("Attempting to coalesce chains belonging to different "
                "functions.");
        // Always place the hot chains before cold ones
        if (c1->freq == 0 ^ c2->freq == 0)
          return c1->freq != 0;

        // Place the entry node's chain before other chains
        auto *entryNode = c1->controlFlowGraph->getEntryNode();
        if (getNodeChain(entryNode) == c1)
          return true;
        if (getNodeChain(entryNode) == c2)
          return false;

        // Sort chains in decreasing order of execution density, and break ties
        // according to the initial ordering.
        double c1ExecDensity = c1->execDensity();
        double c2ExecDensity = c2->execDensity();
        if (c1ExecDensity == c2ExecDensity)
          return c1->delegateNode->mappedAddr < c2->delegateNode->mappedAddr;
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
    if (propConfig.optSplitFuncs && (mergerChain->freq != 0 && c->freq == 0)) {
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
  if ((propConfig.optReorderIP || propConfig.optSplitFuncs) &&
      (leftChain->freq == 0 ^ rightChain->freq == 0))
    error("Attempting to merge hot and cold chains: \n" + toString(*leftChain) +
          "\nAND\n" + toString(*rightChain));

  if (leftChain->debugChain || rightChain->debugChain)
    fprintf(stderr, "MERGING chains:\n%s\nAND%s\n",
            toString(*leftChain).c_str(), toString(*rightChain).c_str());

  mergeInOutEdges(leftChain, rightChain);

  auto rightChainBegin = rightChain->nodeBundles.begin();

  leftChain->nodeBundles.splice(leftChain->nodeBundles.end(),
                                rightChain->nodeBundles);

  for (auto it = rightChainBegin; it != leftChain->nodeBundles.end(); ++it) {
    (*it)->chain = leftChain;
    (*it)->chainOffset += leftChain->size;
  }

  leftChain->size += rightChain->size;
  leftChain->freq += rightChain->freq;

  leftChain->debugChain |= rightChain->debugChain;
  if (leftChain->controlFlowGraph &&
      leftChain->controlFlowGraph != rightChain->controlFlowGraph)
    leftChain->controlFlowGraph = nullptr;

  chains.erase(rightChain->delegateNode->mappedAddr);
}

// This function tries to place two basic blocks immediately adjacent to each
// other (used for fallthroughs). Returns true if the basic blocks have been
// attached this way.
bool NodeChainBuilder::attachNodes(CFGNode *src, CFGNode *sink) {
  if (sink->isEntryNode())
    return false;

  // Ignore edges between hot and cold basic blocks.
  if (src->freq == 0 ^ sink->freq == 0)
    return false;
  NodeChain *srcChain = getNodeChain(src);
  NodeChain *sinkChain = getNodeChain(sink);
  // Skip this edge if the source and sink are in the same chain
  if (srcChain == sinkChain)
    return false;

  // It's possible to form a fall-through between src and sink only if
  // they are respectively located at the end and beginning of their chains.
  if (srcChain->lastNode() != src || sinkChain->firstNode() != sink)
    return false;
  // Attaching is possible. So we merge the chains in the corresponding order.
  mergeChains(srcChain, sinkChain);
  return true;
}

void NodeChainBuilder::bundleNodes(
    NodeChain *chain, std::list<std::unique_ptr<CFGNodeBundle>>::iterator begin,
    std::list<std::unique_ptr<CFGNodeBundle>>::iterator end) {
  CFGNodeBundle *bundle = (begin == chain->nodeBundles.begin())
                              ? nullptr
                              : (*std::prev(begin)).get();
  for (auto it = begin; it != end;) {
    if (!bundle || (*it)->delegateNode->controlFlowGraph !=
                       bundle->delegateNode->controlFlowGraph) {
      bundle = (*it).get();
      it++;
    } else {
      bundle->merge((*it).get());
      it = chain->nodeBundles.erase(it);
    }
  }
}

// This function merges the in-and-out chain-edges of one chain (mergeeChain)
// into those of another (mergerChain).
void NodeChainBuilder::mergeInOutEdges(NodeChain *mergerChain,
                                       NodeChain *mergeeChain) {
  // Add out-edges of mergeeChain to the out-edges of mergerChain
  for (auto &elem : mergeeChain->outEdges) {
    NodeChain *c = (elem.first == mergeeChain) ? mergerChain : elem.first;
    auto res = mergerChain->outEdges.emplace(c, elem.second);
    // If the chain-edge is already present, just add the controlFlowGraph
    // edges, otherwise, add mergerChain to in-edges of the chain.
    if (!res.second)
      res.first->second.insert(res.first->second.end(), elem.second.begin(),
                               elem.second.end());
    else
      c->inEdges.insert(mergerChain);

    // Remove mergeeChain from in-edges
    c->inEdges.erase(mergeeChain);
  }

  // Add in-edges of mergeeChain to in-edges of mergerChain
  for (auto *c : mergeeChain->inEdges) {
    // Self edges were already handled above.
    if (c == mergeeChain)
      continue;
    // Move all controlFlowGraph edges from being mapped to mergeeChain to being
    // mapped to mergerChain.
    auto &mergeeChainEdges = c->outEdges[mergeeChain];
    auto &mergerChainEdges = c->outEdges[mergerChain];

    mergerChainEdges.insert(mergerChainEdges.end(), mergeeChainEdges.begin(),
                            mergeeChainEdges.end());
    mergerChain->inEdges.insert(c);
    // Remove the defunct chain from out edges
    c->outEdges.erase(mergeeChain);
  }
}

// Merge two bb sequences according to the given NodeChainAssembly. A
// NodeChainAssembly is an ordered triple of three slices from two chains.
void NodeChainBuilder::mergeChains(
    std::unique_ptr<NodeChainAssembly> assembly) {
  if (assembly->splitChain()->freq == 0 ^ assembly->unsplitChain()->freq == 0)
    error("Attempting to merge hot and cold chains: \n" +
          toString(*assembly.get()));

  if (assembly->splitChain()->freq == 0 ^ assembly->unsplitChain()->freq == 0)
    error("Attempting to merge hot and cold chains: \n" +
          toString(*assembly.get()));

  // Decide which chain gets merged into the other chain, to make the reordering
  // more efficient.
  NodeChain *mergerChain = (assembly->mergeOrder == US2S1)
                               ? assembly->unsplitChain()
                               : assembly->splitChain();
  NodeChain *mergeeChain = (assembly->mergeOrder == US2S1)
                               ? assembly->splitChain()
                               : assembly->unsplitChain();

  // Merge in and out edges of the two chains
  mergeInOutEdges(mergerChain, mergeeChain);

  // Does S2 mark a function transition?
  // bool S2FuncTransition = assembly->splitsAtFunctionTransition;

  // Create the new node order according the given assembly
  auto S1Begin = assembly->splitChain()->nodeBundles.begin();
  auto S2Begin = assembly->slicePosition;
  auto UBegin = assembly->unsplitChain()->nodeBundles.begin();

  // Reorder S1S2 to S2S1 if needed. This operation takes O(1) because we're
  // splicing from and into the same list.
  if (assembly->needsSplitChainRotation)
    assembly->splitChain()->nodeBundles.splice(
        S1Begin, assembly->splitChain()->nodeBundles, S2Begin,
        assembly->splitChain()->nodeBundles.end());

  switch (assembly->mergeOrder) {
  case S2S1U:
    // Splice U at the end of S.
    assembly->splitChain()->nodeBundles.splice(
        assembly->splitChain()->nodeBundles.end(),
        assembly->unsplitChain()->nodeBundles);
    break;
  case S1US2:
    // Splice U in the middle
    assembly->splitChain()->nodeBundles.splice(
        S2Begin, assembly->unsplitChain()->nodeBundles);
    break;
  case S2US1:
    // Splice U in the middle
    assembly->splitChain()->nodeBundles.splice(
        S1Begin, assembly->unsplitChain()->nodeBundles);
    break;
  case US2S1:
    // Splice S at the end of U
    assembly->unsplitChain()->nodeBundles.splice(
        assembly->unsplitChain()->nodeBundles.end(),
        assembly->splitChain()->nodeBundles);
    break;
  default:
    break;
  }

  // Set the starting and ending point for updating the nodes's chain and offset
  // in the new chain. This helps to reduce the computation time.
  auto chainBegin = mergerChain->nodeBundles.begin();
  uint64_t chainBeginOffset = 0;

  if (assembly->mergeOrder == S1US2 || assembly->mergeOrder == US2S1) {
    // We can skip the first slice (slices[0]) in these cases.
    chainBegin = assembly->slices[1].beginPosition;
    chainBeginOffset = assembly->slices[0].size();
  }

  if (!assembly->splits) {
    // We can skip the S chain in this case.
    chainBegin = UBegin;
    chainBeginOffset = assembly->splitChain()->size;
  }

  uint64_t runningOffset = chainBeginOffset;

  // Update chainOffset and the chain pointer for all the nodes in the sequence.
  for (auto it = chainBegin; it != mergerChain->nodeBundles.end(); ++it) {
    (*it)->chain = mergerChain;
    (*it)->chainOffset = runningOffset;
    runningOffset += (*it)->size;
  }

  if (assembly->splitChain()->size + assembly->unsplitChain()->size >
      propConfig.optChainSplitThreshold) {
    if (assembly->splitChain()->size <= propConfig.optChainSplitThreshold &&
        assembly->unsplitChain()->size <= propConfig.optChainSplitThreshold)
      bundleNodes(mergerChain, mergerChain->nodeBundles.begin(),
                  mergerChain->nodeBundles.end());
    else if (assembly->unsplitChain()->size <=
             propConfig.optChainSplitThreshold)
      bundleNodes(mergerChain, UBegin, mergerChain->nodeBundles.end());
    else if (assembly->splitChain()->size <=
             propConfig.optChainSplitThreshold) {
      switch (assembly->mergeOrder) {
      case S2S1U:
        bundleNodes(mergerChain, mergerChain->nodeBundles.begin(),
                    std::next(UBegin));
        break;
      case S1US2:
        bundleNodes(mergerChain, S1Begin, std::next(UBegin));
        bundleNodes(mergerChain, S2Begin, mergerChain->nodeBundles.end());
        break;
      case S2US1:
        bundleNodes(mergerChain, S2Begin, std::next(UBegin));
        bundleNodes(mergerChain, S1Begin, mergerChain->nodeBundles.end());
        break;
      case US2S1:
        bundleNodes(mergerChain, S2Begin, mergerChain->nodeBundles.end());
        break;
      default:
        break;
      }
    } else
      bundleNodes(mergerChain, UBegin, std::next(UBegin));
  }

  mergerChain->size += mergeeChain->size;
  assert(mergerChain->size == runningOffset &&
         "Mismatch of merger chain's size and running offset!");

  // Update the total frequency the aggregated chain
  mergerChain->freq += mergeeChain->freq;

  // We have already computed the new score in the assembly record. So we can
  // update the score based on that and the other chain's score.
  // mergerChain->bundleScore += mergeeChain->bundleScore;
  mergerChain->score += mergeeChain->score + assembly->scoreGain;

  adjustExtTSPScore(mergerChain);

  mergerChain->debugChain |= mergeeChain->debugChain;

  if (mergerChain->controlFlowGraph &&
      mergerChain->controlFlowGraph != mergeeChain->controlFlowGraph)
    mergerChain->controlFlowGraph = nullptr;

  // Merge the assembly candidate chains of the two chains into the candidate
  // chains of the remaining NodeChain and remove the records for the defunct
  // NodeChain.
  for (NodeChain *c : candidateChains[mergeeChain]) {
    nodeChainAssemblies.erase(std::make_pair(c, mergeeChain));
    nodeChainAssemblies.erase(std::make_pair(mergeeChain, c));
    candidateChains[c].erase(mergeeChain);
    if (c != mergerChain)
      candidateChains[mergerChain].insert(c);
  }

  // Update the NodeChainAssembly for all candidate chains of the merged
  // NodeChain. Remove a NodeChain from the merge chain's candidates if the
  // NodeChainAssembly update finds no gain.
  auto &mergerChainCandidateChains = candidateChains[mergerChain];

  for (auto CI = mergerChainCandidateChains.begin(),
            CE = mergerChainCandidateChains.end();
       CI != CE;) {
    NodeChain *otherChain = *CI;
    auto &otherChainCandidateChains = candidateChains[otherChain];

    bool x = updateNodeChainAssembly(otherChain, mergerChain);

    if (!x)
      nodeChainAssemblies.erase(std::make_pair(otherChain, mergerChain));

    bool y = updateNodeChainAssembly(mergerChain, otherChain);

    if (!y)
      nodeChainAssemblies.erase(std::make_pair(mergerChain, otherChain));

    if (x || y) {
      otherChainCandidateChains.insert(mergerChain);
      CI++;
    } else {
      otherChainCandidateChains.erase(mergerChain);
      CI = mergerChainCandidateChains.erase(CI);
    }
  }

  // remove all the candidate chain records for the merged-in chain
  candidateChains.erase(mergeeChain);

  // Finally, remove the defunct (merged-in) chain record from the chains
  chains.erase(mergeeChain->delegateNode->mappedAddr);
}

void NodeChainBuilder::adjustExtTSPScore(NodeChain *chain) const {
  auto chainEdgesIt = chain->outEdges.find(chain);
  if (chainEdgesIt == chain->outEdges.end())
    return;

  auto &chainEdges = chainEdgesIt->second;

  auto newEnd = std::remove_if(
      chainEdges.begin(), chainEdges.end(), [chain](const CFGEdge *edge) {
        if (edge->src->bundle != edge->sink->bundle)
          return false;

        auto srcOffset = getNodeOffset(edge->src);
        auto sinkOffset = getNodeOffset(edge->sink);
        // Calculate the distance between src and sink
        auto edgeScore = getEdgeExtTSPScore(*edge, sinkOffset - srcOffset -
                                                       edge->src->shSize);
        // chain->bundleScore += edgeScore;
        chain->score -= edgeScore;
        return true;
      });
  chainEdges.erase(newEnd, chainEdges.end());
  if (chainEdges.empty())
    chain->inEdges.erase(chain);
}

// Calculate the Extended TSP metric for a bb chain.
// This function goes over all the BBs in the chain and for bb chain and
// aggregates the score of all edges which are contained in the same chain.
uint64_t NodeChainBuilder::computeExtTSPScore(NodeChain *chain) const {
  uint64_t score = 0;

  auto visit = [&score](CFGEdge &edge, NodeChain *srcChain,
                        NodeChain *sinkChain) {
    assert(srcChain == sinkChain);
    assert(edge.src->bundle != edge.sink->bundle);
    auto srcOffset = getNodeOffset(edge.src);
    auto sinkOffset = getNodeOffset(edge.sink);
    // Calculate the distance between src and sink
    score +=
        getEdgeExtTSPScore(edge, sinkOffset - srcOffset - edge.src->shSize);
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
  // bool doSplit = (splitChain->size <= propConfig.optChainSplitThreshold);
  // If we are not splitting, we only consider the slice position at the
  // beginning of the chain (effectively no splitting).
  // auto slicePosEnd =
  //    doSplit ? splitChain->nodeBundles.end() :
  //    std::next(splitChain->nodeBundles.begin());

  std::unique_ptr<NodeChainAssembly> bestAssembly(nullptr);

  // This function creates the different nodeChainAssemblies for splitChain and
  // unsplitChain, given a slice position in splitChain, and updates the best
  // assembly if required.
  for (auto slicePos = splitChain->nodeBundles.begin();
       slicePos != splitChain->nodeBundles.end(); ++slicePos) {
    // If the split position is at the beginning (no splitting), only consider
    // one MergeOrder
    auto mergeOrderEnd = (slicePos == splitChain->nodeBundles.begin())
                             ? MergeOrder::BeginNext
                             : MergeOrder::End;
    for (uint8_t MI = MergeOrder::Begin; MI != mergeOrderEnd; MI++) {
      MergeOrder mOrder = static_cast<MergeOrder>(MI);

      // Create the NodeChainAssembly representing this particular assembly.
      auto NCA = std::unique_ptr<NodeChainAssembly>(
          new NodeChainAssembly(splitChain, unsplitChain, slicePos, mOrder));

      // Update bestAssembly if needed
      if (NCA->isValid() &&
          (!bestAssembly || nodeChainAssemblyComparator(bestAssembly, NCA)))
        bestAssembly = std::move(NCA);
    }
  }

  // Insert the best assembly of the two chains.
  if (bestAssembly) {
    if (splitChain->debugChain || unsplitChain->debugChain)
      fprintf(stderr, "INSERTING ASSEMBLY: %s\n",
              toString(*bestAssembly).c_str());

    nodeChainAssemblies.insert(bestAssembly->chainPair,
                               std::move(bestAssembly));
    return true;
  }

  return false;
}

void NodeChainBuilder::initNodeChains(
    ControlFlowGraph &cfg, std::vector<std::vector<CFGNode *>> &paths) {
  for (auto &path : paths)
    chains.try_emplace(path.front()->mappedAddr, new NodeChain(path));

  for (auto &node : cfg.nodes) {
    if (!node->bundle)
      chains.try_emplace(node->mappedAddr, new NodeChain(node.get()));
  }
}

// Find all the mutually-forced edges.
// These are all the edges which are -- based on the profile -- the only
// (executed) outgoing edge from their source node and the only (executed)
// incoming edges to their sink nodes
void NodeChainBuilder::initMutuallyForcedEdges(
    ControlFlowGraph &cfg, std::vector<std::vector<CFGNode *>> &paths) {
  DenseMap<CFGNode *, CFGNode *> mutuallyForcedOut;

  DenseMap<CFGNode *, std::vector<CFGEdge *>> profiledOuts;
  DenseMap<CFGNode *, std::vector<CFGEdge *>> profiledIns;

  for (auto &node : cfg.nodes) {
    std::copy_if(node->outs.begin(), node->outs.end(),
                 std::back_inserter(profiledOuts[node.get()]),
                 [](CFGEdge *edge) {
                   return (edge->type == CFGEdge::EdgeType::INTRA_FUNC ||
                           edge->type == CFGEdge::EdgeType::INTRA_DYNA) &&
                          edge->weight != 0;
                 });
    std::copy_if(node->ins.begin(), node->ins.end(),
                 std::back_inserter(profiledIns[node.get()]),
                 [](CFGEdge *edge) {
                   return (edge->type == CFGEdge::EdgeType::INTRA_FUNC ||
                           edge->type == CFGEdge::EdgeType::INTRA_DYNA) &&
                          edge->weight != 0;
                 });
  }

  for (auto &node : cfg.nodes) {
    if (profiledOuts[node.get()].size() == 1) {
      CFGEdge *edge = profiledOuts[node.get()].front();
      if (profiledIns[edge->sink].size() == 1)
        mutuallyForcedOut.try_emplace(node.get(), edge->sink);
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
          cycleCutNodes.push_back(victimEdge->src);
        }
        break;
      } else
        nodeToPathMap[node] = pathCount;
      if (!profiledOuts[node].empty()) {
        CFGEdge *edge = profiledOuts[node].front();
        if (!victimEdge ||
            (edge->sink->mappedAddr < victimEdge->sink->mappedAddr)) {
          victimEdge = edge;
        }
      }
      nodeIt = mutuallyForcedOut.find(nodeIt->second);
    }
  }

  // Remove the victim edges to break cycles in the mutually forced edges
  for (CFGNode *node : cycleCutNodes)
    mutuallyForcedOut.erase(node);

  DenseSet<CFGNode *> mutuallyForcedIn;
  for (auto &elem : mutuallyForcedOut)
    mutuallyForcedIn.insert(elem.second);

  for (auto it = mutuallyForcedOut.begin(); it != mutuallyForcedOut.end();
       ++it) {
    if (!mutuallyForcedIn.count(it->first)) {
      std::vector<CFGNode *> path(1, it->first);
      auto itt = it;
      do {
        path.push_back(itt->second);
      } while ((itt = mutuallyForcedOut.find(itt->second)) !=
               mutuallyForcedOut.end());
      paths.push_back(std::move(path));
    }
  }
}

// This function initializes the ExtTSP algorithm's data structures;
// the nodeChainAssemblies and the candidateChains maps.
void NodeChainBuilder::initializeExtTSP() {
  // For each chain, compute its ExtTSP score, add its chain assembly records
  // and its merge candidate chain.
  candidateChains.clear();
  for (NodeChain *chain : components[currentComponent])
    chain->score = chain->freq ? computeExtTSPScore(chain) : 0;

  DenseSet<std::pair<NodeChain *, NodeChain *>> visited;

  for (NodeChain *chain : components[currentComponent]) {
    auto &thisCandidateChains = candidateChains[chain];
    for (auto &chainEdge : chain->outEdges) {
      NodeChain *otherChain = chainEdge.first;
      if (chain == otherChain)
        continue;
      std::pair<NodeChain *, NodeChain *> chainPair1(chain, otherChain),
          chainPair2(otherChain, chain);
      auto p = std::less<std::pair<NodeChain *, NodeChain *>>()(chainPair1,
                                                                chainPair2)
                   ? chainPair1
                   : chainPair2;
      if (!visited.insert(p).second)
        continue;

      bool x = updateNodeChainAssembly(chain, otherChain);
      bool y = updateNodeChainAssembly(otherChain, chain);

      if (x || y) {
        thisCandidateChains.insert(otherChain);
        candidateChains[otherChain].insert(chain);
      }
    }
  }
}

void NodeChainBuilder::initializeChainComponents() {

  DenseMap<NodeChain *, unsigned> chainToComponentMap;
  unsigned componentId = 0;
  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : chains) {
    NodeChain *chain = elem.second.get();
    // Cold chains will not be placed in any component.
    if (!chain->freq)
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
      for (auto *c : tchain->inEdges) {
        if (chainToComponentMap.try_emplace(c, componentId).second)
          toVisit.push_back(c);
      }
      for (auto &e : tchain->outEdges) {
        if (chainToComponentMap.try_emplace(e.first, componentId).second)
          toVisit.push_back(e.first);
      }
    }
    components.push_back(std::move(toVisit));
    componentId++;
  }
}

void NodeChainBuilder::mergeAllChains() {
  // Set up the outgoing edges for every chain
  for (auto &elem : chains) {
    auto *chain = elem.second.get();
    // Ignore cold chains as they cannot have any hot edges to other nodes
    if (!chain->freq)
      continue;
    auto addEdge = [chain](CFGEdge &edge) {
      // Ignore returns and zero-frequency edges as these edges will not be used
      // by the ExtTSP score algorithm.
      if (!edge.weight || edge.isReturn())
        return;
      if (edge.src->bundle == edge.sink->bundle)
        return;
      auto *sinkNodeChain = getNodeChain(edge.sink);
      chain->outEdges[sinkNodeChain].push_back(&edge);
      sinkNodeChain->inEdges.insert(chain);
    };

    if (propConfig.optReorderIP) {
      for (auto &bundle_ptr : chain->nodeBundles)
        for (CFGNode *node : bundle_ptr->nodes)
          node->forEachOutEdgeRef(addEdge);
    } else {
      for (auto &bundle_ptr : chain->nodeBundles)
        for (CFGNode *node : bundle_ptr->nodes)
          node->forEachIntraOutEdgeRef(addEdge);
    }
  }

  initializeChainComponents();

  for (currentComponent = 0; currentComponent < components.size();
       ++currentComponent) {
    if (propConfig.optPrintStats)
      fprintf(stderr, "COMPONENT: %u -> SIZE: %zu\n", currentComponent,
              components[currentComponent].size());
    // Initialize the Extended TSP algorithm's data.
    initializeExtTSP();

    // Keep merging the chain assembly record with the highest ExtTSP gain,
    // until no more gain is possible.
    while (!nodeChainAssemblies.empty()) {
      auto bestAssembly = nodeChainAssemblies.top();
      nodeChainAssemblies.pop();
      if (bestAssembly->splitChain()->debugChain ||
          bestAssembly->unsplitChain()->debugChain)
        fprintf(stderr, "MERGING for %s\n",
                toString(*bestAssembly.get()).c_str());
      mergeChains(std::move(bestAssembly));
    }
  }
}

void NodeChainBuilder::doOrder(std::unique_ptr<ChainClustering> &clustering) {
  init();

  mergeAllChains();

  // Merge fallthrough basic blocks if we have missed any
  attachFallThroughs();

  if (!propConfig.optReorderIP) {
    // If this is not inter-procedural, coalesce all hot chains into a single
    // hot chain and all cold chains into a single cold chain.
    coalesceChains();
    assert(cfgs.size() == 1 && chains.size() <= 2);

#ifdef PROPELLER_PROTOBUF
    ControlFlowGraph *cfg = cfgs.back();
    if (prop->protobufPrinter) {
      std::list<CFGNode *> nodeOrder;
      for (auto &elem : chains) {
        auto *chain = elem.second.get();
        for (auto& bundle : chain->nodeBundles)
          nodeOrder.insert(chain->freq ? nodeOrder.begin() : nodeOrder.end(),
                           bundle->nodes.begin(), bundle->nodes.end());
      }
      prop->protobufPrinter->addCFG(*cfg, &nodeOrder);
    }
#endif
  }

  // Hand in the built chains to the chain clustering algorithm
  for (auto &elem : chains)
    clustering->addChain(std::move(elem.second));
}

} // namespace propeller
} // namespace lld
