//===- PropellerBBReordering.cpp  -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
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
//     the algorithm will consider for splitting (default-set to 128).
//
// References:
//   * [1] A. Newell and S. Pupyrev, Improved Basic Block Reordering, available
//         at https://arxiv.org/abs/1809.04676
//===----------------------------------------------------------------------===//
#include "PropellerBBReordering.h"

#include "Config.h"
#include "llvm/ADT/DenseSet.h"

#include <numeric>
#include <vector>


using llvm::DenseSet;
using llvm::detail::DenseMapPair;

namespace lld {
namespace propeller {
const unsigned ClusterMergeSizeThreshold = 1 << 21;

std::string toString(MergeOrder mOrder){
  switch (mOrder) {
    case MergeOrder::X2X1Y:
      return "X2X1Y";
    case MergeOrder::X1YX2:
      return "X1YX2";
    case MergeOrder::X2YX1:
      return "X2YX1";
    case MergeOrder::YX2X1:
      return "YX2X1";
    default:
      assert("Invalid MergeOrder!" && false);
      return "";
  }
}

std::string toString(const NodeChain& c){
  std::string str;
  size_t cfgNameLength = c.DelegateNode->CFG->Name.size();
  str += c.DelegateNode->CFG->Name.str() + " [ ";
  for(auto* n: c.Nodes){
    str += n->CFG->getEntryNode()==n ? "Entry" : std::to_string(n->ShName.size() - cfgNameLength - 4);
    str += " (size=" + std::to_string(n->ShSize) + ", freq=" + std::to_string(n->Freq) + ")";
    if (n!=c.Nodes.back())
      str += " -> ";
  }
  str += " ]";
  str += " score: " + std::to_string(c.Score);
  return str;
}

std::string NodeChainBuilder::toString(const NodeChainAssembly &assembly) const{
  std::string str("assembly record between:\n");
  str += lld::propeller::toString(*assembly.SplitChain) + " as X\n";
  str += lld::propeller::toString(*assembly.UnsplitChain) + " as Y\n";
  str += "split position (X):, " + std::to_string(assembly.SlicePosition - assembly.SplitChain->Nodes.begin()) + "\n";
  str += "merge order: " + lld::propeller::toString(assembly.MOrder) + "\n";
  str += "Score: " + std::to_string(assembly.Score);
  return str;
}

// Return the Extended TSP score for one edge, given its source to sink
// direction and distance in the layout.
double getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
                          uint32_t srcSinkDistance) {
  if (edge.Weight == 0)
    return 0;
  uint32_t distance = srcSinkDistance;

  double scale = edge.isReturn() ? 0 : 1.0;

  if (edge.isCall()){
    if (isEdgeForward)
      distance += edge.Src->ShSize / 2;
    else
      distance -= edge.Src->ShSize / 2;
  }

  if (edge.isReturn()){
    if (isEdgeForward)
      distance += edge.Sink->ShSize / 2;
    else
      distance -= edge.Sink->ShSize / 2;
  }

  if(distance == 0 && (edge.Type == CFGEdge::EdgeType::INTRA_FUNC || edge.Type == CFGEdge::EdgeType::INTRA_DYNA))
    return scale * edge.Weight * config->propellerFallthroughWeight;

  if (isEdgeForward && distance < config->propellerForwardJumpDistance)
    return scale * edge.Weight * config->propellerForwardJumpWeight *
           (1.0 -
            ((double)distance) / config->propellerForwardJumpDistance);

  if (!isEdgeForward && distance < config->propellerBackwardJumpDistance)
    return scale * edge.Weight * config->propellerBackwardJumpWeight *
           (1.0 -
            ((double)distance) / config->propellerBackwardJumpDistance);
  //if (config->propellerReorderIP && distance < 4096)
  //  return scale * edge.Weight * 0.0001 * (1.0 - ((double)distance) / 4096);
  return 0;
}

void NodeChainBuilder::init() {
  for (const ControlFlowGraph* cfg: CFGs){
    initNodeChains(*cfg);
    initMutuallyForcedEdges(*cfg);
  }

}

// NodeChainBuilder calls this function after building all the chains to attach
// as many fall-throughs as possible. Given that the algorithm already optimizes
// the extend TSP score, this function will only affect the cold basic blocks
// and thus we do not need to consider the edge weights.
void NodeChainBuilder::attachFallThroughs() {
  for (const ControlFlowGraph* Cfg: CFGs){
    // First, try to keep the fall-throughs from the original order.
    for (auto &Node : Cfg->Nodes) {
      if (Node->FTEdge != nullptr) {
        attachNodes(Node.get(), Node->FTEdge->Sink);
      }
    }

    // Sometimes, the original fall-throughs cannot be kept. So we try to find new
    // fall-through opportunities which did not exist in the original order.
    for (auto &Edge : Cfg->IntraEdges) {
      attachNodes(Edge->Src, Edge->Sink);
    }
  }
}

// Sort BB chains in decreasing order of their execution density.
// NodeChainBuilder calls this function at the end to ensure that hot BB chains
// are placed at the beginning of the function.
void NodeChainBuilder::coalesceChains() {
  std::vector<NodeChain*> chainOrder;
  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : Chains)
    chainOrder.push_back(elem.second.get());

