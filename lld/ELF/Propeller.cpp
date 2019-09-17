#include "Propeller.h"

#include "Config.h"
#include "PropellerBBReordering.h"
#include "PropellerELFCfg.h"
#include "PropellerFuncOrdering.h"
#include "InputFiles.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <stdio.h>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <numeric>
#include <tuple>
#include <vector>

using lld::elf::config;
using llvm::StringRef;

using std::list;
using std::map;
using std::string;
using std::tuple;
using std::vector;
using std::chrono::duration;
using std::chrono::system_clock;

namespace lld {
namespace propeller {

SymbolEntry *Propfile::findSymbol(StringRef SymName) {
  std::pair<StringRef, StringRef> SymNameSplit = SymName.split(".llvm.");
  StringRef FuncName;
  StringRef BBIndex;
  string TmpStr;
  if (!SymbolEntry::isBBSymbol(SymNameSplit.first, &FuncName, &BBIndex)) {
    FuncName = SymNameSplit.first;
    BBIndex = "";
  } else {
    // When SymName is like "11111.bb.foo", set BBIndex to "5".
    // "1111" -> "4".
    TmpStr = std::to_string(BBIndex.size());
    BBIndex = StringRef(TmpStr);
  }
  auto L1 = SymbolNameMap.find(FuncName);
  if (L1 != SymbolNameMap.end()) {
    auto L2 = L1->second.find(BBIndex);
    if (L2 != L1->second.end()) {
      return L2->second;
    }
  }
  // warn(string("failed to find SymbolEntry for '") +
  //     SymbolEntry::toCompactBBName(SymName) + "'.");
  return nullptr;
}

bool Propfile::readSymbols() {
  ssize_t R;
  LineNo = 0;
  LineTag = '\0';
  list<tuple<uint64_t, uint64_t, StringRef, uint64_t>> BBSymbols;
  while ((R = getline(&LineBuf, &LineSize, PStream)) != -1) {
    ++LineNo;
    if (R == 0)
      continue;
    if (LineBuf[0] == '#' || LineBuf[0] == '!')
      continue;
    if (LineBuf[0] == 'B' || LineBuf[0] == 'F') {
      LineTag = LineBuf[0];
      break; // Done symbol section.
    }
    if (LineBuf[0] == 'S') {
      LineTag = LineBuf[0];
      continue;
    }
    if (LineBuf[R - 1] == '\n') {
      LineBuf[--R] = '\0'; // drop '\n' character at the end;
    }
    StringRef L(LineBuf); // LineBuf is null-terminated.

    uint64_t SOrdinal;
    uint64_t SSize;
    auto L1S = L.split(' ');
    auto L1 = L1S.first;
    auto L2S = L1S.second.split(' ');
    auto L2 = L2S.first;
    auto EphemeralStr = L2S.second;
    if (L1.getAsInteger(10, SOrdinal) == true /* means error happens */ ||
        SOrdinal == 0) {
      error("[Propeller]: Invalid ordinal field, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    if (L2.getAsInteger(16, SSize) == true) {
      error("[Propeller]: Invalid size field, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    if (EphemeralStr.empty()) {
      error("[Propeller]: Invalid name field, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    if (EphemeralStr[0] == 'N') { // Function symbol?
      // Save EphemeralStr for persistency across Propeller lifecycle.
      StringRef SavedNameStr = PropfileStrSaver.save(EphemeralStr.substr(1));
      SymbolEntry::AliasesTy SAliases;
      SavedNameStr.split(SAliases, '/');
      for(auto& SAlias: SAliases){
        SAlias = SAlias.split(".llvm.").first;
      }
      StringRef SName = SAliases[0];
      //SAliases.erase(SAliases.begin());
      assert(SymbolOrdinalMap.find(SOrdinal) == SymbolOrdinalMap.end());
      createFunctionSymbol(SOrdinal, SName, std::move(SAliases), SSize);
    } else {
      // It's a bb symbol.
      auto L = EphemeralStr.split('.');
      uint64_t FuncIndex;
      if (L.first.getAsInteger(10, FuncIndex) == true || FuncIndex == 0) {
        error("[Propeller]: Invalid function index field, at propfile line: " +
            std::to_string(LineNo) + ".");
        return false;
      }
      // Only save the index part, which is highly reusable. Note
      // PropfileStrSaver is a UniqueStringSaver.
      StringRef BBIndex = PropfileStrSaver.save(L.second);
      auto ExistingI = SymbolOrdinalMap.find(FuncIndex);
      if (ExistingI != SymbolOrdinalMap.end()) {
        if (ExistingI->second->BBTag) {
          error(
              string("[Propeller]: Index '") + std::to_string(FuncIndex) +
              "' is not a function index, but a bb index, at propfile line: " +
              std::to_string(LineNo) + ".");
          return false;
        }
        createBasicBlockSymbol(SOrdinal, ExistingI->second.get(), BBIndex,
                               SSize);
      } else {
        // A bb symbol appears earlier than its wrapping function, rare, but
        // not impossible, rather play it safely.
        BBSymbols.emplace_back(SOrdinal, FuncIndex, BBIndex, SSize);
      }
    }
  } // End of iterating all symbols.

  for (auto &S : BBSymbols) {
    uint64_t SOrdinal;
    uint64_t FuncIndex;
    uint64_t SSize;
    StringRef BBIndex;
    std::tie(SOrdinal, FuncIndex, BBIndex, SSize) = S;
    auto ExistingI = SymbolOrdinalMap.find(FuncIndex);
    if (ExistingI == SymbolOrdinalMap.end()) {
      error("[Propeller]: Function with index number '" +
            std::to_string(FuncIndex) + "' does not exist, at propfile line: " +
            std::to_string(LineNo) + ".");
      return false;
    }
    createBasicBlockSymbol(SOrdinal, ExistingI->second.get(), BBIndex, SSize);
  }
  return true;
}

// Parse a branch record like below
//   10 12 232590 R
static bool parseBranchOrFallthroughLine(StringRef L, uint64_t *From,
                                         uint64_t *To, uint64_t *Cnt, char *T) {
  auto getInt = [](const StringRef &S) -> uint64_t {
    uint64_t R;
    if (S.getAsInteger(10, R) /* string contains more than numbers */
        || R == 0)
      return 0;
    return R;
  };
  auto S0 = L.split(' ');
  *From = getInt(S0.first);
  auto S1 = S0.second.split(' ');
  *To = getInt(S1.first);
  auto S2 = S1.second.split(' ');
  *Cnt = getInt(S2.first);
  if (!*From || !*To || !*Cnt)
    return false;
  if (!S2.second.empty()) {
    if (S2.second == "C" || S2.second == "R")
      *T = S2.second[0];
    else
      return false;
  } else {
    *T = '\0';
  }
  return true;
}

bool Propfile::processProfile() {
  ssize_t R;
  uint64_t BCnt = 0;
  uint64_t FCnt = 0;
  while ((R = getline(&LineBuf, &LineSize, PStream)) != -1) {
    ++LineNo;
    if (R == 0)
      continue;
    if (LineBuf[0] == '#' || LineBuf[0] == '!')
      continue;
    if (LineBuf[0] == 'S' || LineBuf[0] == 'B' || LineBuf[0] == 'F') {
      LineTag = LineBuf[0];
      continue;
    }
    if (LineTag != 'B' && LineTag != 'F') break;
    if (LineBuf[R - 1] == '\n') {
      LineBuf[--R] = '\0'; // drop '\n' character at the end;
    }
    StringRef L(LineBuf); // LineBuf is null-terminated.
    uint64_t From, To, Cnt;
    char Tag;
    if (!parseBranchOrFallthroughLine(L, &From, &To, &Cnt, &Tag)) {
      error(string("[Propeller]: Unrecognized propfile line: ") +
            std::to_string(LineNo) + ":\n" + L.str());
      return false;
    }
    ELFCfgNode *FromN = Prop.findCfgNode(From);
    ELFCfgNode *ToN = Prop.findCfgNode(To);
    if (!FromN || !ToN) {
      continue;
    }

    if (LineTag == 'B') {
      ++BCnt;
      if (FromN->Cfg == ToN->Cfg) {
        FromN->Cfg->mapBranch(FromN, ToN, Cnt, Tag == 'C', Tag == 'R');
      } else {
        FromN->Cfg->mapCallOut(FromN, ToN, 0, Cnt, Tag == 'C', Tag == 'R');
      }
    } else {
      ++FCnt;
      // LineTag == 'F'
      if (FromN->Cfg == ToN->Cfg)
        FromN->Cfg->markPath(FromN, ToN, Cnt);
    }
  }

  if (!BCnt) {
    warn("[Propeller]: Zero branch info processed.");
  }
  if (!FCnt) {
    warn("[Propeller]: Zero fallthrough info processed.");
  }
  return true;
}

void Propeller::processFile(const pair<elf::InputFile *, uint32_t> &Pair) {
  auto *Inf = Pair.first;
  ELFView *View = ELFView::create(Inf->getName(), Pair.second, Inf->mb);
  if (View) {
    ELFCfgBuilder(*this, View).buildCfgs();
    {
      // Updating global data structure.
      std::lock_guard<mutex> L(this->Lock);
      this->Views.emplace_back(View);
      for (auto &P : View->Cfgs) {
        std::pair<StringRef, StringRef> SplitName = P.first.split(".llvm.");
        auto R = CfgMap[SplitName.first].emplace(P.second.get());
        (void)(R);
        assert(R.second);
      }
    }
  }
}

ELFCfgNode *Propeller::findCfgNode(uint64_t SymbolOrdinal) {
  assert(Propf->SymbolOrdinalMap.find(SymbolOrdinal) !=
         Propf->SymbolOrdinalMap.end());
  SymbolEntry *S = Propf->SymbolOrdinalMap[SymbolOrdinal].get();
  if (!S) {
    // This is an internal error, should not happen.
    error(string("[Propeller]: Invalid symbol ordinal: " +
                 std::to_string(SymbolOrdinal)));
    return nullptr;
  }
  SymbolEntry *Func = S->BBTag ? S->ContainingFunc : S;
  for (auto& FuncAliasName : Func->Aliases){
    auto CfgLI = CfgMap.find(FuncAliasName);
    if (CfgLI == CfgMap.end())
      continue;

    // There might be multiple object files that define SymName.
    // So for "funcFoo.bb.3", we return Obj2.
    // For "funcFoo.bb.1", we return Obj1 (the first matching obj).
    // Obj1:
    //    Cfg1: funcFoo
    //          funcFoo.bb.1
    //          funcFoo.bb.2
    // Obj2:
    //    Cfg1: funcFoo
    //          funcFoo.bb.1
    //          funcFoo.bb.2
    //          funcFoo.bb.3
    // Also note, Objects (CfgLI->second) are sorted in the way
    // they appear on the command line, which is the same as how
    // linker chooses the weak symbol definition.
    if (!S->BBTag) {
      for (auto *Cfg : CfgLI->second) {
        // Check Cfg does have name "SymName".
        for (auto &N : Cfg->Nodes) {
          if (N->ShName.split(".llvm.").first == FuncAliasName) {
            return N.get();
          }
        }
      }
    } else {
      uint32_t NumOnes;
      // Compare the number of "a" in aaa...a.BB.funcname against integer
      // NumOnes.
      if (S->Name.getAsInteger(10, NumOnes) == true || !NumOnes) {
        warn("Internal error, BB name is invalid: '" + S->Name.str() + "'.");
      } else {
        for (auto *Cfg : CfgLI->second) {
          // Check Cfg does have name "SymName".
          for (auto &N : Cfg->Nodes) {
            auto T = N->ShName.find_first_of('.');
            if (T != string::npos && T == NumOnes) {
              return N.get();
            }
          }
        }
      }
    }
  }
  return nullptr;
}

void Propeller::calculateNodeFreqs() {
  auto sumEdgeWeights = [](list<ELFCfgEdge *> &Edges) -> uint64_t {
    return std::accumulate(Edges.begin(), Edges.end(), 0,
                           [](uint64_t PSum, const ELFCfgEdge *Edge) {
                             return PSum + Edge->Weight;
                           });
  };
  for (auto &P : CfgMap) {
    auto &Cfg = *P.second.begin();
    if (Cfg->Nodes.empty())
      continue;
    bool Hot = false;
    Cfg->forEachNodeRef([&Hot, &sumEdgeWeights](ELFCfgNode &Node) {
      uint64_t MaxCallOut =
          Node.CallOuts.empty() ?
          0 :
          (*std::max_element(Node.CallOuts.begin(), Node.CallOuts.end(),
                          [](const ELFCfgEdge *E1, const ELFCfgEdge *E2){
                            return E1->Weight < E2->Weight;
                          }))->Weight;
      Node.Freq =
          std::max({sumEdgeWeights(Node.Outs), sumEdgeWeights(Node.Ins),
                    sumEdgeWeights(Node.CallIns), MaxCallOut});

      Hot |= (Node.Freq != 0);
    });
    if (Hot && Cfg->getEntryNode()->Freq == 0)
      Cfg->getEntryNode()->Freq = 1;
  }
}

bool Propeller::processFiles(std::vector<lld::elf::InputFile *> &Files) {
  if (config->propeller.empty()) return true;
  string PropellerFileName = config->propeller.str();
  FILE *PropFile = fopen(PropellerFileName.c_str(), "r");
  if (!PropFile) {
    error(string("[Propeller]: Failed to open '") + PropellerFileName + "'.");
    return false;
  }
  Propf.reset(new Propfile(PropFile, *this));
  auto startReadSymbolTime = system_clock::now();
  if (!Propf->readSymbols()) {
    error(string("[Propeller]: Invalid propfile: '") + PropellerFileName +
          "'.");
    return false;
  }
  auto startCreateCfgTime = system_clock::now();

  // Creating Cfgs.
  vector<pair<elf::InputFile *, uint32_t>> FileOrdinalPairs;
  int Ordinal = 0;
  for (auto &F : Files) {
    FileOrdinalPairs.emplace_back(F, ++Ordinal);
  }
  llvm::parallel::for_each(
      llvm::parallel::parallel_execution_policy(), FileOrdinalPairs.begin(),
      FileOrdinalPairs.end(),
      std::bind(&Propeller::processFile, this, std::placeholders::_1));

  /* Drop alias cfgs. */
  for(SymbolEntry * F : Propf->FunctionsWithAliases){

    ELFCfg * PrimaryCfg = nullptr;
    CfgMapTy::iterator PrimaryCfgMapEntry;

    for(StringRef& AliasName : F->Aliases){
      auto L = CfgMap.find(AliasName);
      if (L == CfgMap.end())
        continue;

      if (L->second.empty())
        continue;

      if (!PrimaryCfg ||
          PrimaryCfg->Nodes.size() < (*L->second.begin())->Nodes.size()) {
        if (PrimaryCfg)
          CfgMap.erase(PrimaryCfgMapEntry);

        PrimaryCfg = *L->second.begin();
        PrimaryCfgMapEntry = L;
      } else {
        CfgMap.erase(L);
      }
    }
  }

  auto startProcessProfileTime = system_clock::now();

  // Map profiles.
  if (!Propf->processProfile()) {
    return false;
  }

  if (config->propellerPrintStats) {
    duration<double> ProcessProfileTime =
        system_clock::now() - startProcessProfileTime;
    duration<double> ReadSymbolTime = startCreateCfgTime - startReadSymbolTime;
    duration<double> CreateCfgTime =
        startProcessProfileTime - startCreateCfgTime;
    fprintf(stderr, "[Propeller] Read all symbols in %f seconds.\n",
            ReadSymbolTime.count());
    fprintf(stderr, "[Propeller] Created all cfgs in %f seconds.\n",
            CreateCfgTime.count());
    fprintf(stderr, "[Propeller] Proccesed the profile in %f seconds.\n",
            ProcessProfileTime.count());
  }

  if (!config->propellerDumpCfgs.empty()) {
    llvm::SmallString<128> CfgOutputDir(config->outputFile);
    llvm::sys::path::remove_filename(CfgOutputDir);
    for (auto &CfgNameToDump : config->propellerDumpCfgs) {
      auto CfgLI = CfgMap.find(CfgNameToDump);
      if (CfgLI == CfgMap.end()) {
        warn("[Propeller] Could not dump cfg for function '" + CfgNameToDump +
             "' : No such function name exists.");
        continue;
      }
      int Index = 0;
      for (auto *Cfg : CfgLI->second) {
        if (Cfg->Name == CfgNameToDump) {
          llvm::SmallString<128> CfgOutput = CfgOutputDir;
          if (++Index <= 1) {
            llvm::sys::path::append(CfgOutput, (Cfg->Name + ".dot"));
          } else {
            llvm::sys::path::append(
                CfgOutput,
                (Cfg->Name + "." + StringRef(std::to_string(Index) + ".dot")));
          }
          if (!Cfg->writeAsDotGraph(CfgOutput.c_str())) {
            warn("[Propeller] Failed to dump Cfg for: '" + CfgNameToDump +
                 "'.");
          }
        }
      }
    }
  }

  // Releasing all support data (symbol ordinal / name map, saved string refs,
  // etc) before moving to reordering.
  Propf.reset(nullptr);
  return true;
}

vector<StringRef> Propeller::genSymbolOrderingFile() {
  calculateNodeFreqs();

  list<ELFCfg *> CfgOrder;
  if (config->propellerReorderFuncs) {
    CCubeAlgorithm Algo;
    Algo.init(*this);
    auto startFuncOrderTime = system_clock::now();
    auto CfgsReordered = Algo.doOrder(CfgOrder);
    if (config->propellerPrintStats){
      duration<double> FuncOrderTime = system_clock::now() - startFuncOrderTime;
      fprintf(stderr, "[Propeller] Reordered %u hot functions in %f seconds.\n",
              CfgsReordered,
              FuncOrderTime.count());
    }
  } else {
    forEachCfgRef([&CfgOrder](ELFCfg &Cfg) { CfgOrder.push_back(&Cfg); });
    CfgOrder.sort([](const ELFCfg *A, const ELFCfg *B) {
      const auto *AEntry = A->getEntryNode();
      const auto *BEntry = B->getEntryNode();
      return AEntry->MappedAddr < BEntry->MappedAddr;
    });
  }

  list<StringRef> SymbolList(1, "Hot");
  const auto HotPlaceHolder = SymbolList.begin();
  const auto ColdPlaceHolder = SymbolList.end();
  unsigned ReorderedN = 0;
  auto startBBOrderTime = system_clock::now();
  for (auto *Cfg : CfgOrder) {
    if (Cfg->isHot() && config->propellerReorderBlocks) {
      NodeChainBuilder(Cfg).doSplitOrder(
          SymbolList, HotPlaceHolder,
          config->propellerSplitFuncs ? ColdPlaceHolder : HotPlaceHolder,
          config->propellerAlignBasicBlocks,
          config->symbolAlignmentFile);
      ReorderedN++;
    } else {
      auto PlaceHolder =
          config->propellerSplitFuncs ? ColdPlaceHolder : HotPlaceHolder;
      Cfg->forEachNodeRef([&SymbolList, PlaceHolder](ELFCfgNode &N) {
        SymbolList.insert(PlaceHolder, N.ShName);
      });
    }
  }
  if (config->propellerPrintStats) {
    duration<double> BBOrderTime = system_clock::now() - startBBOrderTime;
    fprintf(
        stderr,
        "[Propeller] Reordered basic blocks of %u functions in %f seconds.\n",
        ReorderedN, BBOrderTime.count());
  }

  calculatePropellerLegacy(SymbolList, HotPlaceHolder, ColdPlaceHolder);

  if (!config->propellerDumpSymbolOrder.empty()) {
    FILE *fp = fopen(config->propellerDumpSymbolOrder.str().c_str(), "w");
    if (!fp) {
      warn(StringRef("[Propeller] Dump symbol order: failed to open ") + "'" +
           config->propellerDumpSymbolOrder + "'");
    } else {
      for (auto &N : SymbolList) {
        fprintf(fp, "%s\n", N.str().c_str());
      }
      fclose(fp);
      llvm::outs() << "[Propeller] Dumped symbol order file to: '"
                   << config->propellerDumpSymbolOrder.str() << "'.\n";
    }
  }

  SymbolList.erase(HotPlaceHolder);

  return vector<StringRef>(
      std::move_iterator<list<StringRef>::iterator>(SymbolList.begin()),
      std::move_iterator<list<StringRef>::iterator>(SymbolList.end()));
}

void Propeller::calculatePropellerLegacy(
    list<StringRef> &SymList, list<StringRef>::iterator HotPlaceHolder,
    list<StringRef>::iterator ColdPlaceHolder) {
  // No function split or no cold symbols, all bb symbols shall be removed.
  if (HotPlaceHolder == ColdPlaceHolder) return ;
  // For cold bb symbols that are cut out and placed in code segements, we keep
  // only the first bb symbol that starts the code portion of the function.
  StringRef LastFuncName = "";
  for (auto I = std::next(HotPlaceHolder), J = ColdPlaceHolder; I != J; ++I) {
    StringRef SName = *I;
    StringRef FName;
    if (SymbolEntry::isBBSymbol(SName, &FName)) {
      if (LastFuncName.empty() || LastFuncName != FName) {
        PropLeg.BBSymbolsToKeep.insert(SName);
      }
      LastFuncName = FName;
    }
  }
  return;
}

void Propeller::ELFViewDeleter::operator()(ELFView *V) {
  delete V;
}

bool Propeller::ELFViewOrdinalComparator::operator()(const ELFCfg *A,
                                                     const ELFCfg *B) const {
  return A->View->Ordinal < B->View->Ordinal;
}

PropellerLegacy PropLeg;

} // namespace propeller
} // namespace lld
