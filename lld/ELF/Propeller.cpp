#include "Propeller.h"

#include "Config.h"
#include "PLOBBReordering.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"
#include "PLOFuncOrdering.h"
#include "InputFiles.h"

#include "llvm/Support/Parallel.h"

#include <stdio.h>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <numeric>
#include <tuple>
#include <vector>

using lld::elf::Config;
using lld::plo::CCubeAlgorithm;
using lld::plo::PLOFuncOrdering;
using lld::plo::ExtTSPChainBuilder;
using llvm::StringRef;
using std::list;
using std::map;
using std::string;
using std::tuple;
using std::vector;

namespace lld {
namespace propeller {

SymbolEntry *Propfile::findSymbol(StringRef SymName) {
  StringRef FuncName;
  StringRef BBIndex;
  if (!SymbolEntry::isBBSymbol(SymName, &FuncName, &BBIndex)) {
    FuncName = SymName;
    BBIndex = "";
  }
  auto L1 = SymbolNameMap.find(FuncName);
  if (L1 != SymbolNameMap.end()) {
    auto L2 = L1->second.find(BBIndex);
    if (L2 != L1->second.end()) {
      return L2->second;
    }
  }
  fprintf(stderr, "Warning: failed to find symbol for :%s\n",
          SymName.str().c_str());
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
    if (LineBuf[0] == '#')
      continue;
    if (LineBuf[0] == 'S' || LineBuf[0] == 'B' || LineBuf[0] == 'F') {
      LineTag = LineBuf[0];
      continue;
    }
    if (LineBuf[R - 1] == '\n') {
      LineBuf[--R] = '\0'; // drop '\n' character at the end;
    }
    StringRef L(LineBuf); // LineBuf is null-terminated.
    if (LineTag != '\0' && LineTag != 'S')
      break;

    uint64_t SOrdinal;
    uint64_t SSize;
    auto L1S = L.split(' ');
    auto L1 = L1S.first;
    auto L2S = L1S.second.split(' ');
    auto L2 = L2S.first;
    auto EphemeralStr = L2S.second;
    if (L1.getAsInteger(10, SOrdinal) == true /* means error happens */ ||
        SOrdinal == 0) {
      error("Invalid ordinal field at propfile line: " +
            std::to_string(LineNo));
      return false;
    }
    if (L2.getAsInteger(16, SSize) == true) {
      error("Invalid size field at propfile line: " +
            std::to_string(LineNo));
      return false;
    }
    if (EphemeralStr.empty()) {
      error("Invalid name field at propfile line: " +
            std::to_string(LineNo));
      return false;
    }
    if (EphemeralStr[0] == 'N') { // Function symbol?
      // Save EphemeralStr for persistency across Propeller lifecycle.
      StringRef SavedNameStr = PropfileStrSaver.save(EphemeralStr.substr(1));
      SymbolEntry::AliasesTy SAliases;
      SavedNameStr.split(SAliases, '/');
      StringRef SName = SAliases[0];
      SAliases.erase(SAliases.begin());
      assert(SymbolOrdinalMap.find(SOrdinal) == SymbolOrdinalMap.end());
      createFunctionSymbol(SOrdinal, SName, std::move(SAliases), SSize);
    } else {
      // It's a bb symbol.
      auto L = EphemeralStr.split('.');
      uint64_t FuncIndex;
      if (L.first.getAsInteger(10, FuncIndex) == true || FuncIndex == 0) {
        error("Invalid function index field at propfile line: " +
            std::to_string(LineNo));
        return false;
      }
      // Only save the index part, which is highly reusable.
      StringRef BBIndex = PropfileStrSaver.save(L.second);
      auto ExistingI = SymbolOrdinalMap.find(FuncIndex);
      if (ExistingI != SymbolOrdinalMap.end()) {
        assert(!ExistingI->second->BBTag);
        createBasicBlockSymbol(SOrdinal, ExistingI->second.get(), BBIndex,
                               SSize);
      } else {
        // A bb symbol appears earlier than its wrapping function, rare, but
        // not impossible, rather play it safely.
        BBSymbols.emplace_back(SOrdinal, FuncIndex, BBIndex, SSize);
      }
    }
  } // End of iterating all symbols.

  if (!BBSymbols.empty() && LineTag != 'S') {
    for (auto &S : BBSymbols) {
      uint64_t SOrdinal;
      uint64_t FuncIndex;
      uint64_t SSize;
      StringRef BBIndex;
      std::tie(SOrdinal, FuncIndex, BBIndex, SSize) = S;
      auto ExistingI = SymbolOrdinalMap.find(FuncIndex);
      assert(ExistingI != SymbolOrdinalMap.end());
      createBasicBlockSymbol(SOrdinal, ExistingI->second.get(), BBIndex, SSize);
    }
    list<tuple<uint64_t, uint64_t, StringRef, uint64_t>> Tmp;
    BBSymbols.swap(Tmp); // Empty and release BBSymbols.
  }