  std::sort(
      chainOrder.begin(), chainOrder.end(),
      [](const NodeChain *c1, const NodeChain *c2) {
        if (c1->Nodes.front()->isEntryNode())
          return true;
        if (c2->Nodes.front()->isEntryNode())
          return false;
        double c1ExecDensity = c1->execDensity();
        double c2ExecDensity = c2->execDensity();
        if (c1ExecDensity == c2ExecDensity)
          return c1->DelegateNode->MappedAddr < c2->DelegateNode->MappedAddr;
        return c1ExecDensity > c2ExecDensity;
      });

  NodeChain * mergerChain = nullptr;

  for(NodeChain *c: chainOrder){
    if(!mergerChain){
      mergerChain = c;
      continue;
    }
    // Create a cold partition when -propeller-split-funcs is set.
    if(config->propellerSplitFuncs && (mergerChain->Freq==0 ^ c->Freq==0)){
      mergerChain=c;
      continue;
    }
    mergeChains(mergerChain, c);
  }
}


// Merge two chains in the specified order.
void NodeChainBuilder::mergeChains(NodeChain *leftChain,
                                   NodeChain *rightChain) {

  mergeInOutEdges(leftChain, rightChain);

  for (const CFGNode *node : rightChain->Nodes) {
    leftChain->Nodes.push_back(node);
    NodeToChainMap[node] = leftChain;
    NodeOffsetMap[node] += leftChain->Size;
  }
  leftChain->Size += rightChain->Size;
  leftChain->Freq += rightChain->Freq;
  leftChain->DebugChain |= rightChain->DebugChain;


  Chains.erase(rightChain->DelegateNode->MappedAddr);

  leftChain->FunctionEntryIndices.clear();
  for(unsigned i=1 ; i< leftChain->Nodes.size(); ++i)
    if (leftChain->Nodes[i]->CFG != leftChain->Nodes[i-1]->CFG)
      leftChain->FunctionEntryIndices.push_back(i);
}

