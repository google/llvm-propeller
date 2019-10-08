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

#include "Config.h"
#include "InputFiles.h"
#include "PropellerBBReordering.h"
#include "PropellerCfg.h"
#include "PropellerFuncOrdering.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <list>
#include <map>
#include <numeric> // For std::accumulate.
#include <tuple>
#include <vector>

using lld::elf::config;

namespace lld {
namespace propeller {

Propeller::Propeller(lld::elf::SymbolTable *ST) : Symtab(ST), Propf(nullptr) {}

Propeller::~Propeller() {}

// Read the "@" directives in the propeller file, compare it against "-o"
// filename, return true "-o" file name equals to one of the "@" directives.
bool Propfile::matchesOutputFileName(const StringRef outputFileName) {
  int outputFileTagSeen = 0;
  std::string line;
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
  while (std::getline(PropfStream, line).good()) {
    ++LineNo;
    if (line.empty())
      continue;
    if (line[0] == '#' || line[0] == '!' || line[0] == '@')
      continue;
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
                               symSize);
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
    createBasicBlockSymbol(symOrdinal, existingI->second.get(), bbIndex,
                           symSize);
  }
  return true;
}

// Helper method to parse a branch or fallthrough record like below
//   10 12 232590 R
static bool parseBranchOrFallthroughLine(StringRef lineRef,
                                         uint64_t *fromNodeIdx,
                                         uint64_t *toNodeIdx, uint64_t *count,
                                         char *type) {
  auto getInt = [](const StringRef &S) -> uint64_t {
    uint64_t r;
    if (S.getAsInteger(10, r) /* string contains more than numbers */
        || r == 0)
      return 0;
    return r;
  };
  auto s0 = lineRef.split(' ');
  *fromNodeIdx = getInt(s0.first);
  auto s1 = s0.second.split(' ');
  *toNodeIdx = getInt(s1.first);
  auto s2 = s1.second.split(' ');
  *count = getInt(s2.first);
  if (!*fromNodeIdx || !*toNodeIdx || !*count)
    return false;
  if (!s2.second.empty())
    if (s2.second == "C" || s2.second == "R")
      *type = s2.second[0];
    else
      return false;
  else
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
    if (!parseBranchOrFallthroughLine(L, &from, &to, &count, &tag)) {
      reportParseError("unrecognized line:\n" + L.str());
      return false;
    }
    CFGNode *fromN = Prop.findCfgNode(from);
    CFGNode *toN = Prop.findCfgNode(to);
    if (!fromN || !toN)
      continue;

    if (LineTag == 'B') {
      ++branchCnt;
      if (fromN->CFG == toN->CFG)
        fromN->CFG->mapBranch(fromN, toN, count, tag == 'C', tag == 'R');
      else
        fromN->CFG->mapCallOut(fromN, toN, 0, count, tag == 'C', tag == 'R');
    } else {
      ++fallthroughCnt;
      // LineTag == 'F'
      if (fromN->CFG == toN->CFG)
        fromN->CFG->markPath(fromN, toN, count);
    }
  }

  if (!branchCnt)
    warn("propeller processed 0 branch info");
  if (!fallthroughCnt)
    warn("ropeller processed 0 fallthrough info");
  return true;
}

// Parse each ELF file, create CFG and attach profile data to CFG.
void Propeller::processFile(const std::pair<elf::InputFile *, uint32_t> &pair) {
  auto *inf = pair.first;
  ObjectView *View = ObjectView::create(inf->getName(), pair.second, inf->mb);
  if (View) {
    if (CFGBuilder(*this, View).buildCFGs()) {
      // Updating global data structure.
      std::lock_guard<std::mutex> lock(Lock);
      Views.emplace_back(View);
      for (std::pair<const StringRef, std::unique_ptr<ControlFlowGraph>> &P :
           View->CFGs) {
        auto result = CFGMap[P.first].emplace(P.second.get());
        (void)(result);
        assert(result.second);
      }
    } else {
      warn(Twine("skipped building CFG for '" + inf->getName()));
      ++ProcessFailureCount;
    }
  }
}

CFGNode *Propeller::findCfgNode(uint64_t symbolOrdinal) {
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
  for (auto &cfgP : CFGMap) {
    auto &cfg = *cfgP.second.begin();
    if (cfg->Nodes.empty())
      continue;
    bool Hot = false;
    cfg->forEachNodeRef([&Hot, &sumEdgeWeights](CFGNode &node) {
      uint64_t maxCallOut =
          node.CallOuts.empty()
              ? 0
              : (*std::max_element(node.CallOuts.begin(), node.CallOuts.end(),
                                   [](const CFGEdge *E1, const CFGEdge *E2) {
                                     return E1->Weight < E2->Weight;
                                   }))
                    ->Weight;
      node.Freq = std::max({sumEdgeWeights(node.Outs), sumEdgeWeights(node.Ins),
                            sumEdgeWeights(node.CallIns), maxCallOut});

      Hot |= (node.Freq != 0);
    });
    if (Hot && cfg->getEntryNode()->Freq == 0)
      cfg->getEntryNode()->Freq = 1;
  }
}

