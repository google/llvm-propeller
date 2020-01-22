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

#include "PropellerCFG.h"
#include "PropellerNodeChain.h"

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

// This enum represents the order in which three slices (X1, X2, and Y) are
// merged together.
enum MergeOrder {
  Begin,
  X2X1Y = Begin,
  BeginNext,
  X1YX2 = BeginNext,
  X2YX1,
  YX2X1,
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
  NodeChainAssembly(NodeChain *chainX, NodeChain *chainY,
                    std::list<CFGNode *>::iterator slicePosition,
                    MergeOrder mOrder)
      : ChainPair(chainX, chainY), SlicePosition(slicePosition),
        MOrder(mOrder) {
    NodeChainSlice x1(chainX, chainX->Nodes.begin(), SlicePosition);
    NodeChainSlice x2(chainX, SlicePosition, chainX->Nodes.end());
    NodeChainSlice y(chainY, chainY->Nodes.begin(), chainY->Nodes.end());

    switch (MOrder) {
    case MergeOrder::X2X1Y:
      Slices = {std::move(x2), std::move(x1), std::move(y)};
      break;
    case MergeOrder::X1YX2:
      Slices = {std::move(x1), std::move(y), std::move(x2)};
      break;
    case MergeOrder::X2YX1:
      Slices = {std::move(x2), std::move(y), std::move(x1)};
      break;
    case MergeOrder::YX2X1:
      Slices = {std::move(y), std::move(x2), std::move(x1)};
      break;
    default:
      assert("Invalid MergeOrder!" && false);
    }

    // Set the ExtTSP Score gain as the difference between the new score after
    // merging these chains and the current scores of the two chains.
    ScoreGain =
        computeExtTSPScore() - splitChain()->Score - unsplitChain()->Score;
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

  inline bool split() const {
    return SlicePosition != splitChain()->Nodes.begin();
  }

  inline NodeChain *splitChain() const { return ChainPair.first; }

  inline NodeChain *unsplitChain() const { return ChainPair.second; }

  inline MergeOrder mergeOrder() const { return MOrder; }
};

std::string toString(NodeChainAssembly &assembly);

} // namespace propeller
} // namespace lld

#endif
