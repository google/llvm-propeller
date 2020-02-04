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
//   - parses each ELF object file and generates controlFlowGraph based on
//     relocation information of each basicblock section.
//
//   - maps profile in (a) onto (b) and get cfgs with profile counters (c)
//
//   - calls optimization passes that uses (c).
//
//===----------------------------------------------------------------------===//

#include "Propeller.h"
#include "CodeLayout/CodeLayout.h"
#include "PropellerCFG.h"
#include "PropellerConfig.h"

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
#include <sys/stat.h>
#include <sys/types.h>
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

Propeller::Propeller() : propf(nullptr) {}

Propeller::~Propeller() {}

// Read the "@" directives in the propeller file, compare it against "-o"
// filename, return true "-o" file name equals to one of the "@" directives.
bool Propfile::matchesOutputFileName(const StringRef outputFileName) {
  int outputFileTagSeen = 0;
  std::string line;
  lineNo = 0;
  while ((std::getline(propfStream, line)).good()) {
    ++lineNo;
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
  propfStream.close();
  propfStream.open(propfName);
  lineNo = 0;
  return true;
}

// Given a symbol name, return the corresponding SymbolEntry pointer.
// This is done by looking into table symbolNameMap, which is a 2-dimension
// lookup table. The first dimension is the function name, the second one the
// bbindex. For example, symbol "111.bb.foo" is placed in
// symbolNameMap["foo"]["3"], symbol "foo" is placed in
// symbolNameMap["foo"][""].
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
  auto L1 = symbolNameMap.find(funcName);
  if (L1 != symbolNameMap.end()) {
    auto L2 = L1->second.find(bbIndex);
    if (L2 != L1->second.end())
      return L2->second;
  }
  return nullptr;
}

void Propfile::reportParseError(const StringRef msg) const {
  error(propfName + ":" + std::to_string(lineNo) + ": " + msg);
}

bool Propfile::isHotSymbol(
    SymbolEntry *func,
    const std::map<std::string, std::set<std::string>> &hotBBSymbols,
    StringRef bbIndex, SymbolEntry::BBTagTypeEnum bbtt) {
  std::string N("");
  for (auto A : func->aliases) {
    if (N.empty())
      N = A.str(); // Most of the times.
    else
      N += "/" + A.str();
  }
  auto I0 = hotBBSymbols.find(N);
  if (I0 == hotBBSymbols.end())
    return false;
  // Under allBBMode, all BBs within a hot function are hotbbs.
  if (allBBMode)
    return true;
  // If bbIndex is not provided.
  if (bbIndex.empty())
    return true;
  // Landing pads are always cold.
  if (bbtt == SymbolEntry::BB_LANDING_PAD ||
      bbtt == SymbolEntry::BB_RETURN_AND_LANDING_PAD)
    return false;
  return I0->second.find(bbIndex) != I0->second.end();
}

