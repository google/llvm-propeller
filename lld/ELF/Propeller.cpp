#include "Propeller.h"

#include "PLOELFCfg.h"
#include "PLOELFView.h"
#include "InputFiles.h"

#include "llvm/Support/Parallel.h"

#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <tuple>
#include <vector>

using llvm::StringRef;
using std::list;
using std::map;
using std::string;
using std::tuple;

namespace lld {
namespace propeller {

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

bool Propeller::processPropellerProfile(StringRef &PropellerProf) {
  std::ifstream fin(PropellerProf.str());
  if (!fin.good())
    return false;
  string line;
  char LineTag = '\0';

  map<uint64_t, SymbolEntry *> SymbolOrdinalMap;
  // <Ordinal, FuncIndex, BBIndex, Size>
  list<tuple<uint64_t, uint64_t, StringRef, uint64_t>> BBSymbols;
  while (fin.good() && !std::getline(fin, line).eof()) {
    StringRef L(line);
    if (L == "Symbols" || L == "Branches" || L == "Fallthroughs") {
      LineTag = L[0];
      continue;
    }
    if (LineTag == 'S') {
      uint64_t SOrdinal;
      uint64_t SSize;
      
      auto L1S = L.split(' ');
      auto L1 = L1S.first;
      auto L2S = L1S.second.split(' ');
      auto L2 = L2S.first;
      auto NameStr = L2S.second;
      if (L1.getAsInteger(10, SOrdinal) == true /* means error happens */ ||
          SOrdinal == 0) {
        return false;
      }
      if (L2.getAsInteger(16, SSize) == true) {
        return false;
      }
      if (NameStr.empty()) {
        return false;
      }
      // NameStr is referenced by SymbolEntry StringRefs, so save them.
      PropellerStrSaver.save(NameStr);
      SymbolEntry *NewSymbol = nullptr;
      if (NameStr[0] == 'N') {
        // This is a function symbol.
        auto Names = NameStr.substr(1, NameStr.size() - 1);
        SymbolEntry::AliasesTy SAliases;
        Names.split(SAliases, '/');
        StringRef SName = SAliases[0];
        SAliases.erase(SAliases.begin());
        assert(SymbolOrdinalMap.find(SOrdinal) == SymbolOrdinalMap.end());
        NewSymbol =
            createFunctionSymbol(SOrdinal, SName, std::move(SAliases), SSize);
      } else {
        // It's a bb symbol.
        auto L = NameStr.split('.');
        uint64_t FuncIndex;
        if (L.first.getAsInteger(10, FuncIndex) == true || FuncIndex == 0) {
          return false;
        }
        StringRef BBIndex = L.second;
        auto ExistingI = SymbolOrdinalMap.find(FuncIndex);
        if (ExistingI != SymbolOrdinalMap.end()) {
          assert(!ExistingI->second->isBBSymbol);
          NewSymbol = createBasicBlockSymbol(SOrdinal, ExistingI->second,
                                             BBIndex, SSize);
        } else {
          // A bb symbol appears earlier than its wrapping function, rare, but
          // not impossible, rather play it safely.
          BBSymbols.emplace_back(SOrdinal, FuncIndex, BBIndex, SSize);
        }
      }
      if (NewSymbol) {
        SymbolOrdinalMap[NewSymbol->Ordinal] = NewSymbol;
      }
    }
    if (!BBSymbols.empty() && LineTag != 'S') {
      for (auto &S : BBSymbols) {
        uint64_t SOrdinal;
        uint64_t FuncIndex;
        uint64_t SSize;
        StringRef BBIndex;
        std::tie(SOrdinal, FuncIndex, BBIndex, SSize) = S;
        auto ExistingI = SymbolOrdinalMap.find(FuncIndex);
        assert(ExistingI != SymbolOrdinalMap.end());
        SymbolOrdinalMap[SOrdinal] =
            createBasicBlockSymbol(SOrdinal, ExistingI->second, BBIndex, SSize);
      }
      list<tuple<uint64_t, uint64_t, StringRef, uint64_t>> Tmp;
      BBSymbols.swap(Tmp);  // Empty and release BBSymbols.
    }
    if(LineTag == 'B') {
      auto S0 = L.split(' ');
      uint64_t From, To, Cnt;
      bool isJump = false;
      S0.first.getAsInteger(10, From);
      auto S1 = S0.second.split(' ');
      S1.first.getAsInteger(10, To);
      auto S2 = S1.second.split(' ');
      S2.first.getAsInteger(10, Cnt);
      if (!S2.second.empty()) {
        assert(S2.second == "C");
        isJump = true;
      }
    }
  }
  // check
  for (auto &P : SymbolOrdinalMap) {
    auto *Sym = P.second;
    if (!((Sym->isBBSymbol && Sym->ContainingFunc->isFunction()) ||
          (!Sym->isBBSymbol && !Sym->ContainingFunc))) {
      fprintf(stderr, "Check error.\n");
      exit(1);
    }
  }
  fprintf(stderr, "Check good.\n");
  return true;
}

bool Propeller::processFiles(std::vector<lld::elf::InputFile *> &Files,
                             StringRef PropellerProf) {
  fprintf(stderr, "Entering into Propeller...\n");
  if (!processPropellerProfile(PropellerProf)) {
    error("Failed to process propeller file.");
    return false;
  }

  vector<pair<elf::InputFile *, uint32_t>> FileOrdinalPairs;
  int Ordinal = 0;
  for (auto &F : Files) {
    FileOrdinalPairs.emplace_back(F, ++Ordinal);
  }
  llvm::parallel::for_each(
      llvm::parallel::parallel_execution_policy(), FileOrdinalPairs.begin(),
      FileOrdinalPairs.end(),
      std::bind(&Propeller::processFile, this, std::placeholders::_1));

  for (auto &S: CfgMap) {
    for (auto &T: S.second) {
      fprintf(stderr, "%s: %lu\n", S.first.str().c_str(), T->Nodes.size());
    }
  }
  return true;
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