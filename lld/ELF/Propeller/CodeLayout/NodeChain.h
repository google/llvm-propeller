//===- PropellerNodeChain.h  ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_CODE_LAYOUT_NODE_CHAIN_H
#define LLD_ELF_PROPELLER_CODE_LAYOUT_NODE_CHAIN_H

#include "PropellerCFG.h"
#include "llvm/ADT/DenseSet.h"

#include <list>
#include <unordered_map>
#include <vector>

using llvm::DenseSet;

namespace lld {
namespace propeller {

// Represents a chain of nodes (basic blocks).
class NodeChain {
public:
  // Representative node of the chain, with which it is initially constructed.
  CFGNode *DelegateNode;

  // controlFlowGraph of the nodes in this chain (this will be null if the nodes come from
  // more than one cfg).
  ControlFlowGraph *controlFlowGraph;

  // Ordered list of the nodes in this chain.
  std::list<CFGNode *> nodes;

  // Iterators to the positions in the chain where the chain transitions from
  // one function to another.
  std::list<std::list<CFGNode *>::iterator> FunctionTransitions;

  // Out edges for this chain, to its own nodes and nodes of other chains.
  std::unordered_map<NodeChain *, std::vector<CFGEdge *>> OutEdges;

  // Chains which have outgoing edges to this chain.
  DenseSet<NodeChain *> InEdges;

  // Total binary size of the chain.
  uint64_t size;

  // Total execution frequency of the chain.
  uint64_t freq;

  // Extended TSP score of the chain.
  uint64_t Score = 0;

  // Whether to print out information about how this chain joins with others.
  bool DebugChain;

  // Constructor for building a NodeChain from a single Node
  NodeChain(CFGNode *node)
      : DelegateNode(node), controlFlowGraph(node->controlFlowGraph), nodes(1, node), size(node->shSize),
        freq(node->freq), DebugChain(node->controlFlowGraph->debugCFG) {}

  // Constructor for building a NodeChain from all nodes in the controlFlowGraph according to
  // the initial order.
  NodeChain(ControlFlowGraph *cfg)
      : DelegateNode(cfg->getEntryNode()), controlFlowGraph(cfg), size(cfg->size), freq(0),
        DebugChain(cfg->debugCFG) {
    cfg->forEachNodeRef([this](CFGNode &node) {
      nodes.push_back(&node);
      freq += node.freq;
    });
  }

  // Helper function to iterate over the outgoing edges of this chain to a
  // specific chain, while applying a given function on each edge.
  template <class Visitor>
  void forEachOutEdgeToChain(NodeChain *chain, Visitor V) {
    auto it = OutEdges.find(chain);
    if (it == OutEdges.end())
      return;
    for (CFGEdge *E : it->second)
      V(*E, this, chain);
  }

  // This returns the execution density of the chain.
  double execDensity() const {
    return ((double)freq) / std::max(size, (uint64_t)1);
  }

  bool isSameCFG(const NodeChain &c) { return controlFlowGraph && controlFlowGraph == c.controlFlowGraph; }
};

// This returns a string representation of the chain
std::string toString(const NodeChain &c,
                     std::list<CFGNode *>::const_iterator slicePos);
std::string toString(const NodeChain &c);

} // namespace propeller
} // namespace lld

namespace std {
// Specialization of std::less for NodeChain, which allows for consistent
// tie-breaking in our Map data structures.
template <> struct less<lld::propeller::NodeChain *> {
  bool operator()(const lld::propeller::NodeChain *c1,
                  const lld::propeller::NodeChain *c2) const {
    return c1->DelegateNode->mappedAddr < c2->DelegateNode->mappedAddr;
  }
};

// Specialization of std::less for pair<NodeChain,NodeChain>, which allows for
// consistent tie-breaking in our Map data structures.
template <>
struct less<pair<lld::propeller::NodeChain *, lld::propeller::NodeChain *>> {
  bool operator()(
      const pair<lld::propeller::NodeChain *, lld::propeller::NodeChain *> p1,
      const pair<lld::propeller::NodeChain *, lld::propeller::NodeChain *> p2)
      const {
    if (less<lld::propeller::NodeChain *>()(p1.first, p2.first))
      return true;
    if (less<lld::propeller::NodeChain *>()(p2.first, p1.first))
      return false;
    return less<lld::propeller::NodeChain *>()(p1.second, p2.second);
  }
};
} // namespace std

#endif
