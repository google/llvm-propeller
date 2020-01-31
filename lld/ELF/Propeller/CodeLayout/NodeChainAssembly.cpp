//===- NodeChainAssembly.cpp  ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "NodeChainAssembly.h"
#include "NodeChain.h"

namespace lld {
namespace propeller {

// Return the Extended TSP score for one edge, given its source to sink
// direction and distance in the layout.
uint64_t getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
                            uint64_t srcSinkDistance) {

  // Approximate callsites to be in the middle of the source basic block.
  if (edge.isCall()) {
    if (isEdgeForward)
      srcSinkDistance += edge.Src->ShSize / 2;
    else
      srcSinkDistance -= edge.Src->ShSize / 2;
  }

  if (edge.isReturn()) {
    if (isEdgeForward)
      srcSinkDistance += edge.Sink->ShSize / 2;
    else
      srcSinkDistance -= edge.Sink->ShSize / 2;
  }

  if (srcSinkDistance == 0 && (edge.Type == CFGEdge::EdgeType::INTRA_FUNC ||
                               edge.Type == CFGEdge::EdgeType::INTRA_DYNA))
    return edge.Weight * propellerConfig.optFallthroughWeight;

  if (isEdgeForward && srcSinkDistance < propellerConfig.optForwardJumpDistance)
    return edge.Weight * propellerConfig.optForwardJumpWeight *
           (propellerConfig.optForwardJumpDistance - srcSinkDistance);

  if (!isEdgeForward &&
      srcSinkDistance < propellerConfig.optBackwardJumpDistance)
    return edge.Weight * propellerConfig.optBackwardJumpWeight *
           (propellerConfig.optBackwardJumpDistance - srcSinkDistance);
  return 0;
}

bool NodeChainAssembly::findSliceIndex(CFGNode *node, NodeChain *chain,
                                       uint64_t offset, uint8_t &idx) const {
  for (idx = 0; idx < 3; ++idx) {
    if (chain != Slices[idx].Chain)
      continue;
    // We find if the node's offset lies within the begin and end offset of this
    // slice.
    if (offset < Slices[idx].BeginOffset || offset > Slices[idx].EndOffset)
      continue;
    if (offset < Slices[idx].EndOffset && offset > Slices[idx].BeginOffset)
      return true;
    // A node can have zero size, which means multiple nodes may be associated
    // with the same offset. This means that if the node's offset is at the
    // beginning or the end of the slice, the node may reside in either slices
    // of the chain.
    if (offset == Slices[idx].EndOffset) {
      // If offset is at the end of the slice, iterate backwards over the
      // slice to find a zero-sized node.
      for (auto nodeIt = std::prev(Slices[idx].End);
           nodeIt != std::prev(Slices[idx].Begin); nodeIt--) {
        // Stop iterating if the node's size is non-zero as this would change
        // the offset.
        if ((*nodeIt)->ShSize)
          break;
        // Have we found the node?
        if (*nodeIt == node)
          return true;
      }
    }
    if (offset == Slices[idx].BeginOffset) {
      // If offset is at the beginning of the slice, iterate forwards over the
      // slice to find the node.
      for (auto nodeIt = Slices[idx].Begin; nodeIt != Slices[idx].End;
           nodeIt++) {
        if (*nodeIt == node)
          return true;
        // Stop iterating if the node's size is non-zero as this would change
        // the offset.
        if ((*nodeIt)->ShSize)
          break;
      }
    }
  }
  return false;
}

// This function computes the ExtTSP score for a chain assembly record. This
// goes the three BB slices in the assembly record and considers all edges
// whose source and sink belongs to the chains in the assembly record.
uint64_t NodeChainAssembly::computeExtTSPScore() const {
  // Zero-initialize the score.
  uint64_t score = 0;

  auto addEdgeScore = [this, &score](CFGEdge &edge, NodeChain *srcChain,
                                     NodeChain *sinkChain) {
    uint8_t srcSliceIdx, sinkSliceIdx;
    auto srcNodeOffset = edge.Src->ChainOffset;
    auto sinkNodeOffset = edge.Sink->ChainOffset;
    if (!findSliceIndex(edge.Src, srcChain, srcNodeOffset, srcSliceIdx))
      return;

    if (!findSliceIndex(edge.Sink, sinkChain, sinkNodeOffset, sinkSliceIdx))
      return;

    bool edgeForward = (srcSliceIdx < sinkSliceIdx) ||
                       (srcSliceIdx == sinkSliceIdx &&
                        (srcNodeOffset + edge.Src->ShSize <= sinkNodeOffset));

    uint64_t srcSinkDistance = 0;

    if (srcSliceIdx == sinkSliceIdx) {
      srcSinkDistance = edgeForward
                            ? sinkNodeOffset - srcNodeOffset - edge.Src->ShSize
                            : srcNodeOffset - sinkNodeOffset + edge.Src->ShSize;
    } else {
      const NodeChainSlice &srcSlice = Slices[srcSliceIdx];
      const NodeChainSlice &sinkSlice = Slices[sinkSliceIdx];
      srcSinkDistance =
          edgeForward
              ? srcSlice.EndOffset - srcNodeOffset - edge.Src->ShSize +
                    sinkNodeOffset - sinkSlice.BeginOffset
              : srcNodeOffset - srcSlice.BeginOffset + edge.Src->ShSize +
                    sinkSlice.EndOffset - sinkNodeOffset;
      // Increment the distance by the size of the middle slice if the src
      // and sink are from the two ends.
      if (std::abs(((int16_t)sinkSliceIdx) - ((int16_t)srcSliceIdx)) == 2)
        srcSinkDistance += Slices[1].size();
    }

    score += getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);
  };