// This function tries to place two basic blocks immediately adjacent to each
// other (used for fallthroughs). Returns true if the basic blocks have been
// attached this way.
bool NodeChainBuilder::attachNodes(const CFGNode *src, const CFGNode *sink) {
  // No edge cannot fall-through to the entry basic block.
  if (sink->isEntryNode())
    return false;

  // Ignore edges between hot and cold basic blocks.
  if (src->Freq == 0 ^ sink->Freq == 0)
    return false;
  NodeChain *srcChain = getNodeChain(src);
  NodeChain *sinkChain = getNodeChain(sink);
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

void NodeChainBuilder::mergeInOutEdges(NodeChain * mergerChain, NodeChain * mergeeChain){
  for (auto& elem : mergeeChain->OutEdges) {
    NodeChain * c = (elem.first == mergeeChain) ? mergerChain : elem.first;
    auto res = mergerChain->OutEdges.emplace(c, elem.second);
    if (!res.second)
      res.first->second.merge(elem.second);
    else
      c->InEdges.insert(mergerChain);

    c->InEdges.erase(mergeeChain);
  }

  for (auto * c: mergeeChain->InEdges) {
    if (c == mergeeChain)
      continue;
    auto& edges = c->OutEdges[mergeeChain];

    c->OutEdges[mergerChain].merge(std::move(edges));
    mergerChain->InEdges.insert(c);
    c->OutEdges.erase(mergeeChain);
  }

}


// Merge two BB sequences according to the given NodeChainAssembly. A
// NodeChainAssembly is an ordered triple of three slices from two chains.
void NodeChainBuilder::mergeChains(
    std::unique_ptr<NodeChainAssembly> assembly) {

  mergeInOutEdges(assembly->SplitChain, assembly->UnsplitChain);

  // Create the new node order according the given assembly
  std::vector<const CFGNode *> newNodeOrder;
  for (NodeChainSlice &slice : assembly->Slices) {
    std::copy(slice.Begin, slice.End, std::back_inserter(newNodeOrder));
  }
  // We merge the nodes into the SplitChain
  assembly->SplitChain->Nodes = std::move(newNodeOrder);

  assembly->SplitChain->FunctionEntryIndices.clear();
  for(unsigned i=1 ; i< assembly->SplitChain->Nodes.size(); ++i)
    if (assembly->SplitChain->Nodes[i]->CFG != assembly->SplitChain->Nodes[i-1]->CFG)
      assembly->SplitChain->FunctionEntryIndices.push_back(i);

  // Update nodeOffsetMap and nodeToChainMap for all the nodes in the sequence.
  uint32_t runningOffset = 0;
  for (const CFGNode *node : assembly->SplitChain->Nodes) {
    NodeToChainMap[node] = assembly->SplitChain;
    NodeOffsetMap[node] = runningOffset;
    runningOffset += node->ShSize;
  }
  assembly->SplitChain->Size = runningOffset;

  // Update the total frequency and ExtTSP score of the aggregated chain
  assembly->SplitChain->Freq += assembly->UnsplitChain->Freq;

  // We have already computed the new score in the assembly record. So we can
  // just set the new score equal to that.
  assembly->SplitChain->Score = assembly->Score;

  assembly->SplitChain->DebugChain |= assembly->UnsplitChain->DebugChain;


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

    if (!x)
      NodeChainAssemblies.erase(std::make_pair(otherChain, assembly->SplitChain));

    bool y = updateNodeChainAssembly(assembly->SplitChain, otherChain);

    if (!y)
      NodeChainAssemblies.erase(std::make_pair(assembly->SplitChain, otherChain));

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
  Chains.erase(assembly->UnsplitChain->DelegateNode->MappedAddr);
}

// Calculate the Extended TSP metric for a BB chain.
// This function goes over all the BBs in the chain and for BB chain and
// aggregates the score of all edges which are contained in the same chain.
double NodeChainBuilder::computeExtTSPScore(NodeChain *chain) const {
  double score = 0;

  auto visit = [this, &score] (const CFGEdge& edge){
    assert(getNodeChain(edge.Src) == getNodeChain(edge.Sink));
    auto srcOffset = getNodeOffset(edge.Src);
    auto sinkOffset = getNodeOffset(edge.Sink);
    bool edgeForward = srcOffset + edge.Src->ShSize <= sinkOffset;
    // Calculate the distance between src and sink
    uint32_t distance = edgeForward ? sinkOffset - srcOffset - edge.Src->ShSize
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
  // Remove the assembly record
  //NodeChainAssemblies.erase(std::make_pair(splitChain, unsplitChain));

  //if (splitChain->Size > 4096 || unsplitChain->Size > 4096)
  //  return false;

  // Only consider splitting the chain if the size of the chain is smaller than
  // a threshold.
  bool doSplit = (splitChain->Size <= config->propellerChainSplitThreshold);
  auto slicePosEnd =
      doSplit ? splitChain->Nodes.end() : std::next(splitChain->Nodes.begin());

  std::unique_ptr<NodeChainAssembly> bestAssembly (nullptr);

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
      auto NCA = std::unique_ptr<NodeChainAssembly>(new NodeChainAssembly(
          splitChain, unsplitChain, slicePos, mOrder, this));

      if (!NCA->isValid())
        continue;

      if (!bestAssembly || NodeChainAssembly::CompareNodeChainAssembly()(bestAssembly, NCA))
        bestAssembly = std::move(NCA);
    }
  }

  if (!doSplit){ //&& splitChain->Size <= 4096) {
    for(auto index : splitChain->FunctionEntryIndices){
      auto slicePos = splitChain->Nodes.begin() + index;

      for (uint8_t MI = MergeOrder::Begin; MI != MergeOrder::End; MI++) {
        MergeOrder mOrder = static_cast<MergeOrder>(MI);

        // Create the NodeChainAssembly representing this particular assembly.
        auto NCA = std::unique_ptr<NodeChainAssembly>(new NodeChainAssembly(
            splitChain, unsplitChain, slicePos, mOrder, this));

        if (!NCA->isValid())
          continue;

        if (!bestAssembly || NodeChainAssembly::CompareNodeChainAssembly()(bestAssembly, NCA))
          bestAssembly = std::move(NCA);
      }

    }
  }

  // Insert the best assembly of the two chains (only if it has positive gain).
  if (bestAssembly && bestAssembly->extTSPScoreGain() > 0) {
    if (splitChain->DebugChain || unsplitChain->DebugChain)
      fprintf(stderr, "INSERTING ASSEMBLY: %s\n", toString(*bestAssembly).c_str());

    NodeChainAssemblies.insert(std::make_pair(splitChain, unsplitChain),
                               std::move(bestAssembly));
    return true;
  } else
    return false;
}

