//===- PropellerNodeChainAssembly.h  --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file includes the declaration of the NodeChainAssembly class. Each
// instance of this class gives a recipe for merging two NodeChains together in
// addition to the ExtTSP score gain that will be achieved by that merge. Each
// NodeChainAssembly consists of three NodeChainSlices from two node chains: the
// (potentially) splitted chain, and the unsplit chain. A NodeChainSlice has
// represents a slice of a NodeChain by storing iterators to the beginning and
// end of that slice in the node chain, plus the binary offsets at which these
// slices begin and end. The offsets allow effient computation of the gain in
// ExtTSP score.
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_NODE_CHAIN_ASSEMBLY_H
#define LLD_ELF_PROPELLER_NODE_CHAIN_ASSEMBLY_H

#include "NodeChain.h"
#include "PropellerCFG.h"

#include <list>
#include <vector>

namespace lld {
namespace propeller {

double getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
                          uint64_t srcSinkDistance);

class NodeChainSlice {
public:
  // Chain from which this slice comes from
  NodeChain *Chain;

  // The endpoints of the slice in the corresponding chain
  std::list<CFGNode *>::iterator Begin, End;

  // The offsets corresponding to the two endpoints
  uint64_t BeginOffset, EndOffset;

  // Constructor for building a chain slice from a given chain and the two
  // endpoints of the chain.
  NodeChainSlice(NodeChain *c, std::list<CFGNode *>::iterator begin,
                 std::list<CFGNode *>::iterator end)
      : Chain(c), Begin(begin), End(end) {

    BeginOffset = (*begin)->ChainOffset;
    if (End == Chain->Nodes.end())
      EndOffset = Chain->Size;
    else
      EndOffset = (*end)->ChainOffset;
  }

  // (Binary) size of this slice
  uint64_t size() const { return EndOffset - BeginOffset; }
};

// This enum represents the order in which three slices (S1, S2, and U) are
// merged together.
enum MergeOrder {
  Begin,
  S2S1U = Begin,
  BeginNext,
  S1US2 = BeginNext,
  S2US1,
  US2S1,
  End
};

class NodeChainAssembly {
public:
  // The gain in ExtTSP score achieved by this NodeChainAssembly once it
  // is accordingly applied to the two chains.
  // This is effectively equal to "Score - splitChain->Score -
  // unsplitChain->Score".
  double ScoreGain = 0;

  // The two chains, the first being the splitChain and the second being the
  // unsplitChain.
  std::pair<NodeChain *, NodeChain *> ChainPair;

  // The slice position of splitchain
  std::list<CFGNode *>::iterator SlicePosition;

  // The three chain slices
  std::vector<NodeChainSlice> Slices;

  // The merge order of the slices
  MergeOrder MOrder;

  // The constructor for creating a NodeChainAssembly. slicePosition must be an
  // iterator into chainX.
  NodeChainAssembly(NodeChain *splitChain, NodeChain *unsplitChain,
                    std::list<CFGNode *>::iterator slicePosition,
                    MergeOrder mOrder)
      : ChainPair(splitChain, unsplitChain), SlicePosition(slicePosition),
        MOrder(mOrder) {
    NodeChainSlice s1(splitChain, splitChain->Nodes.begin(), SlicePosition);
    NodeChainSlice s2(splitChain, SlicePosition, splitChain->Nodes.end());
    NodeChainSlice u(unsplitChain, unsplitChain->Nodes.begin(), unsplitChain->Nodes.end());

    switch (MOrder) {
    case MergeOrder::S2S1U:
      Slices = {std::move(s2), std::move(s1), std::move(u)};
      break;
    case MergeOrder::S1US2:
      Slices = {std::move(s1), std::move(u), std::move(s2)};
      break;
    case MergeOrder::S2US1:
      Slices = {std::move(s2), std::move(u), std::move(s1)};
      break;
    case MergeOrder::US2S1:
      Slices = {std::move(u), std::move(s2), std::move(s1)};
      break;
    default:
      assert("Invalid MergeOrder!" && false);
    }

    // Set the ExtTSP Score gain as the difference between the new score after
    // merging these chains and the current scores of the two chains.
    ScoreGain =
        computeExtTSPScore() - splitChain->Score - unsplitChain->Score;
  }

  bool isValid() { return ScoreGain > 0.0001; }

  // Find the NodeChainSlice in this NodeChainAssembly which contains the given
  // node. If the node is not contained in this NodeChainAssembly, then return
  // false. Otherwise, set idx equal to the index of the corresponding slice and
  // return true.
  bool findSliceIndex(CFGNode *node, NodeChain *chain, uint64_t offset,
                      uint8_t &idx) const;

  // This function computes the ExtTSP score for a chain assembly record. This
  // goes the three BB slices in the assembly record and considers all edges
  // whose source and sink belongs to the chains in the assembly record.
  double computeExtTSPScore() const;

  // First node in the resulting assembled chain.
  CFGNode *getFirstNode() const {
    for (auto &slice : Slices)
      if (slice.Begin != slice.End)
        return *slice.Begin;
    return nullptr;
  }

  struct CompareNodeChainAssembly {
    bool operator()(const std::unique_ptr<NodeChainAssembly> &a1,
                    const std::unique_ptr<NodeChainAssembly> &a2) const;
  };

  friend class NodeChainBuilder;

  // We delete the copy constructor to make sure NodeChainAssembly is moved
  // rather than copied.
  NodeChainAssembly(NodeChainAssembly &&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;

  std::pair<uint8_t, size_t> assemblyStrategy() const {
    return std::make_pair(MOrder, (*SlicePosition)->MappedAddr);
  }

  inline bool splits() const {
    return SlicePosition != splitChain()->Nodes.begin();
  }

  inline bool splitsAtFunctionTransition() const {
    return splits() && ((*std::prev(SlicePosition))->CFG != (*SlicePosition)->CFG);
  }

  inline bool needsSplitChainRotation() {
    return (MOrder == S2S1U && splits()) || MOrder == S2US1 || MOrder == US2S1;
  }


  inline NodeChain *splitChain() const { return ChainPair.first; }

  inline NodeChain *unsplitChain() const { return ChainPair.second; }

  inline MergeOrder mergeOrder() const { return MOrder; }
};

std::string toString(NodeChainAssembly &assembly);

} // namespace propeller
} // namespace lld

#endif
