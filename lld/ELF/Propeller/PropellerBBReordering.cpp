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

#include "PropellerConfig.h"
#include "llvm/ADT/DenseSet.h"

#include <numeric>
#include <vector>

using llvm::DenseSet;

namespace lld {
namespace propeller {

void PropellerBBReordering::printStats() {

  DenseMap<CFGNode*, uint64_t> nodeAddressMap;
  llvm::StringMap<unsigned> functionPartitions;
  uint64_t currentAddress = 0;
  ControlFlowGraph* currentCFG = nullptr;
  for(CFGNode* n: HotOrder){
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
  for(CFGNode* n: HotOrder) {
    auto scoreEntry = extTSPScoreMap.try_emplace(n->CFG->Name, 0).first;
    n->forEachOutEdgeRef([&nodeAddressMap, &distances, &histogram, &scoreEntry](CFGEdge& edge){
      if (!edge.Weight)
        return;
      if (edge.isReturn())
        return;
      if (nodeAddressMap.find(edge.Src)==nodeAddressMap.end() || nodeAddressMap.find(edge.Sink)==nodeAddressMap.end())
        return;
      uint64_t srcOffset = nodeAddressMap[edge.Src];
      uint64_t sinkOffset = nodeAddressMap[edge.Sink];
      bool edgeForward = srcOffset + edge.Src->ShSize <= sinkOffset;
      uint64_t srcSinkDistance = edgeForward ? sinkOffset - srcOffset - edge.Src->ShSize: srcOffset - sinkOffset + edge.Src->ShSize;

      if (edge.Type == CFGEdge::EdgeType::INTRA_FUNC || edge.Type == CFGEdge::EdgeType::INTRA_DYNA)
        scoreEntry->second += getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);

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
