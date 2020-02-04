//===-------------------- PropellerELFCfg.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file creates cfg and maps propeller profile onto cfg nodes / edges.
//
//===----------------------------------------------------------------------===//
#include "PropellerCFG.h"

#include "Propeller.h"

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <memory>
#include <ostream>
#include <stdio.h>
#include <string>
#include <vector>

using llvm::object::ObjectFile;
using llvm::object::RelocationRef;
using llvm::object::section_iterator;
using llvm::object::SectionRef;
using llvm::object::SymbolRef;

namespace lld {
namespace propeller {

bool CFGNode::isEntryNode() const {
  return controlFlowGraph->getEntryNode() == this;
}

bool ControlFlowGraph::writeAsDotGraph(StringRef cfgOutName) {
  std::error_code ec;
  llvm::raw_fd_ostream os(cfgOutName, ec, llvm::sys::fs::CD_CreateAlways);
  if (ec.value()) {
    warn("failed to open: '" + cfgOutName + "'");
    return false;
  }
  os << "digraph " << name.str() << "{\n";
  forEachNodeRef([&os](CFGNode &n) {
    os << n.getBBIndex() << " [size=\"" << n.shSize << "\"];";
  });
  os << "\n";
  for (auto &e : intraEdges) {
    bool IsFTEdge = (e->src->ftEdge == e.get());
    os << " " << e->src->getBBIndex() << " -> " << e->sink->getBBIndex()
       << " [label=\"" << e->weight
       << "\", weight=" << (IsFTEdge ? "1.0" : "0.1") << "];\n";
  }
  os << "}\n";
  llvm::outs() << "done dumping cfg '" << name.str() << "' into '"
               << cfgOutName.str() << "'\n";
  return true;
}

// Create an edge for "from->to".
CFGEdge *ControlFlowGraph::createEdge(CFGNode *from, CFGNode *to,
                                      typename CFGEdge::EdgeType type) {
  CFGEdge *edge = nullptr;
  auto CheckExistingEdge = [from, to, type,
                            &edge](std::vector<CFGEdge *> &Edges) {
    for (auto *E : Edges) {
      if (E->src == from && E->sink == to && E->type == type) {
        edge = E;
        return true;
      }
    }
    return false;
  };
  if (true || !from->hotTag || !to->hotTag) {
    if (type < CFGEdge::EdgeType::INTER_FUNC_CALL &&
        CheckExistingEdge(from->outs))
      return edge;
    if (type >= CFGEdge::EdgeType::INTER_FUNC_CALL &&
        CheckExistingEdge(from->callOuts))
      return edge;
  }

  edge = new CFGEdge(from, to, type);
  if (type < CFGEdge::EdgeType::INTER_FUNC_CALL) {
    from->outs.push_back(edge);
    to->ins.push_back(edge);
  } else {
    from->callOuts.push_back(edge);
    to->callIns.push_back(edge);
  }
  // Take ownership of "edge", cfg is responsible for all edges.
  emplaceEdge(edge);
  return edge;
}

// Apply counter (cnt) to all edges between node from -> to. Both nodes are from
// the same cfg.
bool ControlFlowGraph::markPath(CFGNode *from, CFGNode *to, uint64_t cnt) {
  assert(from->controlFlowGraph == to->controlFlowGraph);
  if (from == to)
    return true;
  CFGNode *p = from;

  // Iterate over fallthrough edges between from and to, adding every edge in
  // between to a vector.
  SmallVector<CFGEdge *, 32> fallThroughEdges;
  while (p && p != to) {
    if (p->ftEdge) {
      fallThroughEdges.push_back(p->ftEdge);
      p = p->ftEdge->sink;
    } else
      p = nullptr;
  }
  if (!p) // Fallthroughs break between from and to.
    return false;

  for (auto *e : fallThroughEdges)
    e->weight += cnt;

  return true;
}

// Apply counter (cnt) to the edge from node from -> to. Both nodes are from the
// same cfg.
void ControlFlowGraph::mapBranch(CFGNode *from, CFGNode *to, uint64_t cnt,
                                 bool isCall, bool isReturn) {
  assert(from->controlFlowGraph == to->controlFlowGraph);

  for (auto &e : from->outs) {
    bool edgeTypeOk = true;
    if (!isCall && !isReturn)
      edgeTypeOk =
          e->type == CFGEdge::INTRA_FUNC || e->type == CFGEdge::INTRA_DYNA;
    else if (isCall)
      edgeTypeOk = e->type == CFGEdge::INTRA_RSC;
    if (isReturn)
      edgeTypeOk = e->type == CFGEdge::INTRA_RSR;
    if (edgeTypeOk && e->sink == to) {
      e->weight += cnt;
      return;
    }
  }

  CFGEdge::EdgeType type = CFGEdge::INTRA_DYNA;
  if (isCall)
    type = CFGEdge::INTRA_RSC;
  else if (isReturn)
    type = CFGEdge::INTRA_RSR;

  createEdge(from, to, type)->weight += cnt;
}

// Apply counter (cnt) for calls/returns/ that cross function boundaries.
void ControlFlowGraph::mapCallOut(CFGNode *from, CFGNode *to, uint64_t toAddr,
                                  uint64_t cnt, bool isCall, bool isReturn) {
  assert(from->controlFlowGraph == this);
  assert(from->controlFlowGraph != to->controlFlowGraph);
  CFGEdge::EdgeType edgeType = CFGEdge::INTER_FUNC_RETURN;
  if (isCall || (toAddr && to->controlFlowGraph->getEntryNode() == to &&
                 toAddr == to->mappedAddr))
    edgeType = CFGEdge::INTER_FUNC_CALL;
  if (isReturn)
    edgeType = CFGEdge::INTER_FUNC_RETURN;
  for (auto &e : from->callOuts)
    if (e->sink == to && e->type == edgeType) {
      e->weight += cnt;
      return;
    }
  createEdge(from, to, edgeType)->weight += cnt;
}

// Create a map of cfgName -> a set of bbs that belong to this cfg.
// This is done in 2 steps:
//
// Step 1 - scan all the symbols, for each function symbols, create an entry in
// "groups", below is what "groups" looks like:
//  groups: {
//    "foo": [],
//    "bar": [],
//  }
//
// Step 2 - scan all the symbols, for each bb symbol, find it's function's
// group, and insert the bb symbol into the group. For example, if we have bb
// symbols "a.bb.foo", "aa.bb.foo" and "a.bb.bar", after step 2, the groups
// structure looks like:
//   groups: {
//     "foo": ["a.bb.foo", "aa.bb.foo"],
//     "bar": ["a.bb.bar"],
//   }
std::map<StringRef, std::list<SymbolRef>> CFGBuilder::buildPreCFGGroups() {
  std::map<StringRef, std::list<SymbolRef>> groups;
  auto symbols = view->viewFile->symbols();
  for (const SymbolRef &sym : symbols) {
    auto r = sym.getType();
    auto s = sym.getName();
    if (r && s && *r == SymbolRef::ST_Function) {
      StringRef symName = *s;
      auto ri = groups.emplace(std::piecewise_construct,
                               std::forward_as_tuple(symName),
                               std::forward_as_tuple(1, sym));
      (void)(ri.second);
      assert(ri.second);
    }
  }

  // Now we have a map of function names, group "x.bb.funcname".
  for (const SymbolRef &sym : symbols) {
    // All bb symbols are local, upon seeing the first global, exit.
    if ((sym.getFlags() & SymbolRef::SF_Global) != 0)
      break;
    auto nameOrErr = sym.getName();
    if (!nameOrErr)
      continue;
    StringRef sName = *nameOrErr;
    StringRef fName;
    if (SymbolEntry::isBBSymbol(sName, &fName, nullptr)) {
      auto L = groups.find(fName);
      if (L != groups.end())
        L->second.push_back(sym);
    }
  }
  return groups;
}

// Build map: TextSection -> It's Relocation Section.
// ELF file only contains link from Relocation Section -> It's text section.
std::map<uint64_t, section_iterator> CFGBuilder::buildRelocationSectionMap() {
  std::map<uint64_t, section_iterator> relocationSectionMap;
  for (section_iterator i = view->viewFile->section_begin(),
                        J = view->viewFile->section_end();
       i != J; ++i) {
    SectionRef secRef = *i;
    if (llvm::object::ELFSectionRef(secRef).getType() == llvm::ELF::SHT_RELA) {
      Expected<section_iterator> rr = secRef.getRelocatedSection();
      if (rr) {
        section_iterator &r = *rr;
        assert(r != J);
        relocationSectionMap.emplace(r->getIndex(), *i);
      }
    }
  }
  return relocationSectionMap;
}

// Helper method, process an entry of cfg.
// In selective bb sections, different cold bb lables of a function are grouped
// into one same cold section. Like below:
//
//    section .txt.func:        bb1 (ordinal=100)
//    section .txt.func:        bb2 (ordinal=101)
//    section .txt.func.cold:   bb3 (ordinal=102)
//                              bb4 (ordinal=103)
//                              bb5 (ordinal=104)
//
// Similarly all landing pads labels of a function are grouped into one single
// landing pad section.
//
// Each function has a unique cold section (and landing pad section).
//
// After processing, ordinalRemapping contains:
//    102 -> 102
//    103 -> 102
//    104 -> 102
//
// And tmpNodeMap contains:
//    100 -> CfgNode(mappedAddr=100)
//    101 -> CfgNode(mappedAddr=101)
//    102 -> CfgNode(mappedAddr=102)
//
// Parameters:
//   groupEntries: <cfg name, symbols belong to this cfg>
//   tmpNodeMap: {ordinal -> tmpCfgNode}
//   ordinalRemapping: {old ordinal -> new ordinal}
std::unique_ptr<ControlFlowGraph> CFGBuilder::buildCFGNodes(
    std::map<StringRef, std::list<SymbolRef>>::value_type &groupEntries,
    std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap,
    std::map<uint64_t, uint64_t> &ordinalRemapping) {
  assert(groupEntries.second.size() >= 1);
  // {Section Idx -> <CfgNode, <code/landing-pad labesl>>} mapping.
  std::map<uint32_t,
           std::pair<CFGNode *,
                     std::set<SymbolEntry *, SymbolEntryOrdinalLessComparator>>>
      bbGroupSectionMap;
  StringRef cfgName = groupEntries.first;
  std::unique_ptr<ControlFlowGraph> cfg(new ControlFlowGraph(view, cfgName, 0));

  for (SymbolRef sym : groupEntries.second) {
    auto symNameE = sym.getName();
    auto sectionIE = sym.getSection();
    if (!symNameE || !sectionIE ||
        (*sectionIE) == sym.getObject()->section_end()) {
      tmpNodeMap.clear();
      break;
    }

    StringRef symName = *symNameE;
    uint64_t symShndx = (*sectionIE)->getIndex();
    uint64_t symSectionSize = (*sectionIE)->getSize();
    // symValue is the offset to the beginning of its section.
    uint64_t symValue = sym.getValue();
    // uint64_t symSize = llvm::object::ELFSymbolRef(sym).getSize();
    SymbolEntry *symEnt = prop->propf->findSymbol(symName);
    // symValue is the offset of the bb symbol within a bbsection, if
    // symValue is nonzero, it means the symbol is not on its own
    // section, safe to ignore mapping with a propeller symbol. This
    // is a symbol grouped together w/ other bb symbols in the same
    // section (the cold section or the landing pad section), and this
    // bb symbol is not the representative symbol of the bb section.
    if (!symEnt) {
      if (symValue != 0)
        continue;
      tmpNodeMap.clear();
      break;
    }

    if (tmpNodeMap.find(symEnt->ordinal) != tmpNodeMap.end()) {
      tmpNodeMap.clear();
      error("Internal error checking cfg map.");
      break;
    }

    auto secI = bbGroupSectionMap.find(symShndx);
    if (secI != bbGroupSectionMap.end()) {
      CFGNode *secNode = secI->second.first;
      // All group nodes share the same section, so the shSize field must
      // equal.
      if (secNode->shSize != symSectionSize) {
        tmpNodeMap.clear();
        error("Check internal size error.");
        break;
      }
      // The first node within the section is the representative node.
      if (secNode->mappedAddr > symEnt->ordinal) {
        secNode->mappedAddr = symEnt->ordinal;
        secNode->shName = symName;
        secNode->shSize = symSectionSize;
      }
      // Insert the symbol into the set of symbols, those symbols all belong
      // to the same section.
      if (!secI->second.second.insert(symEnt).second) {
        tmpNodeMap.clear();
        error("Internal error grouping sections.");
        break;
      }
      continue; // to next sym.
    }
    // Otherwise, proceed to create a CFGNode.

    // Drop bb sections with no code
    if (!symSectionSize)
      continue;
    CFGNode *newNode = new CFGNode(symShndx, symName, symSectionSize,
                                   symEnt->ordinal, cfg.get(), symEnt->hotTag);
    tmpNodeMap.emplace(symEnt->ordinal, newNode);

    auto &pair = bbGroupSectionMap[symShndx];
    pair.first = newNode;
    if (!pair.second.insert(symEnt).second) {
      error("Internal error grouping duplicated sections.");
      tmpNodeMap.clear();
      break;
    }
  } // end of iterating of all symbols in a cfg group.

  if (tmpNodeMap.empty()) {
    cfg.reset(nullptr);
    return cfg;
  }

  if (cfg->debugCFG) {
    std::lock_guard<std::mutex> lockGuard(prop->lock);
    fprintf(stderr, "controlFlowGraph node group: %s\n",
            cfg->name.str().c_str());
    for (auto &pair : bbGroupSectionMap) {
      CFGNode *node = pair.second.first;
      auto &symSet = pair.second.second;
      if (symSet.size() > 1) {
        fprintf(stderr, "\t%s, shndx=%lu:", node->shName.str().c_str(),
                node->shndx);
        for (SymbolEntry *SS : symSet)
          fprintf(stderr, " %s[ordinal=%lu]", SS->name.str().c_str(),
                  SS->ordinal);
        fprintf(stderr, "\n");
      }
    }
  }

  for (auto &groupEntry : bbGroupSectionMap) {
    CFGNode *node = groupEntry.second.first;
    auto &symSet = groupEntry.second.second;
    if (symSet.size() <= 1)
      continue;
    SymbolEntry *firstSymbol = *(symSet.begin());
    if (firstSymbol->ordinal != node->mappedAddr) {
      error("Internal error grouping sections.");
      cfg.reset(nullptr);
      return cfg;
    }
    for (SymbolEntry *symEnt : symSet) {
      if (!ordinalRemapping.emplace(symEnt->ordinal, node->mappedAddr).second
          /* The representative node must have the smallest mappedAddr
             (ordinal) */
          || symEnt->ordinal < node->mappedAddr) {
        error("Internal error remapping duplicated sections.");
        cfg.reset(nullptr);
        return cfg;
      }
    }
  }
  return cfg;
}

// This function creates cfgs for a single object file. It firstly calls
// CFGBuilder::buildPreCFGGroups to create a map of cfgname to cfgnode group.
//
// Then for each cfg group, it calls CFGBuilder::buildCFGNodes to create cfg and
// tmpNodeMap, the latter is a map of ordinal -> CFGNode instance.
// One example of controlFlowGraph and tmpNodeMap is:
//   cfg[name=foo], tmpNodeMap={1: CFGNode[BBIndex="1"], 2:CFGNode[BBIndex="2"]}
//   cfg[name=bar], tmpNodeMap={3: CFGNode[BBIndex="3"]}
//
// Finally, for each cfg and tmpNodeMap, this calls CFGBuilder::buildCFG().
bool CFGBuilder::buildCFGs(std::map<uint64_t, uint64_t> &ordinalRemapping) {
  std::map<StringRef, std::list<SymbolRef>> groups{buildPreCFGGroups()};
  std::map<uint64_t, section_iterator> relocationSectionMap{
      buildRelocationSectionMap()};

  // "groups" built in the above step are like:
  //   {
  //     { "func1", {a.bb.func1, aa.bb.func1, aaa.bb.func1}
  //     { "func2", {a.bb.func2, aa.bb.func2, aaa.bb.func2}
  //       ...
  //       ...
  //   }
  for (auto &i : groups) {
    std::map<uint64_t, std::unique_ptr<CFGNode>> tmpNodeMap;
    std::unique_ptr<ControlFlowGraph> cfg{
        buildCFGNodes(i, tmpNodeMap, ordinalRemapping)};

    if (cfg) {
      SymbolRef cfgSym = *(i.second.begin());
      buildCFG(*cfg, cfgSym, tmpNodeMap, relocationSectionMap);
      view->cfgs.emplace(cfg->name, std::move(cfg));
    }
  } // Enf of processing all groups.
  return true;
}

// Build map: basicblock section index -> basicblock section node.
void CFGBuilder::buildShndxNodeMap(
    std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap,
    std::map<uint64_t, CFGNode *> &shndxNodeMap) {
  for (auto &nodeL : tmpNodeMap) {
    auto &node = nodeL.second;
    auto insertResult = shndxNodeMap.emplace(node->shndx, node.get());
    (void)(insertResult);
    assert(insertResult.second);
  }
}

// Build controlFlowGraph for a single function.
//
// For each bb sections of a single function, we iterate all the relocation
// entries, and for relocations that targets another bb in the same function,
// we create an edge between these 2 BBs.
void CFGBuilder::buildCFG(
    ControlFlowGraph &cfg, const SymbolRef &cfgSym,
    std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap,
    std::map<uint64_t, section_iterator> &relocationSectionMap) {
  std::map<uint64_t, CFGNode *> shndxNodeMap;
  buildShndxNodeMap(tmpNodeMap, shndxNodeMap);

  // Recursive call edges.
  // std::list<CFGEdge *> rscEdges;
  // Iterate all bb symbols.
  for (auto &nPair : tmpNodeMap) {
    CFGNode *srcNode = nPair.second.get();
    // For each bb section, we find its rela sections.
    auto relaSecRefI = relocationSectionMap.find(srcNode->shndx);
    if (relaSecRefI == relocationSectionMap.end())
      continue;

    // Iterate all rela entries.
    for (const RelocationRef &rela : relaSecRefI->second->relocations()) {
      SymbolRef rSym = *(rela.getSymbol());
      bool isRSC = (cfgSym == rSym);

      // All bb section symbols are local symbols.
      if (!isRSC &&
          ((rSym.getFlags() & llvm::object::BasicSymbolRef::SF_Global) != 0))
        continue;

      auto sectionIE = rSym.getSection();
      if (!sectionIE)
        continue;
      // Now we have the shndx of one relocation target.
      uint64_t symShndx((*sectionIE)->getIndex());
      CFGNode *targetNode{nullptr};
      // Check to see if shndx is another bb section within the same function.
      auto result = shndxNodeMap.find(symShndx);
      if (result != shndxNodeMap.end()) {
        targetNode = result->second;
        if (targetNode) {
          // If so, we create the edge.
          // CFGEdge *e =
          cfg.createEdge(srcNode, targetNode,
                         isRSC ? CFGEdge::INTRA_RSC : CFGEdge::INTRA_FUNC);
          // If it's a recursive call, record it.
          // if (isRSC)
          //  rscEdges.push_back(e);
        }
      }
    }
  }

  /*
  // For each recursive call, we create a recursive-self-return edges for all
  // exit edges. In the following example, create an edge bb5->bb3 FuncA:
  //    bb1:            <---+
  //        ...             |
  //    bb2:                |
  //        ...             |   r(ecursie)-s(elf)-C(all) edge
  //    bb3:                |
  //        ...             |
  //        call FuncA  --- +
  //        xxx yyy     <---+
  //        ...             |
  //    bb4:                |
  //        ...             |   r(ecursie)-s(elf)-r(eturn) edge
  //    bb5:                |
  //        ...             |
  //        ret   ----------+
  for (auto *rEdge : rscEdges) {
    for (auto &nPair : tmpNodeMap) {
      auto &n = nPair.second;
      if (n->outs.size() == 0 ||
          (n->outs.size() == 1 &&
           (*n->outs.begin())->type == CFGEdge::INTRA_RSC)) {
        // Now "n" is the exit node.
        cfg.createEdge(n.get(), rEdge->src, CFGEdge::INTRA_RSR);
      }
    }
  }
  */
  calculateFallthroughEdges(cfg, tmpNodeMap);

  // Transfer nodes ownership to cfg and destroy tmpNodeMap.
  for (auto &pair : tmpNodeMap) {
    cfg.nodes.emplace_back(std::move(pair.second));
    pair.second.reset(nullptr);
  }
  tmpNodeMap.clear();

  // Calculate the cfg size
  cfg.size = 0;
  cfg.forEachNodeRef([&cfg](CFGNode &n) { cfg.size += n.shSize; });
}

// Calculate fallthroughs. Edge p->q is fallthrough if p & q are adjacent (e.g.
// no other bbs are between p & q), and there is a NORMAL edge from p->Q.
//
// tmpNodeMap groups nodes according to their beginning address:
//      addr1: [Node1]
//      addr2: [Node2]
//      addr3: [Node3]
//      addr4: [Node4]
//    And addr1 <= addr2 <= addr3 <= addr4.
void CFGBuilder::calculateFallthroughEdges(
    ControlFlowGraph &cfg,
    std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap) {

  auto setupFallthrough = [&cfg](CFGNode *n1, CFGNode *n2) {
    for (auto *e : n1->outs)
      if (e->type == CFGEdge::INTRA_FUNC && e->sink == n2) {
        n1->ftEdge = e;
        return;
      }
    if (n1->shSize == 0)
      // An empty section always fallthrough to the next adjacent section.
      n1->ftEdge = cfg.createEdge(n1, n2, CFGEdge::INTRA_FUNC);
  };

  for (auto p = tmpNodeMap.begin(), q = std::next(p), e = tmpNodeMap.end();
       q != e; ++p, ++q)
    setupFallthrough(p->second.get(), q->second.get());
}

std::ostream &operator<<(std::ostream &out, const CFGNode &node) {
  out << "["
      << (node.shName == node.controlFlowGraph->name
              ? "Entry"
              : std::to_string(node.shName.size() -
                               node.controlFlowGraph->name.size() - 4))
      << "]"
      << " [size=" << std::noshowbase << std::dec << node.shSize << ", "
      << " addr=" << std::showbase << std::hex << node.mappedAddr << ", "
      << " frequency=" << std::showbase << std::dec << node.freq << ", "
      << " shndx=" << std::noshowbase << std::dec << node.shndx << "]";
  return out;
}

std::ostream &operator<<(std::ostream &out, const CFGEdge &edge) {
  static const char *TypeStr[] = {"", " (*RSC*)", " (*RSR*)", " (*DYNA*)"};
  out << "edge: " << *edge.src << " -> " << *edge.sink << " [" << std::setw(12)
      << std::setfill('0') << std::noshowbase << std::dec << edge.weight << "]"
      << TypeStr[edge.type];
  return out;
}

std::ostream &operator<<(std::ostream &out, const ControlFlowGraph &cfg) {
  out << "cfg: '" << cfg.view->viewName.str() << ":" << cfg.name.str()
      << "', size=" << std::noshowbase << std::dec << cfg.size << std::endl;
  for (auto &n : cfg.nodes) {
    auto &node = *n;
    out << "  node: " << node << std::endl;
    for (auto &edge : node.outs) {
      out << "    " << *edge << (edge == node.ftEdge ? " (*FT*)" : "")
          << std::endl;
    }
    for (auto &edge : node.callOuts) {
      out << "    Calls: '" << edge->sink->shName.str()
          << "': " << std::noshowbase << std::dec << edge->weight << std::endl;
    }
  }
  out << std::endl;
  return out;
}

bool CFGEdge::isFTEdge() const { return src->ftEdge == this; }

} // namespace propeller
} // namespace lld
