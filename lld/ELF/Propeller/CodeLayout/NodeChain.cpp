//===- NodeChain.cpp  -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "NodeChain.h"

namespace lld {
namespace propeller {

std::string toString(const NodeChain &c,
                     std::list<CFGNode *>::const_iterator slicePos) {
  std::string str;
  if (c.controlFlowGraph)
    str += c.controlFlowGraph->name.str();
  str += " [ ";
  for (auto it = c.nodes.begin(); it != c.nodes.end(); ++it) {
    auto *n = *it;
    if (it == slicePos)
      str += "\n....SLICE POSITION....\n";
    if (!c.controlFlowGraph)
      str += std::to_string(n->controlFlowGraph->getEntryNode()->mappedAddr) + ":";
    str += n->controlFlowGraph->getEntryNode() == n
               ? "Entry"
               : std::to_string(n->shName.size() - n->controlFlowGraph->name.size() - 4);
    str += " (size=" + std::to_string(n->shSize) +
           ", freq=" + std::to_string(n->freq) + ")";
    if (n != c.nodes.back())
      str += " -> ";
  }
  str += " ]";
  str += " score: " + std::to_string(c.Score);
  return str;
}

std::string toString(const NodeChain &c) { return toString(c, c.nodes.end()); }

} // namespace propeller
} // namespace lld
