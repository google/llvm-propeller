//===- PropellerNodeChain.cpp  --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "PropellerNodeChain.h"

namespace lld {
namespace propeller {

std::string toString(const NodeChain &c) {
  std::string str;
  size_t cfgNameLength = c.DelegateNode->CFG->Name.size();
  str += c.DelegateNode->CFG->Name.str() + " [ ";
  for (auto *n : c.Nodes) {
    str += n->CFG->getEntryNode() == n
               ? "Entry"
               : std::to_string(n->ShName.size() - cfgNameLength - 4);
    str += " (size=" + std::to_string(n->ShSize) +
           ", freq=" + std::to_string(n->Freq) + ")";
    if (n != c.Nodes.back())
      str += " -> ";
  }
  str += " ]";
  str += " score: " + std::to_string(c.Score);
  return str;
}

} // namespace propeller
} // namespace lld
