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
#include "PropellerELFCfg.h"

#include "Propeller.h"
#include "Symbols.h"
#include "SymbolTable.h"

#include "llvm/Object/ObjectFile.h"
// Needed by ELFSectionRef & ELFSymbolRef.
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <stdio.h>
#include <string>
#include <vector>

using llvm::object::ObjectFile;
using llvm::object::RelocationRef;
using llvm::object::SectionRef;
using llvm::object::section_iterator;
using llvm::object::SymbolRef;

namespace lld {
namespace propeller {

bool ELFCFG::writeAsDotGraph(const char *cfgOutName) {
  FILE *fp = fopen(cfgOutName, "w");
  if (!fp) {
    warn("[Propeller]: Failed to open: '" + StringRef(cfgOutName) + "'\n");
    return false;
  }
  fprintf(fp, "digraph %s {\n", Name.str().c_str());
  forEachNodeRef([&fp](ELFCFGNode &n) {
    fprintf(fp, "%u [size=\"%lu\"];", n.getBBIndex(), n.ShSize);
  });
  fprintf(fp, "\n");
  for (auto &e : IntraEdges) {
    bool IsFTEdge = (e->Src->FTEdge == e.get());
    fprintf(fp, " %u -> %u [label=\"%lu\", weight=%f];\n", e->Src->getBBIndex(),
            e->Sink->getBBIndex(), e->Weight, IsFTEdge ? 1.0 : 0.1);
  }
  fprintf(fp, "}\n");
  fclose(fp);
  llvm::outs() << "[Propeller]: Done dumping cfg '" << Name.str() << "' into '"
               << cfgOutName << "'.\n";
  return true;
}

// Create an edge for "from->to".
ELFCFGEdge *ELFCFG::createEdge(ELFCFGNode *from, ELFCFGNode *to,
                               typename ELFCFGEdge::EdgeType type) {
  ELFCFGEdge *edge = new ELFCFGEdge(from, to, type);
  if (type < ELFCFGEdge::EdgeType::INTER_FUNC_CALL) {
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

// Apply counter (CNT) to all edges between node from -> to. Both nodes are from
// the same cfg.
bool ELFCFG::markPath(ELFCFGNode *from, ELFCFGNode *to, uint64_t cnt) {
  if (from == nullptr) {
    /* If the from node is null, walk backward from the to node while only
     * one INTRA_FUNC incoming edge is found. */
    assert(to != nullptr);
    ELFCFGNode *p = to;
    do {
      std::vector<ELFCFGEdge *> intraInEdges;
      std::copy_if(p->Ins.begin(), p->Ins.end(),
                   std::back_inserter(intraInEdges), [this](ELFCFGEdge *e) {
                     return e->Type == ELFCFGEdge::EdgeType::INTRA_FUNC &&
                            e->Sink != getEntryNode();
                   });
      if (intraInEdges.size() == 1)
        p = intraInEdges.front()->Src;
      else
        p = nullptr;
    } while (p && p != to);
    return true;
  }

  if (to == nullptr) {
    /* If the to node is null, walk forward from the from node while only
     * one INTRA_FUNC outgoing edge is found. */
    assert(from != nullptr);
    ELFCFGNode *p = from;
    do {
      std::vector<ELFCFGEdge *> IntraOutEdges;
      std::copy_if(p->Outs.begin(), p->Outs.end(),
                   std::back_inserter(IntraOutEdges), [this](ELFCFGEdge *e) {
                     return e->Type == ELFCFGEdge::EdgeType::INTRA_FUNC &&
                            e->Sink != getEntryNode();
                   });
      if (IntraOutEdges.size() == 1)
        p = IntraOutEdges.front()->Sink;
      else
        p = nullptr;
    } while (p && p != from);
    return true;
  }

  assert(from->CFG == to->CFG);
  if (from == to)
    return true;
  ELFCFGNode *p = from;
  while (p && p != to) {
    if (p->FTEdge) {
      p->FTEdge->Weight += cnt;
      p = p->FTEdge->Sink;
    } else
      p = nullptr;
  }
  if (!p) return false;
  return true;
}

// Apply counter (CNT) to the edge from node from -> to. Both nodes are from the
// same cfg.
void ELFCFG::mapBranch(ELFCFGNode *from, ELFCFGNode *to, uint64_t cnt,
                       bool isCall, bool isReturn) {
  assert(from->CFG == to->CFG);

  for (auto &e : from->Outs) {
    bool edgeTypeOk = true;
    if (!isCall && !isReturn)
      edgeTypeOk = e->Type == ELFCFGEdge::INTRA_FUNC ||
                   e->Type == ELFCFGEdge::INTRA_DYNA;
    else
      if (isCall)
        edgeTypeOk = e->Type == ELFCFGEdge::INTRA_RSC;
      if (isReturn)
        edgeTypeOk = e->Type == ELFCFGEdge::INTRA_RSR;
    if (edgeTypeOk && e->Sink == to) {
      e->Weight += cnt;
      return;
    }
  }

  ELFCFGEdge::EdgeType type = ELFCFGEdge::INTRA_DYNA;
  if (isCall)
    type = ELFCFGEdge::INTRA_RSC;
  else if (isReturn)
    type = ELFCFGEdge::INTRA_RSR;

  createEdge(from, to, type)->Weight += cnt;
}

// Apply counter (CNT) for calls/returns/ that cross function boundaries.
void ELFCFG::mapCallOut(ELFCFGNode *from, ELFCFGNode *to, uint64_t toAddr,
                        uint64_t cnt, bool isCall, bool isReturn) {
  assert(from->CFG == this);
  assert(from->CFG != to->CFG);
  ELFCFGEdge::EdgeType edgeType = ELFCFGEdge::INTER_FUNC_RETURN;
  if (isCall ||
      (toAddr && to->CFG->getEntryNode() == to && toAddr == to->MappedAddr))
    edgeType = ELFCFGEdge::INTER_FUNC_CALL;
  if (isReturn)
    edgeType= ELFCFGEdge::INTER_FUNC_RETURN;
  for (auto &e : from->CallOuts)
    if (e->Sink == to && e->Type == edgeType) {
      e->Weight += cnt;
      return ;
    }
  createEdge(from, to, edgeType)->Weight += cnt;
}

void ELFCFGBuilder::buildCFGs() {
  auto symbols = View->ViewFile->symbols();
  std::map<StringRef, std::list<SymbolRef>> groups;
  for (const SymbolRef &sym : symbols) {
    auto r = sym.getType();
    auto s = sym.getName();
    if (r && s && *r == SymbolRef::ST_Function) {
      StringRef symName = *s;
      /*
      lld::elf::Symbol *PSym =
          Plo ? Plo->Symtab->find(symName) : prop->Symtab->find(symName);
      if (PSym) (PSym->kind() == lld::elf::Symbol::UndefinedKind)){ 
        fprintf(stderr, "%s UNDEFINED KIND\n", symName.str().c_str());
        continue;
      }
      */
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
      if (L != groups.end()) {
        L->second.push_back(sym);
      }
    }
  }

  for (auto &i : groups) {
    assert(i.second.size() >= 1);
    std::map<uint64_t, std::unique_ptr<ELFCFGNode>> tmpNodeMap;
    SymbolRef cfgSym = *(i.second.begin());
    StringRef cfgName = i.first;
    std::unique_ptr<ELFCFG> cfg(new ELFCFG(View, cfgName, 0));
    for (SymbolRef sym : i.second) {
      auto symNameE = sym.getName();
      auto sectionIE = sym.getSection();
      if (symNameE && sectionIE &&
          (*sectionIE) != sym.getObject()->section_end()) {
        StringRef symName = *symNameE;
        uint64_t symShndx = (*sectionIE)->getIndex();
        // Note here: BB symbols only carry size information when
        // -fbasicblock-section=all. Objects built with
        // -fbasicblock-section=labels do not have size information
        // for BB symbols.
        uint64_t symSize = llvm::object::ELFSymbolRef(sym).getSize();
        // Drop bb sections with no code
        if (!symSize)
          continue;
        auto *sE = Prop->Propf->findSymbol(symName);
        if (sE) {
          if (tmpNodeMap.find(sE->Ordinal) != tmpNodeMap.end()) {
            error("Internal error checking cfg map.");
            return;
          }
          tmpNodeMap.emplace(
              std::piecewise_construct, std::forward_as_tuple(sE->Ordinal),
              std::forward_as_tuple(new ELFCFGNode(symShndx, symName, sE->Size,
                                                   sE->Ordinal, cfg.get())));
          continue;
        }
        // Otherwise fallthrough to ditch cfg & tmpNodeMap.
      }
      tmpNodeMap.clear();
      cfg.reset(nullptr);
      break;
    }

    if (tmpNodeMap.empty())
      cfg.reset(nullptr);

    if (!cfg)
      continue; // to next cfg group.

    uint32_t groupShndx = 0;
    for (auto &T : tmpNodeMap) {
      if (groupShndx != 0 && T.second->Shndx == groupShndx) {
        cfg.reset(nullptr);
        tmpNodeMap.clear();
        error("[Propeller]: Basicblock sections must not have same section "
              "index, this is usually caused by -fbasicblock-sections=labels. "
              "Use -fbasicblock-sections=list/all instead.");
        return ;
      }
      groupShndx = T.second->Shndx;
    }

    if (cfg) {
      buildCFG(*cfg, cfgSym, tmpNodeMap);
      View->CFGs.emplace(cfg->Name, std::move(cfg));
    }
  } // Enf of processing all groups.
}

// Build map: TextSection -> It's Relocation Section.
// ELF file only contains link from Relocation Section -> It's text section.
void ELFCFGBuilder::buildRelocationSectionMap(
    std::map<uint64_t, section_iterator> &relocationSectionMap) {
  for (section_iterator i = View->ViewFile->section_begin(),
         J = View->ViewFile->section_end(); i != J; ++i) {
    SectionRef secRef = *i;
    if (llvm::object::ELFSectionRef(secRef).getType() == llvm::ELF::SHT_RELA) {
      section_iterator r = secRef.getRelocatedSection();
      assert(r != J);
      relocationSectionMap.emplace(r->getIndex(), *i);
    }
  }
}

// Build map: basicblock section index -> basicblock section node.
void ELFCFGBuilder::buildShndxNodeMap(
    std::map<uint64_t, std::unique_ptr<ELFCFGNode>> &tmpNodeMap,
    std::map<uint64_t, ELFCFGNode *> &shndxNodeMap) {
  for (auto &nodeL : tmpNodeMap) {
    auto &node = nodeL.second;
    auto insertResult = shndxNodeMap.emplace(node->Shndx, node.get());
    (void)(insertResult);
    assert(insertResult.second);
  }
}

void ELFCFGBuilder::buildCFG(
    ELFCFG &cfg, const SymbolRef &cfgSym,
    std::map<uint64_t, std::unique_ptr<ELFCFGNode>> &tmpNodeMap) {
  std::map<uint64_t, ELFCFGNode *> shndxNodeMap;
  buildShndxNodeMap(tmpNodeMap, shndxNodeMap);

  std::map<uint64_t, section_iterator> relocationSectionMap;
  buildRelocationSectionMap(relocationSectionMap);

  // Recursive call edges.
  std::list<ELFCFGEdge *> rscEdges;
  for (auto &nPair : tmpNodeMap) {
    ELFCFGNode *srcNode = nPair.second.get();
    auto relaSecRefI = relocationSectionMap.find(srcNode->Shndx);
    if (relaSecRefI == relocationSectionMap.end())
      continue;

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
      uint64_t symShndx((*sectionIE)->getIndex());
      ELFCFGNode *targetNode{nullptr};
      auto result = shndxNodeMap.find(symShndx);
      if (result != shndxNodeMap.end()) {
        targetNode = result->second;
        if (targetNode) {
          ELFCFGEdge *e = cfg.createEdge(srcNode, targetNode,
                                         isRSC ? ELFCFGEdge::INTRA_RSC
                                               : ELFCFGEdge::INTRA_FUNC);
          if (isRSC)
            rscEdges.push_back(e);
        }
      }
    }
  }

  // Create recursive-self-return edges for all exit edges.
  // In the following example, create an edge bb5->bb3
  // FuncA:
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
           (*n->Outs.begin())->Type == ELFCFGEdge::INTRA_RSC)) {
        // Now "n" is the exit node.
        cfg.createEdge(n.get(), rEdge->Src, ELFCFGEdge::INTRA_RSR);
      }
    }
  }
  calculateFallthroughEdges(cfg, tmpNodeMap);

  // Transfer nodes ownership to cfg and destroy tmpNodeMap.
  for (auto &pair : tmpNodeMap) {
    cfg.Nodes.emplace_back(std::move(pair.second));
    pair.second.reset(nullptr);
  }
  tmpNodeMap.clear();

  // Set cfg size and re-calculate size of the entry basicblock, which is
  // initially the size of the whole function.
  cfg.Size = cfg.getEntryNode()->ShSize;
  cfg.forEachNodeRef([&cfg](ELFCFGNode &n) {
    if (&n != cfg.getEntryNode())
      cfg.getEntryNode()->ShSize -= n.ShSize;
  });
}

