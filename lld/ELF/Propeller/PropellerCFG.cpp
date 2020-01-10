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
  return CFG->getEntryNode() == this;
}

bool ControlFlowGraph::writeAsDotGraph(StringRef cfgOutName) {
  std::error_code ec;
  llvm::raw_fd_ostream os(cfgOutName, ec, llvm::sys::fs::CD_CreateAlways);
  if (ec.value()) {
    warn("failed to open: '" + cfgOutName + "'");
    return false;
  }
  os << "digraph " << Name.str() << "{\n";
  forEachNodeRef([&os](CFGNode &n) {
    os << n.getBBIndex() << " [size=\"" << n.ShSize << "\"];";
  });
  os << "\n";
  for (auto &e : IntraEdges) {
    bool IsFTEdge = (e->Src->FTEdge == e.get());
    os << " " << e->Src->getBBIndex() << " -> " << e->Sink->getBBIndex()
       << " [label=\"" << e->Weight
       << "\", weight=" << (IsFTEdge ? "1.0" : "0.1") << "];\n";
  }
  os << "}\n";
  llvm::outs() << "done dumping cfg '" << Name.str() << "' into '"
               << cfgOutName.str() << "'\n";
  return true;
}

// Create an edge for "from->to".
CFGEdge *ControlFlowGraph::createEdge(CFGNode *from, CFGNode *to,
                                      typename CFGEdge::EdgeType type) {
  CFGEdge *edge = nullptr;
  auto CheckExistingEdge = [from, to,
                            type, &edge](std::vector<CFGEdge *> &Edges) {
    for (auto *E : Edges) {
      if (E->Src == from && E->Sink == to && E->Type == type) {
        edge = E;
        return true;
      }
    }
    return false;
  };
  if (!from->HotTag || !to->HotTag) {
    if (type < CFGEdge::EdgeType::INTER_FUNC_CALL &&
        CheckExistingEdge(from->Outs))
      return edge;
    if (type >= CFGEdge::EdgeType::INTER_FUNC_CALL &&
        CheckExistingEdge(from->CallOuts))
      return edge;
  }

  edge = new CFGEdge(from, to, type);
  if (type < CFGEdge::EdgeType::INTER_FUNC_CALL) {
    from->Outs.push_back(edge);
    to->Ins.push_back(edge);
  } else {
    from->CallOuts.push_back(edge);
    to->CallIns.push_back(edge);
  }
  // Take ownership of "edge", cfg is responsible for all edges.
  emplaceEdge(edge);
  return edge;
}

// Apply counter (cnt) to all edges between node from -> to. Both nodes are from
// the same cfg.
bool ControlFlowGraph::markPath(CFGNode *from, CFGNode *to, uint64_t cnt) {
  if (from == nullptr) {
    /* If the from node is null, walk backward from the to node while only
     * one INTRA_FUNC incoming edge is found. */
    assert(to != nullptr);
    CFGNode *p = to;
    do {
      if (p->Ins.size() == 1 && p->Ins.front()->isFTEdge()){
        p->Ins.front()->Weight += cnt;
        p = p->Ins.front()->Src;
      } else
        p = nullptr;
    } while (p && p != to);
    return true;
  }

  if (to == nullptr) {
    /* If the to node is null, walk forward from the from node while only
     * one INTRA_FUNC outgoing edge is found. */
    assert(from != nullptr);
    CFGNode *p = from;
    do {
      if (p->Outs.size() == 1 && p->Outs.front()->isFTEdge()) {
        p->Outs.front()->Weight += cnt;
        p = p->Outs.front()->Sink;
      } else
        p = nullptr;
    } while (p && p != from);
    return true;
  }

  assert(from->CFG == to->CFG);
  if (from == to)
    return true;
  CFGNode *p = from;
  while (p && p != to) {
    if (p->FTEdge) {
      p->FTEdge->Weight += cnt;
      p = p->FTEdge->Sink;
    } else
      p = nullptr;
  }
  if (!p)
    return false;
  return true;
}

// Apply counter (cnt) to the edge from node from -> to. Both nodes are from the
// same cfg.
void ControlFlowGraph::mapBranch(CFGNode *from, CFGNode *to, uint64_t cnt,
                                 bool isCall, bool isReturn) {
  assert(from->CFG == to->CFG);

  for (auto &e : from->Outs) {
    bool edgeTypeOk = true;
    if (!isCall && !isReturn)
      edgeTypeOk =
          e->Type == CFGEdge::INTRA_FUNC || e->Type == CFGEdge::INTRA_DYNA;
    else if (isCall)
      edgeTypeOk = e->Type == CFGEdge::INTRA_RSC;
    if (isReturn)
      edgeTypeOk = e->Type == CFGEdge::INTRA_RSR;
    if (edgeTypeOk && e->Sink == to) {
      e->Weight += cnt;
      return;
    }
  }

  CFGEdge::EdgeType type = CFGEdge::INTRA_DYNA;
  if (isCall)
    type = CFGEdge::INTRA_RSC;
  else if (isReturn)
    type = CFGEdge::INTRA_RSR;

  createEdge(from, to, type)->Weight += cnt;
}