  // check
  for (auto &P : SymbolOrdinalMap) {
    auto *Sym = P.second.get();
    if (!((Sym->BBTag && Sym->ContainingFunc->isFunction()) ||
          (!Sym->BBTag && !Sym->ContainingFunc))) {
      fprintf(stderr, "Check error.\n");
      exit(1);
    }
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
  while ((R = getline(&LineBuf, &LineSize, PStream)) != -1) {
    ++LineNo;
    if (R == 0)
      continue;
    if (LineBuf[0] == '#')
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
      error(string("Unrecogniz line #") + std::to_string(LineNo) +
            " in propfile:\n" + L.str());
      return false;
    }
    ELFCfgNode *FromN = Prop.findCfgNode(From);
    ELFCfgNode *ToN = Prop.findCfgNode(To);
    if (!FromN || !ToN) {
      continue;
    }

    if (LineTag == 'B') {
      if (FromN->Cfg == ToN->Cfg) {
        FromN->Cfg->mapBranch(FromN, ToN, Cnt, Tag == 'C', Tag == 'R');
      } else {
        FromN->Cfg->mapCallOut(FromN, ToN, 0, Cnt, Tag == 'C', Tag == 'R');
      }
    } else {
      // LineTag == 'F'
      if (FromN->Cfg == ToN->Cfg) {
        if (!FromN->Cfg->markPath(FromN, ToN, Cnt)) {
          fprintf(stderr, "Waring: failed to mark %lu -> %lu (%s -> %s)\n",
                  From, To, FromN->ShName.str().c_str(),
                  ToN->ShName.str().c_str());
        } else {
          fprintf(stderr, "Done marking: %lu -> %lu (%s -> %s)\n",
                  From, To, FromN->ShName.str().c_str(),
                  ToN->ShName.str().c_str());
        }
      }
    }
  }
  return true;
}

void Propeller::processFile(const pair<elf::InputFile *, uint32_t> &Pair) {
  auto *Inf = Pair.first;
  fprintf(stderr, "Processing: %s\n", Inf->getName().str().c_str());
  ELFView *View = ELFView::create(Inf->getName(), Pair.second, Inf->MB);
  if (View) {
    lld::plo::ELFCfgBuilder(*this, View).buildCfgs();
    {
      // Updating global data structure.
      std::lock_guard<mutex> L(this->Lock);
      this->Views.emplace_back(View);
      for (auto &P : View->Cfgs) {
        auto R = CfgMap[P.first].emplace(P.second.get());
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
    error(string("Invalid symbol ordinal: " + std::to_string(SymbolOrdinal)));
    return nullptr;
  }
  StringRef FuncName = S->BBTag ? S->ContainingFunc->Name : S->Name;
  auto CfgLI = CfgMap.find(FuncName);
  if (CfgLI != CfgMap.end()) {
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
    // Also not, Objects (CfgLI->second) are sorted in the way
    // they appear on the command line, which is the same as how
    // linker chooses the weak symbol definition.
    string FQN = S->getFQN();
    for (auto *Cfg : CfgLI->second) {
      // Check Cfg does have name "SymName".
      for (auto &N : Cfg->Nodes) {
        if (N->ShName == FQN) {
          return N.get();
        }
      }
    }
  }
  warn(string("No Cfg found for '") + FuncName.str() +
       "', ordinal: " + std::to_string(SymbolOrdinal) + ".");
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
  if (Config->Propeller.empty()) return true;
  fprintf(stderr, "Entering into Propeller...\n");
  string PropellerFileName = Config->Propeller.str();
  FILE *PropFile = fopen(PropellerFileName.c_str(), "r");
  if (!PropFile) {
    error(string("Failed to open '") + PropellerFileName + "'.");
    return false;
  }
  Propf = llvm::make_unique<Propfile>(PropFile, *this);
  if (!Propf->readSymbols()) {
    error(string("Invalid propfile: '") + PropellerFileName + "'.");
    return false;
  }

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

  // Map profiles.
  if (!Propf->processProfile()) {
    return false;
  }

  // Releasing every support data (symbol ordinal / name map, saved string refs,
  // etc) before moving to reordering.
  Propf.reset(nullptr);

  calculateNodeFreqs();

  // Do reordering ...
  return true;
}

vector<StringRef> Propeller::genSymbolOrderingFile() {
  list<const ELFCfg *> CfgOrder;

  if (Config->ReorderFunctions) {
    CfgOrder = lld::plo::CCubeAlgorithm<Propeller>(*this).doOrder();
  } else {
    std::vector<ELFCfg *> OrderResult;
    forEachCfgRef([&OrderResult](ELFCfg &Cfg) { OrderResult.push_back(&Cfg); });

    std::sort(OrderResult.begin(), OrderResult.end(),
              [](const ELFCfg *A, const ELFCfg *B) {
                const auto &AEntry = A->getEntryNode();
                const auto &BEntry = B->getEntryNode();
                return AEntry->MappedAddr < BEntry->MappedAddr;
              });
    std::copy(OrderResult.begin(), OrderResult.end(),
              std::back_inserter(CfgOrder));
  }

  list<StringRef> SymbolList(1, "Hot");
  const auto HotPlaceHolder = SymbolList.begin();
  const auto ColdPlaceHolder = SymbolList.end();
  for (auto *Cfg : CfgOrder) {
    if (Cfg->isHot() && Config->ReorderBlocks) {
      ExtTSPChainBuilder(Cfg).doSplitOrder(
          SymbolList, HotPlaceHolder,
          Config->SplitFunctions ? ColdPlaceHolder : HotPlaceHolder);
    } else {
      Cfg->forEachNodeRefConst([&SymbolList, ColdPlaceHolder](ELFCfgNode &N) {
        SymbolList.insert(ColdPlaceHolder, N.ShName);
      });
    }
  }
  SymbolList.erase(HotPlaceHolder);
  return vector<StringRef>(
      std::move_iterator<list<StringRef>::iterator>(SymbolList.begin()),
      std::move_iterator<list<StringRef>::iterator>(SymbolList.end()));
}

void Propeller::ELFViewDeleter::operator()(lld::plo::ELFView *V) {
  delete V;
}

bool Propeller::ELFViewOrdinalComparator::operator()(const ELFCfg *A,
                                                     const ELFCfg *B) const {
  return A->View->Ordinal < B->View->Ordinal;
}

} // namespace propeller
} // namespace lld