// Calculate fallthroughs.  edge p->Q is fallthrough if p & q are
// adjacent, and there is a NORMAL edge from p->Q.
void ELFCFGBuilder::calculateFallthroughEdges(
    ELFCFG &cfg, std::map<uint64_t, std::unique_ptr<ELFCFGNode>> &tmpNodeMap) {
  /*
    tmpNodeMap groups nodes according to their address:
      addr1: [Node1]
      addr2: [Node2]
      addr3: [Node3]
      addr4: [Node4]
    And addr1 < addr2 < addr3 < addr4.
  */
  auto setupFallthrough = [&cfg](ELFCFGNode *n1, ELFCFGNode *n2) {
    for (auto *e : n1->Outs)
      if (e->Type == ELFCFGEdge::INTRA_FUNC && e->Sink == n2) {
        n1->FTEdge = e;
        return;
      }
    if (n1->ShSize == 0)
      // An empty section always fallthrough to the next adjacent section.
      n1->FTEdge = cfg.createEdge(n1, n2, ELFCFGEdge::INTRA_FUNC);
  };

  for (auto p = tmpNodeMap.begin(), q = std::next(p), e = tmpNodeMap.end();
       q != e; ++p, ++q)
    setupFallthrough(p->second.get(), q->second.get());
}

// Create an ELFView instance that corresponds to a single ELF file.
ELFView *ELFView::create(const StringRef &vN, const uint32_t ordinal,
                         const MemoryBufferRef &fR) {
  const char *FH = fR.getBufferStart();
  if (fR.getBufferSize() > 6 && FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' &&
      FH[3] == 'F') {
    auto r = ObjectFile::createELFObjectFile(fR);
    if (r)
      return new ELFView(*r, vN, ordinal, fR);
  }
  return nullptr;
}

std::ostream &operator<<(std::ostream &out, const ELFCFGNode &node) {
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

std::ostream &operator<<(std::ostream &out, const ELFCFGEdge &edge) {
  static const char *TypeStr[] = {"", " (*RSC*)", " (*RSR*)", " (*DYNA*)"};
  out << "edge: " << *edge.Src << " -> " << *edge.Sink << " [" << std::setw(12)
      << std::setfill('0') << std::noshowbase << std::dec << edge.Weight << "]"
      << TypeStr[edge.Type];
  return out;
}

std::ostream &operator<<(std::ostream &out, const ELFCFG &cfg) {
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

}  // namespace plo
}  // namespace lld
