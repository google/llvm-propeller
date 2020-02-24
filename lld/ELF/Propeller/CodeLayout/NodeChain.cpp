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

NodeChain *getNodeChain(const CFGNode *n) { return n->bundle->chain; }

int64_t getNodeOffset(const CFGNode *n) {
  return n->bundle->chainOffset + n->bundleOffset;
}

std::string
toString(const NodeChain &c,
         std::list<std::unique_ptr<CFGNodeBundle>>::const_iterator slicePos) {
  std::string str;
  if (c.controlFlowGraph)
    str += c.controlFlowGraph->name.str();
  str += " [ ";
  for (auto bundleIt = c.nodeBundles.begin(); bundleIt != c.nodeBundles.end();
       ++bundleIt) {
    if (bundleIt == slicePos)
      str += "\n....SLICE POSITION....\n";
    for (auto *n : (*bundleIt)->nodes) {
      str +=
          " << bundle [coffset= " + std::to_string((*bundleIt)->chainOffset) +
          "] ";
      if (!c.controlFlowGraph)
        str += std::to_string(n->controlFlowGraph->getEntryNode()->mappedAddr) +
               ":";
      str += n->controlFlowGraph->getEntryNode() == n
                 ? "Entry"
                 : std::to_string(n->shName.size() -
                                  n->controlFlowGraph->name.size() - 4);
      str += " (size=" + std::to_string(n->shSize) +
             ", freq=" + std::to_string(n->freq) +
             ", boffset=" + std::to_string(n->bundleOffset) + ")";
      if (n != (*bundleIt)->nodes.back())
        str += " -> ";
    }
    str += " >> ";
  }
  str += " ]";
  str += " score: " + std::to_string(c.score);
  return str;
}

std::string toString(const NodeChain &c) {
  return toString(c, c.nodeBundles.end());
}

} // namespace propeller
} // namespace lld
