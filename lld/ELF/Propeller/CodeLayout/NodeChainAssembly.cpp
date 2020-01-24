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
double getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
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
           (1.0 -
            ((double)srcSinkDistance) / propellerConfig.optForwardJumpDistance);

  if (!isEdgeForward &&
      srcSinkDistance < propellerConfig.optBackwardJumpDistance)
    return edge.Weight * propellerConfig.optBackwardJumpWeight *
           (1.0 - ((double)srcSinkDistance) /
                      propellerConfig.optBackwardJumpDistance);
  return 0;
}

bool NodeChainAssembly::findSliceIndex(CFGNode *node, NodeChain *chain,
                                       uint64_t offset, uint8_t &idx) const {
  for (idx = 0; idx < 3; ++idx) {
    if (chain != Slices[idx].Chain)
      continue;
    // We find if the node's offset lies with the begin and end offset of this
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
double NodeChainAssembly::computeExtTSPScore() const {
  // Zero-initialize the score.
  double score = 0;

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
      if (std::abs(sinkSliceIdx - srcSliceIdx) == 2)
        srcSinkDistance += Slices[1].size();
    }

    score += getEdgeExtTSPScore(edge, edgeForward, srcSinkDistance);
  };

  // No changes will be made to the score that is contributed by the unsplit
  // chain and we can simply increment by the chain's stored score.
  score += unsplitChain()->Score;

  // We need to recompute the score induced by the split chain (if it has really
  // been split) as the offsets of the nodes have changed.
  if (split())
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
    if (std::less<std::pair<NodeChain *, NodeChain *>>()(a1->ChainPair,
                                                         a2->ChainPair))
      return true;
    if (std::less<std::pair<NodeChain *, NodeChain *>>()(a2->ChainPair,
                                                         a1->ChainPair))
      return false;
    return a1->assemblyStrategy() < a2->assemblyStrategy();
  }
  return a1->ScoreGain < a2->ScoreGain;
}

static std::string toString(MergeOrder mOrder) {
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

std::string toString(NodeChainAssembly &assembly) {
  std::string str("assembly record between:\n");
  str += lld::propeller::toString(*assembly.splitChain()) + " as X\n";
  str += lld::propeller::toString(*assembly.unsplitChain()) + " as Y\n";
  // str += "split position (X):, " + std::to_string(assembly.SlicePosition -
  // assembly.splitChain()->Nodes.begin()) + "\n";
  str += "merge order: " + lld::propeller::toString(assembly.MOrder) + "\n";
  str += "ScoreGain: " + std::to_string(assembly.ScoreGain);
  return str;
}

} // namespace propeller
} // namespace lld
