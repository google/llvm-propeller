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
#include "PropellerBBReordering.h"
#include "PropellerELFCfg.h"
#include "PropellerFuncOrdering.h"
#include "InputFiles.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <numeric>
#include <tuple>
#include <vector>

using lld::elf::config;

namespace lld {
namespace propeller {

// Read the "@" directive in the propeller file, compare it against "-o"
// filename, return true if positive.
bool Propfile::matchesOutputFileName(const StringRef &outputFileName) {
  ssize_t r;
  int outputFileTagSeen = 0;
  while ((r = getline(&LineBuf, &LineSize, PStream)) != -1) {
    if (r == 0) continue;
    if (LineBuf[r - 1] == '\n') {
      LineBuf[--r] = '\0'; // Drop '\n' character at the end.
    }
    if (LineBuf[0] != '@') break;
    ++outputFileTagSeen;
    if (StringRef(LineBuf + 1) == outputFileName)
      return true;
  }
  if (outputFileTagSeen)
    return false;
  // If no @outputFileName is specified, reset the stream and assume linker
  // shall proceed propellering.
  fseek(PStream, 0, SEEK_SET);
  return true;
}

SymbolEntry *Propfile::findSymbol(StringRef symName) {
  std::pair<StringRef, StringRef> symNameSplit = symName.split(".llvm.");
  StringRef funcName;
  StringRef bbIndex;
  string tmpStr;
  if (!SymbolEntry::isBBSymbol(symNameSplit.first, &funcName, &bbIndex)) {
    funcName = symNameSplit.first;
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

// Refer header file for detailed information about symbols section.
bool Propfile::readSymbols() {
  ssize_t R;
  LineNo = 0;
  LineTag = '\0';
  // A std::list of bbsymbols<ordinal, function_ordinal, bbindex and size> that
  // appears before its wrapping function. This should be rather rare.
  std::list<std::tuple<uint64_t, uint64_t, StringRef, uint64_t>> bbSymbols;
  while ((R = getline(&LineBuf, &LineSize, PStream)) != -1) {
    ++LineNo;
    if (R == 0) continue;
    if (LineBuf[0] == '#' || LineBuf[0] == '!' || LineBuf[0] == '@')
      continue;
    if (LineBuf[0] == 'B' || LineBuf[0] == 'F') {
      LineTag = LineBuf[0];
      break; // Done symbol section.
    }
    if (LineBuf[0] == 'S') {
      LineTag = LineBuf[0];
      continue;
    }
    if (LineBuf[R - 1] == '\n')
      LineBuf[--R] = '\0'; // Drop '\n' character at the end.
    StringRef lineStrRef(LineBuf); // LineBuf is null-terminated.

    uint64_t SOrdinal;
    uint64_t SSize;
    auto l1S = lineStrRef.split(' ');
    auto l1 = l1S.first;
    auto l2S = l1S.second.split(' ');
    auto l2 = l2S.first;
    auto ephemeralStr = l2S.second;
    if (l1.getAsInteger(10, SOrdinal) /* means error happens */ ||
        SOrdinal == 0) {
      error("[Propeller]: Invalid ordinal field, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    if (l2.getAsInteger(16, SSize)) {
      error("[Propeller]: Invalid size field, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    if (ephemeralStr.empty()) {
      error("[Propeller]: Invalid name field, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    if (ephemeralStr[0] == 'N') { // Function symbol?
      // Save ephemeralStr for persistency across Propeller lifecycle.
      StringRef savedNameStr = PropfileStrSaver.save(ephemeralStr.substr(1));
      SymbolEntry::AliasesTy sAliases;
      savedNameStr.split(sAliases, '/');
      for(auto& sAlias: sAliases){
        sAlias = sAlias.split(".llvm.").first;
      }
      StringRef sName = sAliases[0];
      assert(SymbolOrdinalMap.find(SOrdinal) == SymbolOrdinalMap.end());
      createFunctionSymbol(SOrdinal, sName, std::move(sAliases), SSize);
    } else {
      // It's a bb symbol.
      auto lineStrRef = ephemeralStr.split('.');
      uint64_t funcIndex;
      if (lineStrRef.first.getAsInteger(10, funcIndex) || funcIndex == 0) {
        error("[Propeller]: Invalid function index field, at propfile line: " +
            std::to_string(LineNo) + ".");
        return false;
      }
      // Only save the index part, which is highly reusable. Note
      // PropfileStrSaver is a UniqueStringSaver.
      StringRef bbIndex = PropfileStrSaver.save(lineStrRef.second);
      auto existingI = SymbolOrdinalMap.find(funcIndex);
      if (existingI != SymbolOrdinalMap.end()) {
        if (existingI->second->BBTag) {
          error(
              string("[Propeller]: Index '") + std::to_string(funcIndex) +
              "' is not a function index, but a bb index, at propfile line: " +
              std::to_string(LineNo) + ".");
          return false;
        }
        createBasicBlockSymbol(SOrdinal, existingI->second.get(), bbIndex,
                               SSize);
      } else
        // A bb symbol appears earlier than its wrapping function, rare, but
        // not impossible, rather play it safely.
        bbSymbols.emplace_back(SOrdinal, funcIndex, bbIndex, SSize);
    }
  } // End of iterating all symbols.

  for (std::tuple<uint64_t, uint64_t, StringRef, uint64_t> &sym : bbSymbols) {
    uint64_t sOrdinal;
    uint64_t funcIndex;
    uint64_t sSize;
    StringRef bbIndex;
    std::tie(sOrdinal, funcIndex, bbIndex, sSize) = sym;
    auto existingI = SymbolOrdinalMap.find(funcIndex);
    if (existingI == SymbolOrdinalMap.end()) {
      error("[Propeller]: Function with index number '" +
            std::to_string(funcIndex) + "' does not exist, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    createBasicBlockSymbol(sOrdinal, existingI->second.get(), bbIndex, sSize);
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
  ssize_t r;
  uint64_t branchCnt = 0;
  uint64_t fallthroughCnt = 0;
  while ((r = getline(&LineBuf, &LineSize, PStream)) != -1) {
    ++LineNo;
    if (r == 0)
      continue;
    if (LineBuf[0] == '#' || LineBuf[0] == '!')
      continue;
    if (LineBuf[0] == 'S' || LineBuf[0] == 'B' || LineBuf[0] == 'F') {
      LineTag = LineBuf[0];
      continue;
    }
    if (LineTag != 'B' && LineTag != 'F') break;
    if (LineBuf[r - 1] == '\n')
      LineBuf[--r] = '\0'; // drop '\n' character at the end;

    StringRef L(LineBuf); // LineBuf is null-terminated.
    uint64_t from, to, count;
    char tag;
    if (!parseBranchOrFallthroughLine(L, &from, &to, &count, &tag)) {
      error(string("[Propeller]: Unrecognized propfile line: ") +
            std::to_string(LineNo) + ":\n" + L.str());
      return false;
    }
    ELFCFGNode *fromN = Prop.findCfgNode(from);
    ELFCFGNode *toN = Prop.findCfgNode(to);
    if (!fromN || !toN) continue;

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
    warn("[Propeller]: Zero branch info processed.");
  if (!fallthroughCnt)
    warn("[Propeller]: Zero fallthrough info processed.");
  return true;
}

// Parse each ELF file, create CFG and attach profile data to CFG.
void Propeller::processFile(const std::pair<elf::InputFile *, uint32_t> &pair) {
  auto *inf = pair.first;
  ELFView *View = ELFView::create(inf->getName(), pair.second, inf->mb);
  if (View) {
    ELFCFGBuilder(*this, View).buildCFGs();
    {
      // Updating global data structure.
      std::lock_guard<std::mutex> lock(this->Lock);
      this->Views.emplace_back(View);
      for (auto &P : View->CFGs) {
        std::pair<StringRef, StringRef> SplitName = P.first.split(".llvm.");
        auto result = CFGMap[SplitName.first].emplace(P.second.get());
        (void)(result);
        assert(result.second);
      }
    }
  }
}

ELFCFGNode *Propeller::findCfgNode(uint64_t symbolOrdinal) {
  assert(Propf->SymbolOrdinalMap.find(symbolOrdinal) !=
         Propf->SymbolOrdinalMap.end());
  SymbolEntry *symbol = Propf->SymbolOrdinalMap[symbolOrdinal].get();
  if (!symbol) {
    // This is an internal error, should not happen.
    error(string("[Propeller]: Invalid symbol ordinal: " +
                 std::to_string(symbolOrdinal)));
    return nullptr;
  }
  SymbolEntry *funcSym = symbol->BBTag ? symbol->ContainingFunc : symbol;
  for (auto& funcAliasName : funcSym->Aliases){
    auto cfgLI = CFGMap.find(funcAliasName);
    if (cfgLI == CFGMap.end())
      continue;

    // Objects (CfgLI->second) are sorted in the way they appear on the command
    // line, which is the same as how linker chooses the weak symbol definition.
    if (!symbol->BBTag) {
      for (auto *CFG : cfgLI->second)
        // Check CFG does have name "SymName".
        for (auto &node : CFG->Nodes)
          if (node->ShName.split(".llvm.").first == funcAliasName)
            return node.get();
    } else {
      uint32_t NumOnes;
      // Compare the number of "a" in aaa...a.BB.funcname against integer
      // NumOnes.
      if (symbol->Name.getAsInteger(10, NumOnes) || !NumOnes)
        warn("Internal error, BB name is invalid: '" + symbol->Name.str() +
             "'.");
      else
        for (auto *CFG : cfgLI->second)
          for (auto &node : CFG->Nodes) {
            // Check CFG does have name "SymName".
            auto t = node->ShName.find_first_of('.');
            if (t != string::npos && t == NumOnes) return node.get();
          }
    }
  }
  return nullptr;
}

void Propeller::calculateNodeFreqs() {
  auto sumEdgeWeights = [](std::vector<ELFCFGEdge *> &edges) -> uint64_t {
    return std::accumulate(edges.begin(), edges.end(), 0,
                           [](uint64_t pSum, const ELFCFGEdge *edge) {
                             return pSum + edge->Weight;
                           });
  };
  for (auto &cfgP : CFGMap) {
    auto &cfg = *cfgP.second.begin();
    if (cfg->Nodes.empty())
      continue;
    bool Hot = false;
    cfg->forEachNodeRef([&Hot, &sumEdgeWeights](ELFCFGNode &node) {
      uint64_t maxCallOut =
          node.CallOuts.empty() ?
          0 :
          (*std::max_element(node.CallOuts.begin(), node.CallOuts.end(),
                          [](const ELFCFGEdge *E1, const ELFCFGEdge *E2){
                            return E1->Weight < E2->Weight;
                          }))->Weight;
      node.Freq =
          std::max({sumEdgeWeights(node.Outs), sumEdgeWeights(node.Ins),
                    sumEdgeWeights(node.CallIns), maxCallOut});

      Hot |= (node.Freq != 0);
    });
    if (Hot && cfg->getEntryNode()->Freq == 0)
      cfg->getEntryNode()->Freq = 1;
  }
}

// Returns true if linker output target matches propeller profile.
bool Propeller::checkPropellerTarget() {
  if (config->propeller.empty()) return false;
  string propellerFileName = config->propeller.str();
  FILE *fPtr = fopen(propellerFileName.c_str(), "r");
  if (!fPtr) {
    error(string("[Propeller]: Failed to open '") + propellerFileName + "'.");
    return false;
  }
  // Propfile takes ownership of FPtr.
  Propf.reset(new Propfile(fPtr, *this));

  return Propf->matchesOutputFileName(
      llvm::sys::path::filename(config->outputFile));
}

// Entrance of Propeller framework. This processes each elf input file in
// parallel and stores the result information.
bool Propeller::processFiles(std::vector<lld::elf::InputFile *> &files) {
  if (!Propf->readSymbols()) {
    error(string("[Propeller]: Invalid propfile: '") + config->propeller.str() +
          "'.");
    return false;
  }

  // Creating CFGs.
  std::vector<std::pair<elf::InputFile *, uint32_t>> fileOrdinalPairs;
  int ordinal = 0;
  for (auto &F : files)
    fileOrdinalPairs.emplace_back(F, ++ordinal);

  llvm::parallel::for_each(
      llvm::parallel::parallel_execution_policy(), fileOrdinalPairs.begin(),
      fileOrdinalPairs.end(),
      std::bind(&Propeller::processFile, this, std::placeholders::_1));

  /* Drop alias cfgs. */
  for(SymbolEntry *funcS : Propf->FunctionsWithAliases){

    ELFCFG * primaryCfg = nullptr;
    CfgMapTy::iterator primaryCfgMapEntry;

    for(StringRef& AliasName : funcS->Aliases){
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
          if (!CFG->writeAsDotGraph(cfgOutput.c_str()))
            warn("[Propeller] Failed to dump CFG: '" + cfgNameToDump + "'.");
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

  std::list<ELFCFG *> cfgOrder;
  if (config->propellerReorderFuncs) {
    CallChainClustering c3;
    c3.init(*this);
    auto cfgsReordered = c3.doOrder(cfgOrder);
    (void)cfgsReordered;
  } else {
    forEachCfgRef([&cfgOrder](ELFCFG &cfg) { cfgOrder.push_back(&cfg); });
    cfgOrder.sort([](const ELFCFG *a, const ELFCFG *b) {
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
      cfg->forEachNodeRef([&symbolList, PlaceHolder](ELFCFGNode &N) {
        symbolList.insert(PlaceHolder, N.ShName);
      });
    }
  }

  calculatePropellerLegacy(symbolList, hotPlaceHolder, coldPlaceHolder);

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
void Propeller::calculatePropellerLegacy(
    std::list<StringRef> &symList, std::list<StringRef>::iterator hotPlaceHolder,
    std::list<StringRef>::iterator coldPlaceHolder) {
  // No function split or no cold symbols, all bb symbols shall be removed.
  if (hotPlaceHolder == coldPlaceHolder) return ;
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

void Propeller::ELFViewDeleter::operator()(ELFView *v) {
  delete v;
}

bool Propeller::ELFViewOrdinalComparator::operator()(const ELFCFG *a,
                                                     const ELFCFG *b) const {
  return a->View->Ordinal < b->View->Ordinal;
}

PropellerLegacy PropLeg;

} // namespace propeller
} // namespace lld
