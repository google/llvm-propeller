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

std::string toString(const NodeChain &c, std::list<CFGNode*>::const_iterator slicePos) {
  std::string str;
  if (c.CFG)
    str += c.CFG->Name.str();
  str += " [ ";
  for (auto it = c.Nodes.begin(); it != c.Nodes.end(); ++it) {
    auto *n = *it;
    if (it == slicePos)
      str += "\n....SLICE POSITION....\n";
    if (!c.CFG)
      str += std::to_string(n->CFG->getEntryNode()->MappedAddr) + ":";
    str += n->CFG->getEntryNode() == n
               ? "Entry"
               : std::to_string(n->ShName.size() - n->CFG->Name.size() - 4);
    str += " (size=" + std::to_string(n->ShSize) +
           ", freq=" + std::to_string(n->Freq) + ")";
    if (n != c.Nodes.back())
      str += " -> ";
  }
  str += " ]";
  str += " score: " + std::to_string(c.Score);
  return str;
}

std::string toString(const NodeChain &c){
  return toString(c, c.Nodes.end());
}


} // namespace propeller
} // namespace lld