void NodeChainBuilder::initNodeChains(const ControlFlowGraph &cfg) {
  for (auto &node : cfg.Nodes) {
    NodeChain *chain = new NodeChain(node.get());
    NodeToChainMap[node.get()] = chain;
    NodeOffsetMap[node.get()] = 0;
    Chains.try_emplace(node->MappedAddr, chain);
  }
}

// Find all the mutually-forced edges.
// These are all the edges which are -- based on the profile -- the only
// (executed) outgoing edge from their source node and the only (executed)
// incoming edges to their sink nodes
void NodeChainBuilder::initMutuallyForcedEdges(const ControlFlowGraph &cfg) {
  DenseMap<const CFGNode *, std::vector<CFGEdge *>> profiledOuts;
  DenseMap<const CFGNode *, std::vector<CFGEdge *>> profiledIns;
  DenseMap<const CFGNode *, CFGNode *> mutuallyForcedOut;

  for (auto &node : cfg.Nodes) {
    std::copy_if(node->Outs.begin(), node->Outs.end(),
                 std::back_inserter(profiledOuts[node.get()]),
                 [](const CFGEdge *edge) {
                   return (edge->Type == CFGEdge::EdgeType::INTRA_FUNC || edge->Type == CFGEdge::EdgeType::INTRA_DYNA) &&
                          edge->Weight != 0;
                 });
    std::copy_if(node->Ins.begin(), node->Ins.end(),
                 std::back_inserter(profiledIns[node.get()]),
                 [](const CFGEdge *edge) {
                   return (edge->Type == CFGEdge::EdgeType::INTRA_FUNC || edge->Type == CFGEdge::EdgeType::INTRA_DYNA) &&
                          edge->Weight != 0;
                 });
  }

  for (auto &node : cfg.Nodes) {
    if (profiledOuts[node.get()].size() == 1) {
      CFGEdge *edge = profiledOuts[node.get()].front();
      if(edge->Type != CFGEdge::EdgeType::INTRA_FUNC)
        continue;
      if (profiledIns[edge->Sink].size() == 1)
        mutuallyForcedOut[node.get()] = edge->Sink;
    }
  }

  // Break cycles in the mutually forced edges by cutting the edge sinking to
  // the smallest address in every cycle (hopefully a loop backedge)
  DenseMap<const CFGNode *, unsigned> nodeToPathMap;
  SmallVector<const CFGNode *, 16> cycleCutNodes;
  unsigned pathCount = 0;
  for (auto it = mutuallyForcedOut.begin(); it != mutuallyForcedOut.end();
       ++it) {
    // Check to see if the node (and its cycle) have already been visited.
    if (nodeToPathMap[it->first])
      continue;
    const CFGEdge *victimEdge = nullptr;
    auto nodeIt = it;
    pathCount++;
    while (nodeIt != mutuallyForcedOut.end()) {
      const CFGNode *node = nodeIt->first;
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
      const CFGEdge *edge = profiledOuts[node].front();
      if (!victimEdge ||
          (edge->Sink->MappedAddr < victimEdge->Sink->MappedAddr)) {
        victimEdge = edge;
      }
      nodeIt = mutuallyForcedOut.find(nodeIt->second);
    }
  }

  // Remove the victim edges to break cycles in the mutually forced edges
  for (const CFGNode *node : cycleCutNodes)
    mutuallyForcedOut.erase(node);

  for(auto& elem: mutuallyForcedOut)
    MutuallyForcedOut.insert(elem);
}

