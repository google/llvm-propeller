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
#ifndef LLD_ELF_PROPELLER_CODE_LAYOUT_NODE_CHAIN_ASSEMBLY_H
#define LLD_ELF_PROPELLER_CODE_LAYOUT_NODE_CHAIN_ASSEMBLY_H

#include "NodeChain.h"
#include "PropellerCFG.h"

#include <list>
#include <vector>

namespace lld {
namespace propeller {

uint64_t getEdgeExtTSPScore(const CFGEdge &edge, bool isEdgeForward,
                            uint64_t srcSinkDistance);

// This class defines a slices of a node chain, specified by iterators to the
// beginning and end of the slice.
class NodeChainSlice {
public:
  // chain from which this slice comes from
  NodeChain *chain;

  // The endpoints of the slice in the corresponding chain
  std::list<CFGNode *>::iterator Begin, End;

  // The offsets corresponding to the two endpoints
  uint64_t BeginOffset, EndOffset;

  // Constructor for building a chain slice from a given chain and the two
  // endpoints of the chain.
  NodeChainSlice(NodeChain *c, std::list<CFGNode *>::iterator begin,
                 std::list<CFGNode *>::iterator end)
      : chain(c), Begin(begin), End(end) {

    BeginOffset = (*begin)->chainOffset;
    if (End == chain->nodes.end())
      EndOffset = chain->size;
    else
      EndOffset = (*end)->chainOffset;
  }

  // Constructor for building a chain slice from a node chain containing all of
  // its nodes.
  NodeChainSlice(NodeChain *c)
      : chain(c), Begin(c->nodes.begin()), End(c->nodes.end()), BeginOffset(0),
        EndOffset(c->size) {}

  // (Binary) size of this slice
  uint64_t size() const { return EndOffset - BeginOffset; }

  bool isEmpty() const { return Begin == End; }
};

// This enum represents the order in which three slices (S1, S2, and U) are
// merged together. We exclude S1S2U and US1S2 since they are respectively
// equivalent to S2S1U (with S2 being empty) and U2U1S (with U2 being empty).
enum MergeOrder {
  Begin,
  S2S1U = Begin,
  BeginNext,
  S1US2 = BeginNext,
  S2US1,
  US2S1,
  End
};

// This class defines a strategy for assembling two chains together with one of
// the chains potentially split into two chains.
class NodeChainAssembly {
public:
  // The gain in ExtTSP score achieved by this NodeChainAssembly once it
  // is accordingly applied to the two chains.
  // This is effectively equal to "Score - splitChain->Score -
  // unsplitChain->Score".
  uint64_t ScoreGain = 0;

  // The two chains, the first being the splitChain and the second being the
  // unsplitChain.
  std::pair<NodeChain *, NodeChain *> ChainPair;

  // The splitting position in splitchain
  std::list<CFGNode *>::iterator SlicePosition;

  // The three chain slices
  std::vector<NodeChainSlice> Slices;

  // The merge order of the slices
  MergeOrder MOrder;

  // The constructor for creating a NodeChainAssembly. slicePosition must be an
  // iterator into splitChain->nodes.
  NodeChainAssembly(NodeChain *splitChain, NodeChain *unsplitChain,
                    std::list<CFGNode *>::iterator slicePosition,
                    MergeOrder mOrder)
      : ChainPair(splitChain, unsplitChain), SlicePosition(slicePosition),
        MOrder(mOrder) {
    // Construct the slices.
    NodeChainSlice s1(splitChain, splitChain->nodes.begin(), SlicePosition);
    NodeChainSlice s2(splitChain, SlicePosition, splitChain->nodes.end());
    NodeChainSlice u(unsplitChain);

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
    auto assemblyScore = computeExtTSPScore();
    auto chainsScore = splitChain->Score + unsplitChain->Score;
    ScoreGain = assemblyScore > chainsScore ? assemblyScore - chainsScore : 0;
  }

  bool isValid() { return ScoreGain; }

  // Find the NodeChainSlice in this NodeChainAssembly which contains the given
  // node. If the node is not contained in this NodeChainAssembly, then return
  // false. Otherwise, set idx equal to the index of the corresponding slice and
  // return true.
  bool findSliceIndex(CFGNode *node, NodeChain *chain, uint64_t offset,
                      uint8_t &idx) const;

  // This function computes the ExtTSP score for a chain assembly record. This
  // goes over the three bb slices in the assembly record and considers all
  // edges whose source and sink belong to the chains in the assembly record.
  uint64_t computeExtTSPScore() const;

  // First node in the resulting assembled chain.
  CFGNode *getFirstNode() const {
    for (auto &slice : Slices)
      if (!slice.isEmpty())
        return *slice.Begin;
    return nullptr;
  }

  // Comparator for two NodeChainAssemblies. It compare ScoreGain and break ties
  // consistently.
  struct CompareNodeChainAssembly {
    bool operator()(const std::unique_ptr<NodeChainAssembly> &a1,
                    const std::unique_ptr<NodeChainAssembly> &a2) const;
  };

  // We delete the copy constructor to make sure NodeChainAssembly is moved
  // rather than copied.
  NodeChainAssembly(NodeChainAssembly &&) = default;
  // copy constructor is implicitly deleted
  // NodeChainAssembly(NodeChainAssembly&) = delete;
  NodeChainAssembly() = delete;

  // This returns a unique value for every different assembly record between two
  // chains. When ChainPair is equal, this helps differentiate and compare the
  // two assembly records.
  std::pair<uint8_t, size_t> assemblyStrategy() const {
    return std::make_pair(MOrder, (*SlicePosition)->mappedAddr);
  }

  inline bool splits() const {
    return SlicePosition != splitChain()->nodes.begin();
  }

  inline bool splitsAtFunctionTransition() const {
    return splits() &&
           ((*std::prev(SlicePosition))->controlFlowGraph != (*SlicePosition)->controlFlowGraph);
  }

  inline bool needsSplitChainRotation() {
    return (MOrder == S2S1U && splits()) || MOrder == S2US1 || MOrder == US2S1;
  }

  inline NodeChain *splitChain() const { return ChainPair.first; }

  inline NodeChain *unsplitChain() const { return ChainPair.second; }
};

std::string toString(NodeChainAssembly &assembly);

} // namespace propeller
} // namespace lld

#endif