bool Propfile::processSymbolLine(
    StringRef symLine,
    std::list<std::tuple<uint64_t, uint64_t, StringRef, uint64_t,
                         SymbolEntry::BBTagTypeEnum>> &bbSymbolsToPostProcess,
    const std::map<std::string, std::set<std::string>> &hotBBSymbols) {
  uint64_t symOrdinal;
  uint64_t symSize;
  auto l1S = symLine.split(' ');
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
    StringRef savedNameStr = propfileStrSaver.save(ephemeralStr.substr(1));
    SymbolEntry::AliasesTy sAliases;
    savedNameStr.split(sAliases, '/');
    StringRef sName = sAliases[0];
    assert(symbolOrdinalMap.find(symOrdinal) == symbolOrdinalMap.end());
    SymbolEntry *func = createFunctionSymbol(
        symOrdinal, sName, std::move(sAliases), symSize, hotBBSymbols);
    func->hotTag = isHotSymbol(func, hotBBSymbols);
    return true;
  }

  // It's a bb symbol.
  auto bbParts = ephemeralStr.split('.');
  uint64_t funcIndex;
  if (bbParts.first.getAsInteger(10, funcIndex) || funcIndex == 0) {
    reportParseError("invalid function index field");
    return false;
  }
  // If it ends with 'r', 'l' or 'lineRef' suffix.
  char optionalSuffix = bbParts.second.back();
  SymbolEntry::BBTagTypeEnum bbTagType;
  StringRef ephemeralBBIndex;
  if (optionalSuffix == 'r' || optionalSuffix == 'l' || optionalSuffix == 'L') {
    bbTagType = SymbolEntry::toBBTagType(optionalSuffix);
    ephemeralBBIndex = bbParts.second.drop_back();
  } else {
    bbTagType = SymbolEntry::BB_NORMAL;
    ephemeralBBIndex = bbParts.second;
  }
  // Only save the index part, which is highly reusable. Note
  // propfileStrSaver is a UniqueStringSaver.
  StringRef bbIndex = propfileStrSaver.save(ephemeralBBIndex);
  auto existingI = symbolOrdinalMap.find(funcIndex);
  if (existingI != symbolOrdinalMap.end()) {
    if (existingI->second->bbTag) {
      reportParseError("index '" + std::to_string(funcIndex) +
                       "' is not a function index, but a bb index");
      return false;
    }
    bool hotTag =
        isHotSymbol(existingI->second.get(), hotBBSymbols, bbIndex, bbTagType);
    createBasicBlockSymbol(symOrdinal, existingI->second.get(), bbIndex,
                           symSize, hotTag, bbTagType);
  } else {
    // A bb symbol appears earlier than its wrapping function, rare, but
    // not impossible, rather play it safely.
    bbSymbolsToPostProcess.emplace_back(symOrdinal, funcIndex, bbIndex, symSize,
                                        bbTagType);
  }
  return true;
}

// Refer header file for detailed information about symbols section.
bool Propfile::readSymbols() {
  std::string line;
  // A list of bbsymbols<ordinal, function_ordinal, bbindex, size, type> that
  // appears before its wrapping function. This should be rather rare.
  std::list<std::tuple<uint64_t, uint64_t, StringRef, uint64_t,
                       SymbolEntry::BBTagTypeEnum>>
      bbSymbolsToPostProcess;
  std::map<std::string, std::set<std::string>> hotBBSymbols;
  std::map<std::string, std::set<std::string>>::iterator currentHotBBSetI =
      hotBBSymbols.end();
  while (std::getline(propfStream, line).good()) {
    ++lineNo;
    if (line.empty())
      continue;
    if (line == "#AllBB") {
      allBBMode = true;
      continue;
    }
    if (line[0] == '#' || line[0] == '@')
      continue;
    if (line[0] == '!' && line.size() > 1) {
      if (allBBMode) {
        if (line[1] != '!' &&
            !hotBBSymbols.emplace(line.substr(1), std::set<std::string>())
                 .second) {
          reportParseError("duplicated hot bb function field");
          return false;
        }
        continue;
      }
      // Now allBBMode is false, we consider every function and every hot bbs.
      if (line[1] == '!') {
        if (currentHotBBSetI == hotBBSymbols.end()) {
          reportParseError("invalid hot bb index field");
          return false;
        }
        currentHotBBSetI->second.insert(line.substr(2));
      } else {
        auto RP = hotBBSymbols.emplace(line.substr(1), std::set<std::string>());
        if (!RP.second) {
          reportParseError("duplicated hot bb function field");
          return false;
        }
        currentHotBBSetI = RP.first;
      }
      continue;
    }
    if (line[0] == 'B' || line[0] == 'F') {
      lineTag = line[0];
      break; // Done symbol section.
    }
    if (line[0] == 'S') {
      lineTag = 'S';
      continue;
    }
    if (!processSymbolLine(StringRef(line), bbSymbolsToPostProcess,
                           hotBBSymbols))
      return false;
  } // End of iterating all symbols.

  // Post process bb symbols that are listed before its wrapping function.
  for (std::tuple<uint64_t, uint64_t, StringRef, uint64_t,
                  SymbolEntry::BBTagTypeEnum> &sym : bbSymbolsToPostProcess) {
    uint64_t symOrdinal;
    uint64_t funcIndex;
    StringRef bbIndex;
    uint64_t symSize;
    SymbolEntry::BBTagTypeEnum bbtt;
    std::tie(symOrdinal, funcIndex, bbIndex, symSize, bbtt) = sym;
    auto existingI = symbolOrdinalMap.find(funcIndex);
    if (existingI == symbolOrdinalMap.end()) {
      reportParseError("function with index number '" +
                       std::to_string(funcIndex) + "' does not exist");
      return false;
    }
    SymbolEntry *FuncSym = existingI->second.get();
    bool hotTag = isHotSymbol(FuncSym, hotBBSymbols, bbIndex, bbtt);
    createBasicBlockSymbol(symOrdinal, FuncSym, bbIndex, symSize, hotTag, bbtt);
  }
  return true;
}