// This function initializes the ExtTSP algorithm's data structures. This
// the NodeChainAssemblies and the CandidateChains maps.
void NodeChainBuilder::initializeExtTSP() {
  // For each chain, compute its ExtTSP score, add its chain assembly records
  // and its merge candidate chain.
  //warn("Started initialization: " + Twine(Chains.size()));
  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : Chains) {
    NodeChain *chain = elem.second.get();
    chain->Score = chain->Freq ? computeExtTSPScore(chain) : 0;
  }

  DenseSet<std::pair<NodeChain *, NodeChain *>> visited;


  for (DenseMapPair<uint64_t, std::unique_ptr<NodeChain>> &elem : Chains) {
    NodeChain * chain = elem.second.get();
    auto visit = [&visited, chain, this] (const CFGEdge& edge) {
      if (!edge.Weight)
        return;
      NodeChain *otherChain = getNodeChain(edge.Sink);
      if (chain == otherChain)
        return;
      if (visited.count(std::make_pair(chain, otherChain)))
        return;

      bool x = updateNodeChainAssembly(chain, otherChain);
      bool y = updateNodeChainAssembly(otherChain, chain);

      if (x || y) {
        CandidateChains[chain].insert(otherChain);
        CandidateChains[otherChain].insert(chain);
      }
      visited.insert(std::make_pair(chain, otherChain));
      visited.insert(std::make_pair(otherChain, chain));
    };

    for (const CFGNode *node : chain->Nodes) {
      if (!node->Freq)
        continue;
      if(config->propellerReorderIP)
        node->forEachOutEdgeRef(visit);
      else
        node->forEachIntraOutEdgeRef(visit);
    }
  }
  //warn("Finished initialization: " + Twine(Chains.size())  + " Heap size: " + Twine(NodeChainAssemblies.size()));
  /*
  if (DebugCFG) {
    for(const DenseMapPair<std::pair<NodeChain *, NodeChain *>,
        std::unique_ptr<NodeChainAssembly>> &elem: NodeChainAssemblies){
      fprintf(stderr, "%s\n", NodeChainBuilder::toString(*elem.second.get()).c_str());
    }
  }
  */
}

void NodeChainBuilder::mergeAllChains() {
  // Attach the mutually-foced edges (which will not be split anymore by the
  // Extended TSP algorithm).
  for (auto &elem : MutuallyForcedOut)
    attachNodes(elem.first, elem.second);

  for(auto& elem: Chains){
    auto * c_ptr = elem.second.get();
    auto visit = [c_ptr, this] (const CFGEdge& edge) {
      if (!edge.Weight)
        return;
      auto* sinkNodeChain = getNodeChain(edge.Sink);
      c_ptr->OutEdges[sinkNodeChain].push_back(&edge);
      sinkNodeChain->InEdges.insert(c_ptr);
    };

    if (config->propellerReorderIP) {
      for(const CFGNode * node: c_ptr->Nodes)
        node->forEachOutEdgeRef(visit);
    } else {
      for(const CFGNode * node: c_ptr->Nodes)
        node->forEachIntraOutEdgeRef(visit);
    }
  }


  // Initialize the Extended TSP algorithm's data.
  initializeExtTSP();

  // Keep merging the chain assembly record with the highest ExtTSP gain, until
  // no more gain is possible.
  bool merged = false;
  do {
    merged = false;

    if (!NodeChainAssemblies.empty()) {
      auto bestAssembly = NodeChainAssemblies.top();
      NodeChainAssemblies.pop();
      if(bestAssembly->extTSPScoreGain() > 0) {
        if (bestAssembly->SplitChain->DebugChain || bestAssembly->UnsplitChain->DebugChain)
          fprintf(stderr, "MERGING for %s\n", toString(*bestAssembly.get()).c_str());
        fprintf(stderr, "MERGING with score %.11f %u %u SPLIT(%d)\n", bestAssembly->extTSPScoreGain(), bestAssembly->SplitChain->Size, bestAssembly->UnsplitChain->Size, bestAssembly->split());
        mergeChains(std::move(bestAssembly));
        merged = true;
      }
    }
  } while (merged);

  // Merge fallthrough basic blocks if we have missed any
  attachFallThroughs();
}

