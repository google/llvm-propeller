//===- PropellerNodeChain.h  ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLD_ELF_PROPELLER_NODE_CHAIN_H
#define LLD_ELF_PROPELLER_NODE_CHAIN_H

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
  ControlFlowGraph *CFG;
  std::list<CFGNode *> Nodes;
  std::list<std::list<CFGNode *>::iterator> FunctionEntryIndices;
  std::unordered_map<NodeChain *, std::vector<CFGEdge *>> OutEdges;
  DenseSet<NodeChain *> InEdges;

  // Total binary size of the chain
  uint64_t Size;

  // Total execution frequency of the chain
  uint64_t Freq;

  // Extended TSP score of the chain
  double Score = 0;

  bool DebugChain;

  // Constructor for building a NodeChain from a single Node
  NodeChain(CFGNode *node)
      : DelegateNode(node), CFG(node->CFG), Nodes(1, node), Size(node->ShSize),
        Freq(node->Freq), DebugChain(node->CFG->DebugCFG) {}

  NodeChain(ControlFlowGraph *cfg)
      : DelegateNode(cfg->getEntryNode()), CFG(cfg), Size(cfg->Size), Freq(0),
        DebugChain(cfg->DebugCFG) {
    cfg->forEachNodeRef([this](CFGNode &node) {
      Nodes.push_back(&node);
      Freq += node.Freq;
    });
  }

  template <class Visitor>
  void forEachOutEdgeToChain(NodeChain *chain, Visitor V) {
    auto it = OutEdges.find(chain);
    if (it == OutEdges.end())
      return;
    for (CFGEdge *E : it->second)
      V(*E, this, chain);
  }

  double execDensity() const {
    return ((double)Freq) / std::max(Size, (uint64_t)1);
  }
};

std::string toString(const NodeChain &c);
} // namespace propeller
} // namespace lld

namespace std {

template <> struct less<lld::propeller::NodeChain *> {
  bool operator()(const lld::propeller::NodeChain *c1,
                  const lld::propeller::NodeChain *c2) const {
    return c1->DelegateNode->MappedAddr < c2->DelegateNode->MappedAddr;
  }
};

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