// Returns true if linker output target matches propeller profile.
bool Propeller::checkTarget() {
  if (config->propeller.empty())
    return false;
  std::string propellerFileName = config->propeller.str();
  // Propfile takes ownership of FPtr.
  Propf.reset(new Propfile(*this, propellerFileName));
  Propf->PropfStream.open(Propf->PropfName);
  if (!Propf->PropfStream.good()) {
    error(std::string("[Propeller]: Failed to open '") + propellerFileName +
          "'.");
    return false;
  }
  return Propf->matchesOutputFileName(
      llvm::sys::path::filename(config->outputFile));
}

// Entrance of Propeller framework. This processes each elf input file in
// parallel and stores the result information.
bool Propeller::processFiles(std::vector<lld::elf::InputFile *> &files) {
  if (!Propf->readSymbols()) {
    error(std::string("[Propeller]: Invalid propfile: '") +
          config->propeller.str() + "'.");
    return false;
  }

  // Creating CFGs.
  std::vector<std::pair<elf::InputFile *, uint32_t>> fileOrdinalPairs;
  int ordinal = 0;
  for (auto &F : files)
    fileOrdinalPairs.emplace_back(F, ++ordinal);

  ProcessFailureCount = 0;
  llvm::parallel::for_each(
      llvm::parallel::parallel_execution_policy(), fileOrdinalPairs.begin(),
      fileOrdinalPairs.end(),
      std::bind(&Propeller::processFile, this, std::placeholders::_1));

  if (ProcessFailureCount * 100 / files.size() >= 50)
    warn("[Propeller]: propeller failed to parse more than half the objects, "
         "optimization would suffer.");

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

  if (!config->propellerDumpCfgs.empty()) {
    llvm::SmallString<128> cfgOutputDir(config->outputFile);
    llvm::sys::path::remove_filename(cfgOutputDir);
    for (auto &cfgNameToDump : config->propellerDumpCfgs) {
      auto cfgLI = CFGMap.find(cfgNameToDump);
      if (cfgLI == CFGMap.end()) {
        warn("[Propeller] Could not dump cfg for function '" + cfgNameToDump +
             "' : No such function name exists.");
        continue;
      }
      int Index = 0;
      for (auto *CFG : cfgLI->second)
        if (CFG->Name == cfgNameToDump) {
          llvm::SmallString<128> cfgOutput = cfgOutputDir;
          if (++Index <= 1)
            llvm::sys::path::append(cfgOutput, (CFG->Name + ".dot"));
          else
            llvm::sys::path::append(
                cfgOutput,
                (CFG->Name + "." + StringRef(std::to_string(Index) + ".dot")));
          if (!CFG->writeAsDotGraph(StringRef(cfgOutput)))
            warn("failed to dump CFG: '" + cfgNameToDump + "'");
        }
    }
  }

  // Releasing all support data (symbol ordinal / name map, saved string refs,
  // etc) before moving to reordering.
  Propf.reset(nullptr);
  return true;
}

// Generate symbol ordering file according to selected optimization pass and
// feed it to the linker.
std::vector<StringRef> Propeller::genSymbolOrderingFile() {
  calculateNodeFreqs();

  std::list<ControlFlowGraph *> cfgOrder;
  if (config->propellerReorderFuncs) {
    CallChainClustering c3;
    c3.init(*this);
    auto cfgsReordered = c3.doOrder(cfgOrder);
    (void)cfgsReordered;
  } else {
    forEachCfgRef(
        [&cfgOrder](ControlFlowGraph &cfg) { cfgOrder.push_back(&cfg); });
    cfgOrder.sort([](const ControlFlowGraph *a, const ControlFlowGraph *b) {
      const auto *aEntry = a->getEntryNode();
      const auto *bEntry = b->getEntryNode();
      return aEntry->MappedAddr < bEntry->MappedAddr;
    });
  }

  std::list<StringRef> symbolList(1, "Hot");
  const auto hotPlaceHolder = symbolList.begin();
  const auto coldPlaceHolder = symbolList.end();
  unsigned reorderedN = 0;
  for (auto *cfg : cfgOrder) {
    if (cfg->isHot() && config->propellerReorderBlocks) {
      NodeChainBuilder(cfg).doSplitOrder(
          symbolList, hotPlaceHolder,
          config->propellerSplitFuncs ? coldPlaceHolder : hotPlaceHolder);
      reorderedN++;
    } else {
      auto PlaceHolder =
          config->propellerSplitFuncs ? coldPlaceHolder : hotPlaceHolder;
      cfg->forEachNodeRef([&symbolList, PlaceHolder](CFGNode &N) {
        symbolList.insert(PlaceHolder, N.ShName);
      });
    }
  }

  calculateLegacy(symbolList, hotPlaceHolder, coldPlaceHolder);

  if (!config->propellerDumpSymbolOrder.empty()) {
    FILE *fp = fopen(config->propellerDumpSymbolOrder.str().c_str(), "w");
    if (!fp)
      warn(StringRef("[Propeller] Dump symbol order: failed to open ") + "'" +
           config->propellerDumpSymbolOrder + "'");
    else {
      for (auto &sym : symbolList)
        fprintf(fp, "%s\n", sym.str().c_str());
      fclose(fp);
      llvm::outs() << "[Propeller] Dumped symbol order file to: '"
                   << config->propellerDumpSymbolOrder.str() << "'.\n";
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

} // namespace propeller
} // namespace lld