// Helper method to parse a branch or fallthrough record like below
//   10 12 232590 R
static bool parseBranchOrFallthroughLine(StringRef lineRef,
                                         uint64_t *fromNodeIdx,
                                         uint64_t *toNodeIdx, uint64_t *count,
                                         char *type) {
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
  while (std::getline(propfStream, line).good()) {
    ++lineNo;
    if (line[0] == '#' || line[0] == '!')
      continue;
    if (line[0] == 'S' || line[0] == 'B' || line[0] == 'F') {
      lineTag = line[0];
      continue;
    }
    if (lineTag != 'B' && lineTag != 'F')
      break;

    StringRef lineRef(line); // LineBuf is null-terminated.
    uint64_t from, to, count;
    char tag;
    auto UpdateOrdinal = [this](uint64_t OriginOrdinal) -> uint64_t {
      auto iter = ordinalRemapping.find(OriginOrdinal);
      if (iter != ordinalRemapping.end())
        return iter->second;
      return OriginOrdinal;
    };
    if (!parseBranchOrFallthroughLine(lineRef, &from, &to, &count, &tag)) {
      reportParseError("unrecognized line:\n" + lineRef.str());
      return false;
    }
    from = UpdateOrdinal(from);
    to = UpdateOrdinal(to);
    CFGNode *fromN = prop->findCfgNode(from);
    CFGNode *toN = prop->findCfgNode(to);
    if (!fromN || !toN)
      continue;

    if (lineTag == 'B') {
      if (!fromN || !toN)
        continue;
      ++branchCnt;
      if (fromN->controlFlowGraph == toN->controlFlowGraph)
        fromN->controlFlowGraph->mapBranch(fromN, toN, count, tag == 'C',
                                           tag == 'R');
      else
        fromN->controlFlowGraph->mapCallOut(fromN, toN, 0, count, tag == 'C',
                                            tag == 'R');
    } else {
      if (fromN && toN && (fromN->controlFlowGraph != toN->controlFlowGraph))
        continue;
      ++fallthroughCnt;
      // lineTag == 'F'
      ControlFlowGraph *cfg =
          fromN ? fromN->controlFlowGraph : toN->controlFlowGraph;
      cfg->markPath(fromN, toN, count);
    }
  }

  if (!branchCnt)
    warn("propeller processed 0 branch info");
  if (!fallthroughCnt)
    warn("propeller processed 0 fallthrough info");
  return true;
}

// Parse each ELF file, create controlFlowGraph and attach profile data to
// controlFlowGraph.
void Propeller::processFile(ObjectView *view) {
  if (view) {
    std::map<uint64_t, uint64_t> ordinalRemapping;
    if (CFGBuilder(view).buildCFGs(ordinalRemapping)) {
      // Updating global data structure.
      std::lock_guard<std::mutex> lockGuard(lock);
      Views.emplace_back(view);
      for (std::pair<const StringRef, std::unique_ptr<ControlFlowGraph>> &p :
           view->cfgs) {
        auto result = cfgMap[p.first].emplace(p.second.get());
        (void)(result);
        assert(result.second);
      }
      propf->ordinalRemapping.insert(ordinalRemapping.begin(),
                                     ordinalRemapping.end());

    } else {
      warn("skipped building controlFlowGraph for '" + view->viewName + "'");
      ++processFailureCount;
    }
  }
}