  // No changes will be made to the score that is contributed by the unsplit
  // chain and we can simply increment by the chain's stored score.
  score += unsplitChain()->Score;

  // We need to recompute the score induced by the split chain (if it has really
  // been split) as the offsets of the nodes have changed.
  if (splits())
    splitChain()->forEachOutEdgeToChain(splitChain(), addEdgeScore);
  else
    score += splitChain()->Score;

  // Consider the contribution to score for inter-chain edges.
  splitChain()->forEachOutEdgeToChain(unsplitChain(), addEdgeScore);
  unsplitChain()->forEachOutEdgeToChain(splitChain(), addEdgeScore);

  return score;
}

bool NodeChainAssembly::CompareNodeChainAssembly::operator()(
    const std::unique_ptr<NodeChainAssembly> &a1,
    const std::unique_ptr<NodeChainAssembly> &a2) const {

  if (a1->ScoreGain == a2->ScoreGain) {
    // If score gains are equal, we pick a consistent order based on the chains
    // in the assembly records
    if (std::less<std::pair<NodeChain *, NodeChain *>>()(a1->ChainPair,
                                                         a2->ChainPair))
      return true;
    if (std::less<std::pair<NodeChain *, NodeChain *>>()(a2->ChainPair,
                                                         a1->ChainPair))
      return false;
    // When even the chain pairs are the same, we resort to the assembly
    // strategy to pick a consistent order.
    return a1->assemblyStrategy() < a2->assemblyStrategy();
  }
  return a1->ScoreGain < a2->ScoreGain;
}

static std::string toString(MergeOrder mOrder) {
  switch (mOrder) {
  case MergeOrder::S2S1U:
    return "S2S1U";
  case MergeOrder::S1US2:
    return "S1US2";
  case MergeOrder::S2US1:
    return "S2US1";
  case MergeOrder::US2S1:
    return "US2S1";
  default:
    assert("Invalid MergeOrder!" && false);
    return "";
  }
}

std::string toString(NodeChainAssembly &assembly) {
  std::string str("assembly record between:\n");
  str += toString(*assembly.splitChain(), assembly.SlicePosition) + " as S\n";
  str += toString(*assembly.unsplitChain()) + " as U\n";
  str += "merge order: " + toString(assembly.MOrder) + "\n";
  str += "ScoreGain: " + std::to_string(assembly.ScoreGain);
  return str;
}

} // namespace propeller
} // namespace lld