// This function computes the ExtTSP score for a chain assembly record. This
// goes the three BB slices in the assembly record and considers all edges
// whose source and sink belongs to the chains in the assembly record.
double NodeChainBuilder::NodeChainAssembly::computeExtTSPScore() const {
  double score = 0;

  auto visit = [this, &score] (const CFGEdge& edge){
        uint8_t srcSliceIdx, sinkSliceIdx;
        if (!findSliceIndex(edge.Src, srcSliceIdx))
          return;

        if (!findSliceIndex(edge.Sink, sinkSliceIdx))
          return;

        auto srcNodeOffset = ChainBuilder->getNodeOffset(edge.Src);
        auto sinkNodeOffset = ChainBuilder->getNodeOffset(edge.Sink);

        bool edgeForward = (srcSliceIdx < sinkSliceIdx) ||
                           (srcSliceIdx == sinkSliceIdx &&
                            (srcNodeOffset + edge.Src->ShSize <= sinkNodeOffset));

        uint32_t srcSinkDistance = 0;

        if (srcSliceIdx == sinkSliceIdx) {
          srcSinkDistance = edgeForward
                       ? sinkNodeOffset - srcNodeOffset - edge.Src->ShSize
                       : srcNodeOffset - sinkNodeOffset + edge.Src->ShSize;
        } else {
            const NodeChainSlice &srcSlice = Slices[srcSliceIdx];
            const NodeChainSlice &sinkSlice = Slices[sinkSliceIdx];
            srcSinkDistance = edgeForward
                         ? srcSlice.EndOffset - srcNodeOffset - edge.Src->ShSize +
                               sinkNodeOffset - sinkSlice.BeginOffset
                         : srcNodeOffset - srcSlice.BeginOffset +
                               edge.Src->ShSize + sinkSlice.EndOffset -
                               sinkNodeOffset;
          // Increment the distance by the size of the middle slice if the src
          // and sink are from the two ends.
          if (std::abs(sinkSliceIdx - srcSliceIdx) == 2)
            srcSinkDistance += Slices[1].size();
        }

        score += getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);
      };


  score += UnsplitChain->Score;

  if (split())
    SplitChain->forEachOutEdgeToChain(SplitChain, visit);
  else
    score += SplitChain->Score;

  SplitChain->forEachOutEdgeToChain(UnsplitChain, visit);
  UnsplitChain->forEachOutEdgeToChain(SplitChain, visit);

  return score;
}

void NodeChainBuilder::doOrder(std::unique_ptr<ChainClustering> &CC){
  init();
  mergeAllChains();

  if(!config->propellerReorderIP)
    coalesceChains();

  for(auto& elem: Chains)
    CC->addChain(std::move(elem.second));
}

void ChainClustering::addChain(std::unique_ptr<const NodeChain>&& chain_ptr){
  for(const CFGNode *n: chain_ptr->Nodes)
    NodeToChainMap[n] = chain_ptr.get();
  auto& chainList = ((config->propellerReorderIP || config->propellerSplitFuncs || config->propellerReorderFuncs) && chain_ptr->Freq==0) ? ColdChains : HotChains;
  chainList.push_back(std::move(chain_ptr));
}

// Initialize a cluster containing a single chain an associates it with a unique
// id.
ChainClustering::Cluster::Cluster(const NodeChain *chain)
    : Chains(1, chain), DelegateChain(chain) {}

