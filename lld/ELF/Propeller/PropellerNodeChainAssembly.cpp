#include "PropellerNodeChainAssembly.h"

namespace lld {
namespace propeller {

// Return the Extended TSP score for one edge, given its source to sink
// direction and distance in the layout.
double getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
                          uint64_t srcSinkDistance) {
  if (edge.isCall()){
    if (isEdgeForward)
      srcSinkDistance += edge.Src->ShSize / 2;
    else
      srcSinkDistance -= edge.Src->ShSize / 2;
  }

  if (edge.isReturn()){
    if (isEdgeForward)
      srcSinkDistance += edge.Sink->ShSize / 2;
    else
      srcSinkDistance -= edge.Sink->ShSize / 2;
  }

  if (srcSinkDistance == 0 && (edge.Type == CFGEdge::EdgeType::INTRA_FUNC || edge.Type == CFGEdge::EdgeType::INTRA_DYNA))
    return edge.Weight * propellerConfig.optFallthroughWeight;

  if (isEdgeForward && srcSinkDistance < propellerConfig.optForwardJumpDistance)
    return edge.Weight * propellerConfig.optForwardJumpWeight *
           (1.0 -
            ((double)srcSinkDistance) / propellerConfig.optForwardJumpDistance);

  if (!isEdgeForward && srcSinkDistance < propellerConfig.optBackwardJumpDistance)
    return edge.Weight * propellerConfig.optBackwardJumpWeight *
           (1.0 -
            ((double)srcSinkDistance) / propellerConfig.optBackwardJumpDistance);
  return 0;
}



// This function computes the ExtTSP score for a chain assembly record. This
// goes the three BB slices in the assembly record and considers all edges
// whose source and sink belongs to the chains in the assembly record.
double NodeChainAssembly::computeExtTSPScore() const {
  double score = 0;

  auto visit = [this, &score] (CFGEdge& edge, NodeChain * srcChain, NodeChain *sinkChain){
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


  score += unsplitChain()->Score;

  if (split())
    splitChain()->forEachOutEdgeToChain(splitChain(), visit);
  else
    score += splitChain()->Score;

  splitChain()->forEachOutEdgeToChain(unsplitChain(), visit);
  unsplitChain()->forEachOutEdgeToChain(splitChain(), visit);

  return score;
}

bool NodeChainAssembly::CompareNodeChainAssembly::operator () (
    const std::unique_ptr<NodeChainAssembly> &a1,
    const std::unique_ptr<NodeChainAssembly> &a2) const {

  if (a1->ScoreGain == a2->ScoreGain){
    if (std::less<std::pair<NodeChain*,NodeChain*>>()(a1->ChainPair,a2->ChainPair))
        return true;
    if (std::less<std::pair<NodeChain*,NodeChain*>>()(a2->ChainPair,a1->ChainPair))
        return false;
    return a1->assemblyStrategy() < a2->assemblyStrategy();
  }
  return a1->ScoreGain < a2->ScoreGain;
}

static std::string toString(MergeOrder mOrder){
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
  //str += "split position (X):, " + std::to_string(assembly.SlicePosition - assembly.splitChain()->Nodes.begin()) + "\n";
  str += "merge order: " + lld::propeller::toString(assembly.MOrder) + "\n";
  str += "ScoreGain: " + std::to_string(assembly.ScoreGain);
  return str;
}


} //namespace propeller
} //namespace lld