CFGNode *Propeller::findCfgNode(uint64_t symbolOrdinal) {
  if (symbolOrdinal == 0)
    return nullptr;
  assert(propf->symbolOrdinalMap.find(symbolOrdinal) !=
         propf->symbolOrdinalMap.end());
  SymbolEntry *symbol = propf->symbolOrdinalMap[symbolOrdinal].get();
  if (!symbol) {
    // This is an internal error, should not happen.
    error(std::string("invalid symbol ordinal: " +
                      std::to_string(symbolOrdinal)));
    return nullptr;
  }
  SymbolEntry *funcSym = symbol->bbTag ? symbol->containingFunc : symbol;
  for (auto &funcAliasName : funcSym->aliases) {
    auto cfgLI = cfgMap.find(funcAliasName);
    if (cfgLI == cfgMap.end())
      continue;

    // Objects (CfgLI->second) are sorted in the way they appear on the command
    // line, which is the same as how linker chooses the weak symbol definition.
    if (!symbol->bbTag) {
      for (auto *controlFlowGraph : cfgLI->second)
        // Check controlFlowGraph does have name "SymName".
        for (auto &node : controlFlowGraph->nodes)
          if (node->shName == funcAliasName)
            return node.get();
    } else {
      uint32_t numOnes;
      // Compare the number of "a" in aaa...a.bb.funcname against integer
      // numOnes.
      if (symbol->name.getAsInteger(10, numOnes) || !numOnes)
        warn("internal error, bb name is invalid: " + symbol->name.str());
      else
        for (auto *controlFlowGraph : cfgLI->second)
          for (auto &node : controlFlowGraph->nodes) {
            // Skip the entry node as we know this is a bb symbol.
            if (node->isEntryNode())
              continue;
            // Check controlFlowGraph does have name "SymName".
            auto t = node->shName.find_first_of('.');
            if (t != std::string::npos && t == numOnes)
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
        [](uint64_t pSum, const CFGEdge *edge) { return pSum + edge->weight; });
  };
  auto ZeroOutEdgeWeights = [](std::vector<CFGEdge *> &Es) {
    for (auto *E : Es)
      E->weight = 0;
  };

  for (auto &cfgP : cfgMap) {
    auto &cfg = *cfgP.second.begin();
    if (cfg->nodes.empty())
      continue;
    cfg->forEachNodeRef([&cfg, &sumEdgeWeights,
                         &ZeroOutEdgeWeights](CFGNode &node) {
      uint64_t maxCallOut =
          node.callOuts.empty()
              ? 0
              : (*std::max_element(node.callOuts.begin(), node.callOuts.end(),
                                   [](const CFGEdge *e1, const CFGEdge *e2) {
                                     return e1->weight < e2->weight;
                                   }))
                    ->weight;
      if (node.hotTag)
        node.freq =
            std::max({sumEdgeWeights(node.outs), sumEdgeWeights(node.ins),
                      sumEdgeWeights(node.callIns), maxCallOut});
      else {
        node.freq = 0;
        ZeroOutEdgeWeights(node.ins);
        ZeroOutEdgeWeights(node.outs);
        ZeroOutEdgeWeights(node.callIns);
        ZeroOutEdgeWeights(node.callOuts);
      }

      cfg->hot |= (node.freq != 0);

      // Find non-zero frequency nodes with fallthroughs and propagate the
      // weight via the fallthrough edge if no other normal edge carries weight.
      if (node.freq && node.ftEdge && node.ftEdge->sink->hotTag) {
        uint64_t sumIntraOut = 0;
        for (auto *e : node.outs) {
          if (e->type == CFGEdge::EdgeType::INTRA_FUNC)
            sumIntraOut += e->weight;
        }

        if (!sumIntraOut)
          node.ftEdge->weight = node.freq;
      }
    });
  }
}

// Returns true if linker output target matches propeller profile.
bool Propeller::checkTarget() {
  if (propConfig.optPropeller.empty())
    return false;
  std::string propellerFileName = propConfig.optPropeller.str();
  // Propfile takes ownership of FPtr.
  propf.reset(new Propfile(propellerFileName));
  propf->propfStream.open(propf->propfName);
  if (!propf->propfStream.good()) {
    error(std::string("failed to open '") + propellerFileName + "'");
    return false;
  }
  return propf->matchesOutputFileName(
      llvm::sys::path::filename(propConfig.optLinkerOutputFile));
}

// Entrance of Propeller framework. This processes each elf input file in
// parallel and stores the result information.
bool Propeller::processFiles(std::vector<ObjectView *> &views) {
  if (!propf->readSymbols()) {
    error(std::string("invalid propfile: '") + propConfig.optPropeller.str() +
          "'");
    return false;
  }

  processFailureCount = 0;
  llvm::parallel::for_each(
      llvm::parallel::parallel_execution_policy(), views.begin(), views.end(),
      std::bind(&Propeller::processFile, this, std::placeholders::_1));

  if (processFailureCount * 100 / views.size() >= 50)
    warn("propeller failed to parse more than half the objects, "
         "optimization would suffer");

  /* Drop alias cfgs. */
  for (SymbolEntry *funcS : propf->functionsWithAliases) {
    ControlFlowGraph *primaryCfg = nullptr;
    CfgMapTy::iterator primaryCfgMapEntry;
    for (StringRef &aliasName : funcS->aliases) {
      auto cfgMapI = cfgMap.find(aliasName);
      if (cfgMapI == cfgMap.end())
        continue;

      if (cfgMapI->second.empty())
        continue;

      if (!primaryCfg ||
          primaryCfg->nodes.size() < (*cfgMapI->second.begin())->nodes.size()) {
        if (primaryCfg)
          cfgMap.erase(primaryCfgMapEntry);

        primaryCfg = *cfgMapI->second.begin();
        primaryCfgMapEntry = cfgMapI;
      } else
        cfgMap.erase(cfgMapI);
    }
  }

  // Map profiles.
  if (!propf->processProfile())
    return false;

  calculateNodeFreqs();

  dumpCfgs();

  // Releasing all support data (symbol ordinal / name map, saved string refs,
  // etc) before moving to reordering.
  propf.reset(nullptr);
  return true;
}

bool Propeller::dumpCfgs() {
  if (propConfig.optDumpCfgs.empty())
    return true;

  std::set<std::string> cfgToDump(propConfig.optDumpCfgs.begin(),
                                  propConfig.optDumpCfgs.end());
  llvm::SmallString<128> cfgOutputDir(propConfig.optLinkerOutputFile);
  llvm::sys::path::remove_filename(cfgOutputDir);
  for (auto &cfgName : cfgToDump) {
    StringRef cfgNameRef(cfgName);
    if (cfgName == "@" || cfgNameRef.startswith("@@")) {
#ifdef PROPELLER_PROTOBUF
      if (!protobufPrinter.get())
        protobufPrinter.reset(ProtobufPrinter::create(
            Twine(propConfig.optLinkerOutputFile, ".cfg.pb.txt").str()));
      if (cfgNameRef.consume_front("@@")) {
        protobufPrinter->clearCFGGroup();
        const bool cfgNameEmpty = cfgNameRef.empty();
        for (auto &cfgMapEntry : cfgMap)
          for (auto *cfg : cfgMapEntry.second)
            if (cfgNameEmpty || cfg->name == cfgNameRef)
              protobufPrinter->addCFG(*cfg);
        protobufPrinter->printCFGGroup();
        protobufPrinter.reset(nullptr);
      }
#else
      warn("dump to protobuf not supported");
#endif
      continue;
    }
    auto cfgLI = cfgMap.find(cfgName);
    if (cfgLI == cfgMap.end()) {
      warn("could not dump cfg for function '" + cfgName +
           "' : no such function name exists");
      continue;
    }
    int index = 0;
    for (auto *controlFlowGraph : cfgLI->second)
      if (controlFlowGraph->name == cfgName) {
        llvm::SmallString<128> cfgOutput = cfgOutputDir;
        if (++index <= 1)
          llvm::sys::path::append(cfgOutput, (controlFlowGraph->name + ".dot"));
        else
          llvm::sys::path::append(cfgOutput,
                                  (controlFlowGraph->name + "." +
                                   StringRef(std::to_string(index) + ".dot")));
        if (!controlFlowGraph->writeAsDotGraph(StringRef(cfgOutput)))
          warn("failed to dump controlFlowGraph: '" + cfgName + "'");
      }
  }
  return true;
}

ObjectView *Propeller::createObjectView(const StringRef &vn,
                                        const uint32_t ordinal,
                                        const MemoryBufferRef &mbr) {
  const char *start = mbr.getBufferStart();
  if (mbr.getBufferSize() > 6 && start[0] == 0x7f && start[1] == 'E' &&
      start[2] == 'L' && start[3] == 'F') {
    auto r = ObjectFile::createELFObjectFile(mbr);
    if (r)
      return new ObjectView(*r, vn, ordinal, mbr);
  }
  return nullptr;
}

// Generate symbol ordering file according to selected optimization pass and
// feed it to the linker.
std::vector<StringRef> Propeller::genSymbolOrderingFile() {
  int total_objs = 0;
  int hot_objs = 0;
  for (auto &Obj : Views) {
    for (auto &cp : Obj->cfgs) {
      auto &c = *(cp.second);
      if (c.isHot()) {
        ++hot_objs;
        break; // process to next object.
      }
    }
    ++total_objs;
  }

  std::list<StringRef> symbolList(1, "hot");
  const auto hotPlaceHolder = symbolList.begin();
  const auto coldPlaceHolder = symbolList.end();
  propLayout = make<CodeLayout>();
  propLayout->doSplitOrder(symbolList, hotPlaceHolder, coldPlaceHolder);
#ifdef PROPELLER_PROTOBUF
  if (protobufPrinter) {
    protobufPrinter->printCFGGroup();
    protobufPrinter.reset();
  }
#endif

  calculateLegacy(symbolList, hotPlaceHolder, coldPlaceHolder);

  if (!propConfig.optDumpSymbolOrder.empty()) {
    FILE *fp = fopen(propConfig.optDumpSymbolOrder.str().c_str(), "w");
    if (!fp)
      warn(StringRef("dump symbol order: failed to open ") + "'" +
           propConfig.optDumpSymbolOrder + "'");
    else {
      for (auto &sym : symbolList) {
        auto a = sym.split(".bb.");
        if (a.second.empty()) {
          fprintf(fp, "%s\n", sym.str().c_str());
        } else {
          fprintf(fp, "%zu.bb.%s\n", a.first.size(), a.second.str().c_str());
        }
      }
      fclose(fp);
      llvm::outs() << "Dumped symbol order file to: '"
                   << propConfig.optDumpSymbolOrder.str() << "'\n";
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
  StringRef lastFuncName = "";
  for (auto i = std::next(hotPlaceHolder), j = coldPlaceHolder; i != j; ++i) {
    StringRef sName = *i;
    StringRef fName;
    if (SymbolEntry::isBBSymbol(sName, &fName)) {
      if (lastFuncName.empty() || lastFuncName != fName)
        propLeg.bbSymbolsToKeep.insert(sName);
      lastFuncName = fName;
    }
  }
  return;
}

bool Propeller::ObjectViewOrdinalComparator::operator()(
    const ControlFlowGraph *a, const ControlFlowGraph *b) const {
  return a->view->ordinal < b->view->ordinal;
}

PropellerLegacy propLeg;

PropellerConfig propConfig;

} // namespace propeller
} // namespace lld