// Returns the most frequent caller of a function. This function also gets as
// the second parameter the cluster containing this function to save a lookup
// into the ChainToClusterMap.
ChainClustering::Cluster *
CallChainClustering::getMostLikelyPredecessor(const NodeChain * chain,
                                              Cluster *cluster) {
  DenseMap<Cluster*, uint64_t> clusterEdge;

  for(const CFGNode * n: chain->Nodes){
    auto visit = [&clusterEdge, n, chain, cluster, this] (const CFGEdge& edge){
      if (!edge.Weight)
        return;
      if (edge.isReturn())
        return;
      auto *caller = NodeToChainMap[edge.Src];
      if (!caller)
        return;
      auto * callerCluster = ChainToClusterMap[caller];
      assert(caller->Freq);
      if (caller == chain || callerCluster == cluster)
        return;
      if (callerCluster->Size > ClusterMergeSizeThreshold)
        return;
      // Ignore calls which are cold relative to the callee
      if (edge.Weight * 10 < n->Freq)
        return;
      // Do not merge if the caller cluster's density would degrade by more than
      // 1/8.
      if (8 * callerCluster->Size * (cluster->Weight * callerCluster->Weight) <
          callerCluster->Weight * (cluster->Size + callerCluster->Size))
        return;
      clusterEdge[callerCluster] += edge.Weight;
    };
    n->forEachInEdgeRef(visit);

  }

  auto bestCaller = std::max_element(clusterEdge.begin(), clusterEdge.end(), [] (const DenseMapPair<Cluster*, uint64_t>& p1,
                                                               const DenseMapPair<Cluster*, uint64_t>& p2) {
    if (p1.second == p2.second)
      return std::less<Cluster*>()(p1.first, p2.first);
    return p1.second < p2.second;
  });

  if (bestCaller == clusterEdge.end())
    return nullptr;
  return bestCaller->first;
}


void ChainClustering::sortClusters(std::vector<Cluster *> &clusterOrder) {
  for (auto &p : Clusters)
    clusterOrder.push_back(p.second.get());

  auto clusterComparator = [](Cluster *c1, Cluster *c2) -> bool {
    // Set a deterministic order when execution densities are equal.
    if (c1->getDensity() == c2->getDensity())
      return c1->DelegateChain->DelegateNode->MappedAddr <
          c2->DelegateChain->DelegateNode->MappedAddr;
    return c1->getDensity() > c2->getDensity();
  };

  std::sort(clusterOrder.begin(), clusterOrder.end(), clusterComparator);
}

void NoOrdering::doOrder(std::vector<const CFGNode*> &hotOrder,
                                  std::vector<const CFGNode*> &coldOrder){
  auto chainComparator = [](const std::unique_ptr<const NodeChain> &c_ptr1,
                            const std::unique_ptr<const NodeChain> &c_ptr2) -> bool {
    return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
  };

  std::sort(HotChains.begin(), HotChains.end(), chainComparator);
  std::sort(ColdChains.begin(), ColdChains.end(), chainComparator);

  for(auto& c_ptr: HotChains)
    for(const CFGNode* n: c_ptr->Nodes)
      hotOrder.push_back(n);

  for(auto& c_ptr: ColdChains)
    for(const CFGNode* n: c_ptr->Nodes)
    coldOrder.push_back(n);
}

// Merge clusters together based on the CallChainClustering algorithm.
void CallChainClustering::mergeClusters() {
  // Build a map for the execution density of each chain.
  DenseMap<const NodeChain *, double> chainWeightMap;

  for(auto& c_ptr: HotChains){
    const NodeChain * chain = c_ptr.get();
    chainWeightMap.try_emplace(chain, chain->execDensity());
  }

  // Sort the hot chains in decreasing order of their execution density.
  std::stable_sort(HotChains.begin(), HotChains.end(),
            [&chainWeightMap] (const std::unique_ptr<const NodeChain> &c_ptr1,
                               const std::unique_ptr<const NodeChain> &c_ptr2){
              return chainWeightMap[c_ptr1.get()] > chainWeightMap[c_ptr2.get()];
            });

  for (auto& c_ptr : HotChains){
    const NodeChain* chain = c_ptr.get();
    if (chainWeightMap[chain] <= 0.005)
      break;
    auto *cluster = ChainToClusterMap[chain];
    // Ignore merging if the cluster containing this function is bigger than
    // 2MBs (size of a large page).
    if (cluster->Size > ClusterMergeSizeThreshold)
      continue;
    assert(cluster);

    Cluster *predecessorCluster = getMostLikelyPredecessor(chain, cluster);
    if (!predecessorCluster)
      continue;

    //assert(predecessorCluster != cluster && predecessorChain != chain);
    mergeTwoClusters(predecessorCluster, cluster);
  }
}