// Apply counter (cnt) for calls/returns/ that cross function boundaries.
void ControlFlowGraph::mapCallOut(CFGNode *from, CFGNode *to, uint64_t toAddr,
                                  uint64_t cnt, bool isCall, bool isReturn) {
  assert(from->CFG == this);
  assert(from->CFG != to->CFG);
  CFGEdge::EdgeType edgeType = CFGEdge::INTER_FUNC_RETURN;
  if (isCall ||
      (toAddr && to->CFG->getEntryNode() == to && toAddr == to->MappedAddr))
    edgeType = CFGEdge::INTER_FUNC_CALL;
  if (isReturn)
    edgeType = CFGEdge::INTER_FUNC_RETURN;
  for (auto &e : from->CallOuts)
    if (e->Sink == to && e->Type == edgeType) {
      e->Weight += cnt;
      return;
    }
  createEdge(from, to, edgeType)->Weight += cnt;
}

std::map<StringRef, std::list<SymbolRef>> CFGBuilder::buildPreCFGGroups() {
  std::map<StringRef, std::list<SymbolRef>> groups;
  auto symbols = View->ViewFile->symbols();
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
  for (section_iterator i = View->ViewFile->section_begin(),
                        J = View->ViewFile->section_end();
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

// Helper method, process an entry of group.
// In selective bb sections, different cold bb lables are grouped into one same
// cold section. Like below:
//
//    section .txt.func:        bb1 (ordinal=100)
//    section .txt.func:        bb2 (ordinal=101)
//    section .txt.func.cold:   bb3 (ordinal=102)
//                              bb4 (ordinal=103)
//                              bb5 (ordinal=104)
//
// After processing, OrdinalRemapping contains:
//    102 -> 102
//    103 -> 102
//    104 -> 102
//
// And tmpNodeMap contains:
//    100 -> CfgNode(MappedAddr=100) 
//    101 -> CfgNode(MappedAddr=101) 
//    102 -> CfgNode(MappedAddr=102) 
//
std::unique_ptr<ControlFlowGraph> CFGBuilder::buildCFGNodes(
    std::map<StringRef, std::list<SymbolRef>>::value_type &GE,
    std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap,
    std::map<uint64_t, uint64_t> &OrdinalRemapping) {
  assert(GE.second.size() >= 1);
  std::map<uint32_t,
           std::pair<CFGNode *,
                     std::set<SymbolEntry *, SymbolEntryOrdinalLessComparator>>>
      bbGroupSectionMap;
  StringRef cfgName = GE.first;
  std::unique_ptr<ControlFlowGraph> cfg(new ControlFlowGraph(View, cfgName, 0));

  for (SymbolRef sym : GE.second) {
    auto symNameE = sym.getName();
    auto sectionIE = sym.getSection();
    if (!symNameE && !sectionIE &&
        (*sectionIE) == sym.getObject()->section_end()) {
      tmpNodeMap.clear();
      break;
    }

    StringRef symName = *symNameE;
    uint64_t symShndx = (*sectionIE)->getIndex();
    uint64_t symSectionSize = (*sectionIE)->getSize();
    uint64_t symValue = sym.getValue();
    // Note here: BB symbols only carry size information when
    // -fbasicblock-section=all. Objects built with
    // -fbasicblock-section=labels do not have size information
    // for BB symbols.
    // uint64_t symSize = llvm::object::ELFSymbolRef(sym).getSize();
    SymbolEntry *sE = prop->Propf->findSymbol(symName);
    // symValue is the offset of the bb symbol within a bbsection, if
    // symValue is nonzero, it means this is a symbol grouped together w/ other
    // bb symbols in the same section (the code section or the landing pad
    // section), and this bb symbol is not the representative symbol of the bb
    // section, we can safely ignore the symbol.
    if (!sE) {
      if (symValue != 0) continue;
      tmpNodeMap.clear();
      break;
    }

    if (tmpNodeMap.find(sE->Ordinal) != tmpNodeMap.end()) {
      tmpNodeMap.clear();
      error("Internal error checking cfg map.");
      break;
    }
    // All cold BBs go into a single cold section. All landing pads go
    // into a single landing pad section.
    bool needGroup =
        !sE->HotTag || symName.front() == 'l' || symName.front() == 'L';
    if (needGroup) {
      auto groupI = bbGroupSectionMap.find(symShndx);
      if (groupI != bbGroupSectionMap.end()) {
        CFGNode *groupNode = groupI->second.first;
        // All group nodes share the same section, so the ShSize field must
        // equal.
        if (groupNode->ShSize != symSectionSize) {
          tmpNodeMap.clear();
          error("Check internal size error.");
          break;
        }
        // The first node within the section is the representative node.
        if (groupNode->MappedAddr > sE->Ordinal) {
          groupNode->MappedAddr = sE->Ordinal;
          groupNode->ShName = symName;
          groupNode->ShSize = symSectionSize;
        }
        if (!groupI->second.second.insert(sE).second) {
          tmpNodeMap.clear();
          error("Internal error grouping sections.");
          break;
        }
        continue; // to next sym.
      }
    }

    // Drop bb sections with no code
    if (!symSectionSize)
      continue;
    CFGNode *node = new CFGNode(symShndx, symName, symSectionSize, sE->Ordinal,
                                cfg.get(), sE->HotTag);
    tmpNodeMap.emplace(sE->Ordinal, node);
    if (needGroup) {
      auto &P = bbGroupSectionMap[symShndx];
      P.first = node;
      if (!P.second.insert(sE).second) {
        error("Internal error grouping duplicated sections.");
        tmpNodeMap.clear();
        break;
      }
    }
  }

  if (tmpNodeMap.empty()) {
    cfg.reset(nullptr);
    return cfg;
  }

  for (auto &P : bbGroupSectionMap) {
    auto *Node = P.second.first;
    auto &SymSet = P.second.second;
    if (SymSet.size() > 1) {
      SymbolEntry *FirstSymbol = *(SymSet.begin());
      if (FirstSymbol->Ordinal != Node->MappedAddr) {
        error("Internal error grouping sections.");
        cfg.reset(nullptr);
        return cfg;
      }
      for (SymbolEntry *SS : SymSet) {
        if (!OrdinalRemapping.emplace(SS->Ordinal, Node->MappedAddr).second
            /* The representative Node must have the smallest MappedAddr
               (Ordinal) */
            || SS->Ordinal < Node->MappedAddr) {
          error("Internal error remapping duplicated sections.");
          cfg.reset(nullptr);
          return cfg;
        }
      }
    }
  }
  return cfg;
}

// This function creates CFGs for a single object file.
//
// Step 1 - scan all the symbols, for each function symbols, create an entry in
// "groups", below is what "groups" looks like:
//  groups: {
//    "foo": [],
//    "bar": [],
//  }
//
// Step 2 - scan all the symbols, for each BB symbol, find it's function's
// group, and insert the bb symbol into the group. For example, if we have BB
// symbols "a.BB.foo", "aa.BB.foo" and "a.BB.bar", after step 2, the groups
// structure looks like:
//   groups: {
//     "foo": ["a.BB.foo", "aa.BB.foo"],
//     "bar": ["a.BB.bar"],
//   }
//
// Step 3 - for each group, create CFG and tmpNodeMap, the latter is an ordered
// map of CFGNode (index key is Symbol Ordinal). For the above example, the
// following data structure is created:
//   CFG[Name=foo], tmpNodeMap={1: CFGNode[BBIndex="1"], 2:CFGNode[BBIndex="2"]}
//   CFG[Name=bar], tmpNodeMap={3: CFGNode[BBIndex="3"]}
//
// For each CFG and tmpNodeMap, call CFGBuilder::buildCFG().
bool CFGBuilder::buildCFGs(std::map<uint64_t, uint64_t> &OrdinalRemapping) {
  std::map<StringRef, std::list<SymbolRef>> groups{buildPreCFGGroups()};
  std::map<uint64_t, section_iterator> relocationSectionMap{
      buildRelocationSectionMap()};

  // Groups built in the above step are like:
  //   {
  //     { "func1", {a.BB.func1, aa.BB.func1, aaa.BB.func1}
  //     { "func2", {a.BB.func2, aa.BB.func2, aaa.BB.func2}
  //       ...
  //       ...
  //   }
  for (auto &i : groups) {
    std::map<uint64_t, std::unique_ptr<CFGNode>> tmpNodeMap;
    std::unique_ptr<ControlFlowGraph> cfg{
        buildCFGNodes(i, tmpNodeMap, OrdinalRemapping)};

    if (cfg) {
      SymbolRef cfgSym = *(i.second.begin());
      buildCFG(*cfg, cfgSym, tmpNodeMap, relocationSectionMap);
      View->CFGs.emplace(cfg->Name, std::move(cfg));
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
    auto insertResult = shndxNodeMap.emplace(node->Shndx, node.get());
    (void)(insertResult);
    assert(insertResult.second);
  }
}

// Build CFG for a single function.
//
// For each BB sections of a single function, we iterate all the relocation
// entries, and for relocations that targets another BB in the same function,
// we create an edge between these 2 BBs.
void CFGBuilder::buildCFG(
    ControlFlowGraph &cfg, const SymbolRef &cfgSym,
    std::map<uint64_t, std::unique_ptr<CFGNode>> &tmpNodeMap,
    std::map<uint64_t, section_iterator> &relocationSectionMap) {
  std::map<uint64_t, CFGNode *> shndxNodeMap;
  buildShndxNodeMap(tmpNodeMap, shndxNodeMap);

  // Recursive call edges.
  //std::list<CFGEdge *> rscEdges;
  // Iterate all bb symbols.
  for (auto &nPair : tmpNodeMap) {
    CFGNode *srcNode = nPair.second.get();
    // For each bb section, we find its rela sections.
    auto relaSecRefI = relocationSectionMap.find(srcNode->Shndx);
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
      // Now we have the Shndx of one relocation target.
      uint64_t symShndx((*sectionIE)->getIndex());
      CFGNode *targetNode{nullptr};
      // Check to see if Shndx is another BB section within the same function.
      auto result = shndxNodeMap.find(symShndx);
      if (result != shndxNodeMap.end()) {
        targetNode = result->second;
        if (targetNode) {
          // If so, we create the edge.
          // CFGEdge *e =
          cfg.createEdge(srcNode, targetNode,
                            isRSC ? CFGEdge::INTRA_RSC : CFGEdge::INTRA_FUNC);
          // If it's a recursive call, record it.
          //if (isRSC)
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
      if (n->Outs.size() == 0 ||
          (n->Outs.size() == 1 &&
           (*n->Outs.begin())->Type == CFGEdge::INTRA_RSC)) {
        // Now "n" is the exit node.
        cfg.createEdge(n.get(), rEdge->Src, CFGEdge::INTRA_RSR);
      }
    }
  }
  */
  calculateFallthroughEdges(cfg, tmpNodeMap);

  // Transfer nodes ownership to cfg and destroy tmpNodeMap.
  for (auto &pair : tmpNodeMap) {
    cfg.Nodes.emplace_back(std::move(pair.second));
    pair.second.reset(nullptr);
  }
  tmpNodeMap.clear();

  // Calculate the cfg size
  cfg.Size = 0;
  cfg.forEachNodeRef([&cfg](CFGNode &n) {
    cfg.Size += n.ShSize;
  });
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
    for (auto *e : n1->Outs)
      if (e->Type == CFGEdge::INTRA_FUNC && e->Sink == n2) {
        n1->FTEdge = e;
        return;
      }
    if (n1->ShSize == 0)
      // An empty section always fallthrough to the next adjacent section.
      n1->FTEdge = cfg.createEdge(n1, n2, CFGEdge::INTRA_FUNC);
  };

  for (auto p = tmpNodeMap.begin(), q = std::next(p), e = tmpNodeMap.end();
       q != e; ++p, ++q)
    setupFallthrough(p->second.get(), q->second.get());
}

std::ostream &operator<<(std::ostream &out, const CFGNode &node) {
  out << "["
      << (node.ShName == node.CFG->Name
              ? "Entry"
              : std::to_string(node.ShName.size() - node.CFG->Name.size() - 4))
      << "]"
      << " [size=" << std::noshowbase << std::dec << node.ShSize << ", "
      << " addr=" << std::showbase << std::hex << node.MappedAddr << ", "
      << " frequency=" << std::showbase << std::dec << node.Freq << ", "
      << " shndx=" << std::noshowbase << std::dec << node.Shndx << "]";
  return out;
}

std::ostream &operator<<(std::ostream &out, const CFGEdge &edge) {
  static const char *TypeStr[] = {"", " (*RSC*)", " (*RSR*)", " (*DYNA*)"};
  out << "edge: " << *edge.Src << " -> " << *edge.Sink << " [" << std::setw(12)
      << std::setfill('0') << std::noshowbase << std::dec << edge.Weight << "]"
      << TypeStr[edge.Type];
  return out;
}

std::ostream &operator<<(std::ostream &out, const ControlFlowGraph &cfg) {
  out << "cfg: '" << cfg.View->ViewName.str() << ":" << cfg.Name.str()
      << "', size=" << std::noshowbase << std::dec << cfg.Size << std::endl;
  for (auto &n : cfg.Nodes) {
    auto &node = *n;
    out << "  node: " << node << std::endl;
    for (auto &edge : node.Outs) {
      out << "    " << *edge << (edge == node.FTEdge ? " (*FT*)" : "")
          << std::endl;
    }
    for (auto &edge : node.CallOuts) {
      out << "    Calls: '" << edge->Sink->ShName.str()
          << "': " << std::noshowbase << std::dec << edge->Weight << std::endl;
    }
  }
  out << std::endl;
  return out;
}

bool CFGEdge::isFTEdge() const {
  return Src->FTEdge == this;
}

} // namespace propeller
} // namespace lld
