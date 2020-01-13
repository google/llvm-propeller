//===------------------------- Propeller.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Propeller.cpp is the entry point to Propeller framework. The main
// functionalities are:
//
//   - parses propeller profile file, which contains profile information in the
//     granularity of basicblocks.  (a)
//
//   - parses each ELF object file and generates CFG based on relocation
//     information of each basicblock section.
//
//   - maps profile in (a) onto (b) and get CFGs with profile counters (c)
//
//   - calls optimization passes that uses (c).
//
//===----------------------------------------------------------------------===//

#include "Propeller.h"
#include "PropellerBBReordering.h"
#include "PropellerConfig.h"
#include "PropellerCFG.h"

#ifdef PROPELLER_PROTOBUF
#include "propeller_cfg.pb.h"
#endif
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#if LLVM_ON_UNIX
#include <unistd.h>
#endif

#include <fstream>
#include <list>
#include <map>
#include <numeric> // For std::accumulate.
#include <tuple>
#include <vector>

namespace lld {
namespace propeller {

Propeller::Propeller() : Propf(nullptr) {}

Propeller::~Propeller() {}

// Read the "@" directives in the propeller file, compare it against "-o"
// filename, return true "-o" file name equals to one of the "@" directives.
bool Propfile::matchesOutputFileName(const StringRef outputFileName) {
  int outputFileTagSeen = 0;
  std::string line;
  LineNo = 0;
  while ((std::getline(PropfStream, line)).good()) {
    ++LineNo;
    if (line.empty())
      continue;
    if (line[0] != '@')
      break;
    ++outputFileTagSeen;
    if (StringRef(line.c_str() + 1) == outputFileName)
      return true;
  }
  if (outputFileTagSeen)
    return false;
  // If no @outputFileName is specified, reset the stream and assume linker
  // shall proceed propellering.
  PropfStream.close();
  PropfStream.open(PropfName);
  LineNo = 0;
  return true;
}

// Given a symbol name, return the corresponding SymbolEntry pointer.
// This is done by looking into table SymbolNameMap, which is a 2-dimension
// lookup table. The first dimension is the function name, the second one the
// bbindex. For example, symbol "111.bb.foo" is placed in
// SymbolNameMap["foo"]["3"], symbol "foo" is placed in
// SymbolNameMap["foo"][""].
SymbolEntry *Propfile::findSymbol(StringRef symName) {
  StringRef funcName;
  StringRef bbIndex;
  std::string tmpStr;
  if (!SymbolEntry::isBBSymbol(symName, &funcName, &bbIndex)) {
    funcName = symName;
    bbIndex = "";
  } else {
    // When symName is like "11111.bb.foo", set BBIndex to "5".
    // "1111" -> "4".
    tmpStr = std::to_string(bbIndex.size());
    bbIndex = StringRef(tmpStr);
  }
  auto L1 = SymbolNameMap.find(funcName);
  if (L1 != SymbolNameMap.end()) {
    auto L2 = L1->second.find(bbIndex);
    if (L2 != L1->second.end())
      return L2->second;
  }
  return nullptr;
}

void Propfile::reportParseError(const StringRef msg) const {
  error(PropfName + ":" + std::to_string(LineNo) + ": " + msg);
}

// Refer header file for detailed information about symbols section.
bool Propfile::readSymbols() {
  std::string line;
  // A list of bbsymbols<ordinal, function_ordinal, bbindex and size> that
  // appears before its wrapping function. This should be rather rare.
  std::list<std::tuple<uint64_t, uint64_t, StringRef, uint64_t>> bbSymbols;
  std::map<std::string, std::set<uint64_t>> HotBBSymbols;
  auto IsHotBB = [this, &HotBBSymbols](SymbolEntry *FuncSym,
                                       StringRef BBIndex) -> bool {
    std::string N("");
    for (auto A : FuncSym->Aliases) {
      if (N.empty())
        N = A.str(); // Most of the times.
      else
        N += "/" + A.str();
    }
    auto I0 = HotBBSymbols.find(N);
    if (I0 == HotBBSymbols.end())
      return false;
    // Under AllBBMode, all BBs within a hot function are hotbbs.
    if (this->AllBBMode) return true;
    uint64_t index = std::stoull(BBIndex.str());
    return I0->second.find(index) != I0->second.end();
  };
  std::map<std::string, std::set<uint64_t>>::iterator CurrentHotBBSetI =
      HotBBSymbols.end();
  while (std::getline(PropfStream, line).good()) {
    ++LineNo;
    if (line.empty())
      continue;
    if (line == "#AllBB") {
      AllBBMode = true;
      continue;
    }
    if (line[0] == '#' || line[0] == '@')
      continue;
    if (line[0] == '!' && line.size() > 1) {
      if (AllBBMode) {
        if (line[1] != '!' &&
            !HotBBSymbols.emplace(line.substr(1), std::set<uint64_t>())
                 .second) {
          reportParseError("duplicated hot bb function field");
          return false;
        }
        continue;
      }
      // Now AllBBMode is false, we consider every function and every hot bbs.
      if (line[1] == '!') {
        uint64_t bbindex = std::stoull(line.substr(2));
        if (CurrentHotBBSetI == HotBBSymbols.end() || !bbindex) {
          reportParseError("invalid hot bb index field");
          return false;
        }
        CurrentHotBBSetI->second.insert(bbindex);
      } else {
        auto RP = HotBBSymbols.emplace(line.substr(1), std::set<uint64_t>());
        if (!RP.second) {
          reportParseError("duplicated hot bb function field");
          return false;
        }
        CurrentHotBBSetI = RP.first;
      }
      continue;
    }
    if (line[0] == 'B' || line[0] == 'F') {
      LineTag = line[0];
      break; // Done symbol section.
    }
    if (line[0] == 'S') {
      LineTag = line[0];
      continue;
    }
    StringRef lineStrRef(line);

    uint64_t symOrdinal;
    uint64_t symSize;
    auto l1S = lineStrRef.split(' ');
    auto l1 = l1S.first;
    auto l2S = l1S.second.split(' ');
    auto l2 = l2S.first;
    auto ephemeralStr = l2S.second;
    if (l1.getAsInteger(10, symOrdinal) /* means error happens */ ||
        symOrdinal == 0) {
      reportParseError("invalid ordinal field");
      return false;
    }
    if (l2.getAsInteger(16, symSize)) {
      reportParseError("invalid size field");
      return false;
    }
    if (ephemeralStr.empty()) {
      reportParseError("invalid name field");
      return false;
    }
    if (ephemeralStr[0] == 'N') { // Function symbol?
      // Save ephemeralStr for persistency across Propeller lifecycle.
      StringRef savedNameStr = PropfileStrSaver.save(ephemeralStr.substr(1));
      SymbolEntry::AliasesTy sAliases;
      savedNameStr.split(sAliases, '/');
      StringRef sName = sAliases[0];
      assert(SymbolOrdinalMap.find(symOrdinal) == SymbolOrdinalMap.end());
      createFunctionSymbol(symOrdinal, sName, std::move(sAliases), symSize);
    } else {
      // It's a bb symbol.
      auto lineStrRef = ephemeralStr.split('.');
      uint64_t funcIndex;
      if (lineStrRef.first.getAsInteger(10, funcIndex) || funcIndex == 0) {
        reportParseError("invalid function index field");
        return false;
      }
      // Only save the index part, which is highly reusable. Note
      // PropfileStrSaver is a UniqueStringSaver.
      StringRef bbIndex = PropfileStrSaver.save(lineStrRef.second);
      auto existingI = SymbolOrdinalMap.find(funcIndex);
      if (existingI != SymbolOrdinalMap.end()) {
        if (existingI->second->BBTag) {
          reportParseError("index '" + std::to_string(funcIndex) +
                           "' is not a function index, but a bb index");
          return false;
        }
        createBasicBlockSymbol(symOrdinal, existingI->second.get(), bbIndex,
                               symSize,
                               IsHotBB(existingI->second.get(), bbIndex));
      } else
        // A bb symbol appears earlier than its wrapping function, rare, but
        // not impossible, rather play it safely.
        bbSymbols.emplace_back(symOrdinal, funcIndex, bbIndex, symSize);
    }
  } // End of iterating all symbols.

  for (std::tuple<uint64_t, uint64_t, StringRef, uint64_t> &sym : bbSymbols) {
    uint64_t symOrdinal;
    uint64_t funcIndex;
    uint64_t symSize;
    StringRef bbIndex;
    std::tie(symOrdinal, funcIndex, bbIndex, symSize) = sym;
    auto existingI = SymbolOrdinalMap.find(funcIndex);
    if (existingI == SymbolOrdinalMap.end()) {
      reportParseError("function with index number '" +
                       std::to_string(funcIndex) + "' does not exist");
      return false;
    }
    SymbolEntry *FuncSym = existingI->second.get();
    createBasicBlockSymbol(symOrdinal, FuncSym, bbIndex, symSize,
                           IsHotBB(FuncSym, bbIndex));
  }
  return true;
}

// Helper method to parse a branch or fallthrough record like below
//   10 12 232590 R
static bool parseBranchOrFallthroughLine(StringRef lineRef,
                                         uint64_t *fromNodeIdx,
                                         uint64_t *toNodeIdx, uint64_t *count,
                                         char *type) {
  /*
  auto getInt = [](const StringRef &S) -> uint64_t {
    uint64_t r;
    if (S.getAsInteger(10, r) // string contains more than numbers
        || r == 0)
      return 0;
    return r;
  };
  */
  auto s0 = lineRef.split(' ');
  auto s1 = s0.second.split(' ');
  auto s2 = s1.second.split(' ');
  if (s0.first.getAsInteger(10, *fromNodeIdx) ||
      s1.first.getAsInteger(10, *toNodeIdx) ||
      s2.first.getAsInteger(10, *count))
    return false;
  if (!*count)
    return false;
  if (!s2.second.empty()) {
    if (s2.second == "C" || s2.second == "R")
      *type = s2.second[0];
    else
      return false;
    if (!*fromNodeIdx || !*toNodeIdx)
      return false;
  } else
    *type = '\0';
  return true;
}

// Read propeller profile. Refer header file for detail about propeller profile.
bool Propfile::processProfile() {
  std::string line;
  uint64_t branchCnt = 0;
  uint64_t fallthroughCnt = 0;
  while (std::getline(PropfStream, line).good()) {
    ++LineNo;
    if (line[0] == '#' || line[0] == '!')
      continue;
    if (line[0] == 'S' || line[0] == 'B' || line[0] == 'F') {
      LineTag = line[0];
      continue;
    }
    if (LineTag != 'B' && LineTag != 'F')
      break;

    StringRef L(line); // LineBuf is null-terminated.
    uint64_t from, to, count;
    char tag;
    auto UpdateOrdinal = [this](uint64_t OriginOrdinal) -> uint64_t {
      auto I = OrdinalRemapping.find(OriginOrdinal);
      if (I != OrdinalRemapping.end()) {
        //fprintf(stderr, "Updated %lu->%lu\n", OriginOrdinal, I->second);
        return I->second;
      }
      return OriginOrdinal;
    };
    if (!parseBranchOrFallthroughLine(L, &from, &to, &count, &tag)) {
      reportParseError("unrecognized line:\n" + L.str());
      return false;
    }
    from = UpdateOrdinal(from);
    to = UpdateOrdinal(to);
    CFGNode *fromN = prop->findCfgNode(from);
    CFGNode *toN = prop->findCfgNode(to);
    if (!fromN || !toN)
      continue;

    if (LineTag == 'B') {
      if(!fromN || !toN)
        continue;
      ++branchCnt;
      if (fromN->CFG == toN->CFG)
        fromN->CFG->mapBranch(fromN, toN, count, tag == 'C', tag == 'R');
      else
        fromN->CFG->mapCallOut(fromN, toN, 0, count, tag == 'C', tag == 'R');
    } else {
      if (fromN && toN && (fromN->CFG != toN->CFG))
        continue;
      ++fallthroughCnt;
      // LineTag == 'F'
      ControlFlowGraph * cfg = fromN ? fromN->CFG : toN->CFG;
      cfg->markPath(fromN, toN, count);
    }
  }

  if (!branchCnt)
    warn("propeller processed 0 branch info");
  if (!fallthroughCnt)
    warn("propeller processed 0 fallthrough info");
  return true;
}

// Parse each ELF file, create CFG and attach profile data to CFG.
void Propeller::processFile(ObjectView *view) {
  if (view) {
    std::map<uint64_t, uint64_t> OrdinalRemapping;
    if (CFGBuilder(view).buildCFGs(OrdinalRemapping)) {
      // Updating global data structure.
      std::lock_guard<std::mutex> lock(Lock);
      Views.emplace_back(view);
      for (std::pair<const StringRef, std::unique_ptr<ControlFlowGraph>> &P :
           view->CFGs) {
        auto result = CFGMap[P.first].emplace(P.second.get());
        (void)(result);
        assert(result.second);
      }
      Propf->OrdinalRemapping.insert(OrdinalRemapping.begin(),
                                     OrdinalRemapping.end());

    } else {
      warn("skipped building CFG for '" + view->ViewName +"'");
      ++ProcessFailureCount;
    }
  }
}

CFGNode *Propeller::findCfgNode(uint64_t symbolOrdinal) {
  if (symbolOrdinal == 0)
    return nullptr;
  assert(Propf->SymbolOrdinalMap.find(symbolOrdinal) !=
         Propf->SymbolOrdinalMap.end());
  SymbolEntry *symbol = Propf->SymbolOrdinalMap[symbolOrdinal].get();
  if (!symbol) {
    // This is an internal error, should not happen.
    error(std::string("invalid symbol ordinal: " +
                      std::to_string(symbolOrdinal)));
    return nullptr;
  }
  SymbolEntry *funcSym = symbol->BBTag ? symbol->ContainingFunc : symbol;
  for (auto &funcAliasName : funcSym->Aliases) {
    auto cfgLI = CFGMap.find(funcAliasName);
    if (cfgLI == CFGMap.end())
      continue;

    // Objects (CfgLI->second) are sorted in the way they appear on the command
    // line, which is the same as how linker chooses the weak symbol definition.
    if (!symbol->BBTag) {
      for (auto *CFG : cfgLI->second)
        // Check CFG does have name "SymName".
        for (auto &node : CFG->Nodes)
          if (node->ShName == funcAliasName)
            return node.get();
    } else {
      uint32_t NumOnes;
      // Compare the number of "a" in aaa...a.BB.funcname against integer
      // NumOnes.
      if (symbol->Name.getAsInteger(10, NumOnes) || !NumOnes)
        warn("internal error, BB name is invalid: '" + symbol->Name.str());
      else
        for (auto *CFG : cfgLI->second)
          for (auto &node : CFG->Nodes) {
            // Skip the entry node as we know this is a BB symbol.
            if (node->isEntryNode())
              continue;
            // Check CFG does have name "SymName".
            auto t = node->ShName.find_first_of('.');
            if (t != std::string::npos && t == NumOnes)
              return node.get();
          }
    }
  }
  return nullptr;
}

void Propeller::calculateNodeFreqs() {
  auto sumEdgeWeights = [](std::vector<CFGEdge *> &edges) -> uint64_t {
    return std::accumulate(
        edges.begin(), edges.end(), 0,
        [](uint64_t pSum, const CFGEdge *edge) { return pSum + edge->Weight; });
  };
  auto ZeroOutEdgeWeights = [](std::vector<CFGEdge *> &Es) {
    for (auto *E : Es)
      E->Weight = 0;
  };

  for (auto &cfgP : CFGMap) {
    auto &cfg = *cfgP.second.begin();
    if (cfg->Nodes.empty())
      continue;
    cfg->forEachNodeRef([&cfg, &sumEdgeWeights,
                         &ZeroOutEdgeWeights](CFGNode &node) {
      uint64_t maxCallOut =
          node.CallOuts.empty()
              ? 0
              : (*std::max_element(node.CallOuts.begin(), node.CallOuts.end(),
                                   [](const CFGEdge *E1, const CFGEdge *E2) {
                                     return E1->Weight < E2->Weight;
                                   }))
                    ->Weight;
      if (node.HotTag)
        node.Freq =
            std::max({sumEdgeWeights(node.Outs), sumEdgeWeights(node.Ins),
                      sumEdgeWeights(node.CallIns), maxCallOut});
      else {
        node.Freq = 0;
        ZeroOutEdgeWeights(node.Ins);
        ZeroOutEdgeWeights(node.Outs);
        ZeroOutEdgeWeights(node.CallIns);
        ZeroOutEdgeWeights(node.CallOuts);
      }

      cfg->Hot |= (node.Freq != 0);

      // Find non-zero frequency nodes with fallthroughs and propagate the
      // weight via the fallthrough edge if no other normal edge carries weight.
      if (node.Freq && node.FTEdge && node.FTEdge->Sink->HotTag) {
        uint64_t sumIntraOut = 0;
        for (auto * e: node.Outs) {
          if (e->Type == CFGEdge::EdgeType::INTRA_FUNC)
            sumIntraOut += e->Weight;
        }

        if (!sumIntraOut)
          node.FTEdge->Weight = node.Freq;
      }
    });

    /*
    if (cfg->Hot && cfg->getEntryNode()->Freq == 0)
      cfg->getEntryNode()->Freq = 1;
      */
  }
}

// Returns true if linker output target matches propeller profile.
bool Propeller::checkTarget() {
  if (propellerConfig.optPropeller.empty())
    return false;
  std::string propellerFileName = propellerConfig.optPropeller.str();
  // Propfile takes ownership of FPtr.
  Propf.reset(new Propfile(propellerFileName));
  Propf->PropfStream.open(Propf->PropfName);
  if (!Propf->PropfStream.good()) {
    error(std::string("failed to open '") + propellerFileName + "'");
    return false;
  }
  return Propf->matchesOutputFileName(
      llvm::sys::path::filename(propellerConfig.optLinkerOutputFile));
}

// Entrance of Propeller framework. This processes each elf input file in
// parallel and stores the result information.
bool Propeller::processFiles(std::vector<ObjectView *> &views) {
  if (!Propf->readSymbols()) {
    error(std::string("invalid propfile: '") +
          propellerConfig.optPropeller.str() + "'");
    return false;
  }

  if(!propellerConfig.optBBOrder.empty()){
    for (StringRef s: propellerConfig.optBBOrder){
      auto r = s.split('.');
      std::string bbIndex = r.first.str() == "0" ? "" : r.first;
      std::string funcName = r.second;
      bool found = false;
      auto l1 = prop->Propf->SymbolNameMap.find(funcName);
      if (l1 != prop->Propf->SymbolNameMap.end()) {
        auto l2 = l1->second.find(bbIndex);
        if (l2 != l1->second.end()) {
          BBLayouts[funcName].push_back(l2->second->Ordinal);
          found = true;
        }
      }
      if (!found)
        warn("Symbol not found: " + s);
    }
  }

  ProcessFailureCount = 0;
  llvm::parallel::for_each(
      llvm::parallel::parallel_execution_policy(), views.begin(), views.end(),
      std::bind(&Propeller::processFile, this, std::placeholders::_1));

  if (ProcessFailureCount * 100 / views.size() >= 50)
    warn("propeller failed to parse more than half the objects, "
         "optimization would suffer");

  /* Drop alias cfgs. */
  for (SymbolEntry *funcS : Propf->FunctionsWithAliases) {
    ControlFlowGraph *primaryCfg = nullptr;
    CfgMapTy::iterator primaryCfgMapEntry;
    for (StringRef &AliasName : funcS->Aliases) {
      auto cfgMapI = CFGMap.find(AliasName);
      if (cfgMapI == CFGMap.end())
        continue;

      if (cfgMapI->second.empty())
        continue;

      if (!primaryCfg ||
          primaryCfg->Nodes.size() < (*cfgMapI->second.begin())->Nodes.size()) {
        if (primaryCfg)
          CFGMap.erase(primaryCfgMapEntry);

        primaryCfg = *cfgMapI->second.begin();
        primaryCfgMapEntry = cfgMapI;
      } else
        CFGMap.erase(cfgMapI);
    }
  }

  // Map profiles.
  if (!Propf->processProfile())
    return false;

  calculateNodeFreqs();

  dumpCfgs();
  
  // Releasing all support data (symbol ordinal / name map, saved string refs,
  // etc) before moving to reordering.
  Propf.reset(nullptr);
  return true;
}

bool Propeller::dumpCfgs() {
  if (propellerConfig.optDumpCfgs.empty()) return true;

  std::set<std::string> cfgToDump(propellerConfig.optDumpCfgs.begin(),
                                  propellerConfig.optDumpCfgs.end());
  llvm::SmallString<128> cfgOutputDir(propellerConfig.optLinkerOutputFile);
  llvm::sys::path::remove_filename(cfgOutputDir);
  for (auto &cfgName : cfgToDump) {
    StringRef cfgNameRef(cfgName);
    if (cfgName == "@" || cfgNameRef.startswith("@@")) {
#ifdef PROPELLER_PROTOBUF
      if (!protobufPrinter.get())
        protobufPrinter.reset(ProtobufPrinter::create(
            Twine(propellerConfig.optLinkerOutputFile, ".cfg.pb.txt").str()));
      if (cfgNameRef.consume_front("@@")) {
        protobufPrinter->clearCFGGroup();
        const bool cfgNameEmpty = cfgNameRef.empty();
        for (auto &cfgMapEntry: CFGMap)
          for (auto *cfg: cfgMapEntry.second)
            if (cfgNameEmpty || cfg->Name == cfgNameRef)
              protobufPrinter->addCFG(*cfg);
        protobufPrinter->printCFGGroup();
        protobufPrinter.reset(nullptr);
      }
#else
      warn("dump to protobuf not supported");
#endif
      continue;
    }
    auto cfgLI = CFGMap.find(cfgName);
    if (cfgLI == CFGMap.end()) {
      warn("could not dump cfg for function '" + cfgName +
           "' : no such function name exists");
      continue;
    }
    int Index = 0;
    for (auto *CFG : cfgLI->second)
      if (CFG->Name == cfgName) {
        llvm::SmallString<128> cfgOutput = cfgOutputDir;
        if (++Index <= 1)
          llvm::sys::path::append(cfgOutput, (CFG->Name + ".dot"));
        else
          llvm::sys::path::append(
              cfgOutput,
              (CFG->Name + "." + StringRef(std::to_string(Index) + ".dot")));
        if (!CFG->writeAsDotGraph(StringRef(cfgOutput)))
          warn("failed to dump CFG: '" + cfgName + "'");
      }
  }
  return true;
}

ObjectView *Propeller::createObjectView(const StringRef &vN,
                                        const uint32_t ordinal,
                                        const MemoryBufferRef &fR) {
  const char *FH = fR.getBufferStart();
  if (fR.getBufferSize() > 6 && FH[0] == 0x7f && FH[1] == 'E' && FH[2] == 'L' &&
      FH[3] == 'F') {
    auto r = ObjectFile::createELFObjectFile(fR);
    if (r)
      return new ObjectView(*r, vN, ordinal, fR);
  }
  return nullptr;
}

// Generate symbol ordering file according to selected optimization pass and
// feed it to the linker.
std::vector<StringRef> Propeller::genSymbolOrderingFile() {
  int total_objs = 0;
  int hot_objs = 0;
  for (auto &Obj : Views) {
    for (auto &CP : Obj->CFGs) {
      auto &C = *(CP.second);
      if (C.isHot()) {
        ++hot_objs;
        break; // process to next object.
      }
    }
    ++total_objs;
  }

  std::list<StringRef> symbolList(1, "Hot");
  const auto hotPlaceHolder = symbolList.begin();
  const auto coldPlaceHolder = symbolList.end();
  propLayout = make<PropellerBBReordering>();
  propLayout->doSplitOrder(symbolList, hotPlaceHolder, coldPlaceHolder);
#ifdef PROPELLER_PROTOBUF
  if (protobufPrinter) {
    protobufPrinter->printCFGGroup();
    protobufPrinter.reset();
  }
#endif

  calculateLegacy(symbolList, hotPlaceHolder, coldPlaceHolder);

  if (!propellerConfig.optDumpSymbolOrder.empty()) {
    FILE *fp = fopen(propellerConfig.optDumpSymbolOrder.str().c_str(), "w");
    if (!fp)
      warn(StringRef("dump symbol order: failed to open ") + "'" +
           propellerConfig.optDumpSymbolOrder + "'");
    else {
      for (auto &sym : symbolList) {
        auto A = sym.split(".BB.");
        if (A.second.empty()) {
          fprintf(fp, "%s\n", sym.str().c_str());
        } else {
          fprintf(fp, "%zu.BB.%s\n", A.first.size(), A.second.str().c_str());
        }
      }
      fclose(fp);
      llvm::outs() << "Dumped symbol order file to: '"
                   << propellerConfig.optDumpSymbolOrder.str() << "'\n";
    }
  }

  symbolList.erase(hotPlaceHolder);

  return std::vector<StringRef>(
      std::move_iterator<std::list<StringRef>::iterator>(symbolList.begin()),
      std::move_iterator<std::list<StringRef>::iterator>(symbolList.end()));
}

// Calculate a std::list of basicblock symbols that are to be kept in the final
// binary. For hot bb symbols, all bb symbols are to be dropped, because the
// content of all hot bb sections are grouped together with the origin function.
// For cold bb symbols, only the first bb symbols of the same function are kept.
void Propeller::calculateLegacy(
    std::list<StringRef> &symList,
    std::list<StringRef>::iterator hotPlaceHolder,
    std::list<StringRef>::iterator coldPlaceHolder) {
  // No function split or no cold symbols, all bb symbols shall be removed.
  if (hotPlaceHolder == coldPlaceHolder)
    return;
  // For cold bb symbols that are split and placed in cold segements,
  // only the first bb symbol of every function partition is kept.
  StringRef LastFuncName = "";
  for (auto i = std::next(hotPlaceHolder), j = coldPlaceHolder; i != j; ++i) {
    StringRef sName = *i;
    StringRef fName;
    if (SymbolEntry::isBBSymbol(sName, &fName)) {
      if (LastFuncName.empty() || LastFuncName != fName)
        PropLeg.BBSymbolsToKeep.insert(sName);
      LastFuncName = fName;
    }
  }
  return;
}

bool Propeller::ObjectViewOrdinalComparator::
operator()(const ControlFlowGraph *a, const ControlFlowGraph *b) const {
  return a->View->Ordinal < b->View->Ordinal;
}

PropellerLegacy PropLeg;

PropellerConfig propellerConfig;

} // namespace propeller
} // namespace lld