void ChainClustering::doOrder(std::vector<const CFGNode*> &hotOrder,
                                  std::vector<const CFGNode*> &coldOrder){
  //warn("[propeller]" + Twine(HotChains.size())+ " Hot chains and " + Twine(ColdChains.size()) + " Cold chains.");
  initClusters();
  mergeClusters();
  std::vector<Cluster *> clusterOrder;
  sortClusters(clusterOrder);
  for (Cluster *cl: clusterOrder)
    for(const NodeChain* c: cl->Chains)
      for(const CFGNode *n: c->Nodes)
        hotOrder.push_back(n);

  auto chainComparator = [](const std::unique_ptr<const NodeChain> &c_ptr1,
                            const std::unique_ptr<const NodeChain> &c_ptr2) -> bool {
    return c_ptr1->DelegateNode->MappedAddr < c_ptr2->DelegateNode->MappedAddr;
  };

  std::sort(ColdChains.begin(), ColdChains.end(), chainComparator);

  for(auto &c_ptr: ColdChains)
    for(const CFGNode* n: c_ptr->Nodes)
    coldOrder.push_back(n);
}

void PropellerBBReordering::printStats() {

  DenseMap<const CFGNode*, uint32_t> nodeAddressMap;
  llvm::StringMap<uint32_t> functionPartitions;
  uint32_t currentAddress = 0;
  const ControlFlowGraph* currentCFG = nullptr;
  for(const CFGNode* n: HotOrder){
    if (currentCFG != n->CFG){
      currentCFG = n->CFG;
      functionPartitions[currentCFG->Name]++;
    }
    nodeAddressMap[n]=currentAddress;
    currentAddress += n->ShSize;
  }

  for(auto &elem: functionPartitions){
    fprintf(stderr, "FUNCTION PARTITIONS: %s,%u\n", elem.first().str().c_str(), elem.second);
  }

  std::vector<uint64_t> distances({0,128, 640, 1028, 4096, 65536, 2 << 20, std::numeric_limits<uint64_t>::max()});
  std::map<uint64_t, uint64_t> histogram;
  llvm::StringMap<double> extTSPScoreMap;
  for(const CFGNode* n: HotOrder) {
    n->forEachOutEdgeRef([&nodeAddressMap, &distances, &histogram, &extTSPScoreMap](const CFGEdge& edge){
      if (!edge.Weight)
        return;
      if (edge.isReturn())
        return;
      if (nodeAddressMap.find(edge.Src)==nodeAddressMap.end() || nodeAddressMap.find(edge.Sink)==nodeAddressMap.end())
        return;
      uint64_t srcOffset = nodeAddressMap[edge.Src];
      uint64_t sinkOffset = nodeAddressMap[edge.Sink];
      bool edgeForward = srcOffset + edge.Src->ShSize <= sinkOffset;
      uint32_t srcSinkDistance = edgeForward ? sinkOffset - srcOffset - edge.Src->ShSize: srcOffset - sinkOffset + edge.Src->ShSize;

      if (edge.Type == CFGEdge::EdgeType::INTRA_FUNC || edge.Type == CFGEdge::EdgeType::INTRA_DYNA)
        extTSPScoreMap[edge.Src->CFG->Name] += getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);

      auto res = std::lower_bound(distances.begin(), distances.end(), srcSinkDistance);
      histogram[*res] += edge.Weight;
    });
  }

  for(auto& elem: extTSPScoreMap)
    fprintf(stderr, "Ext TSP Score: %s %.6f\n", elem.first().str().c_str(), elem.second);
  fprintf(stderr, "DISTANCE HISTOGRAM: ");
  for(auto elem: histogram)
    fprintf(stderr, "\t[%lu -> %lu]", elem.first, elem.second);
  fprintf(stderr, "\n");
}

} // namespace propeller
} // namespace lld
