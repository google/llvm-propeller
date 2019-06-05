#include "Propeller.h"

#include "PLOELFCfg.h"
#include "PLOELFView.h"
#include "InputFiles.h"

#include "llvm/Support/Parallel.h"

#include <stdio.h>
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
  char LineTag = '\0';
  list<tuple<uint64_t, uint64_t, StringRef, uint64_t>> BBSymbols;
  while ((R = getline(&LineBuf, &LineSize, PStream)) != -1) {
    if (R == 0)
      continue;
    if (LineBuf[0] == '#')
      continue;
    if (LineBuf[0] == 'S' || LineBuf[0] == 'B' || LineBuf[0] == 'F') {
      LineTag = LineBuf[0];
      continue;
    }
    if (LineBuf[R] == '\n') {
      LineBuf[R--] = '\0'; // drop '\n' character at the end;
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
      return false;
    }
    if (L2.getAsInteger(16, SSize) == true) {
      return false;
    }
    if (EphemeralStr.empty()) {
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

    // if(LineTag == 'B') {
    //   auto S0 = L.split(' ');
    //   uint64_t From, To, Cnt;
    //   bool isJump = false;
    //   S0.first.getAsInteger(10, From);
    //   auto S1 = S0.second.split(' ');
    //   S1.first.getAsInteger(10, To);
    //   auto S2 = S1.second.split(' ');
    //   S2.first.getAsInteger(10, Cnt);
    //   if (!S2.second.empty()) {
    //     assert(S2.second == "C");
    //     isJump = true;
    //   }
    //   ELFCfgNode *FromN = findCfgNode(From);
    //   ELFCfgNode *ToN = findCfgNode(To);
    // }

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
//   assert (SymbolOrdinalMap.find(SymbolOrdinal) != SymbolOrdinalMap.end());
//   SymbolEntry *S = SymbolOrdinalMap[SymbolOrdinal];
//   if (!S) {
//     // This is an internal error, should not happen.
//     error(string("Invalid symbol ordinal: " + std::to_string(SymbolOrdinal)));
//     return nullptr;
//   }
//   StringRef FuncName = S->isBBSymbol ? S->ContainingFunc->Name : S->Name;
//   auto CfgLI = CfgMap.find(FuncName);
//   if (CfgLI != CfgMap.end()) {
//     // There might be multiple object files that define SymName.
//     // So for "funcFoo.bb.3", we return Obj2.
//     // For "funcFoo.bb.1", we return Obj1 (the first matching obj).
//     // Obj1:
//     //    Cfg1: funcFoo
//     //          funcFoo.bb.1
//     //          funcFoo.bb.2
//     // Obj2:
//     //    Cfg1: funcFoo
//     //          funcFoo.bb.1
//     //          funcFoo.bb.2
//     //          funcFoo.bb.3
//     // Also not, Objects (CfgLI->second) are sorted in the way
//     // they appear on the command line, which is the same as how
//     // linker chooses the weak symbol definition.
//     for (auto *Cfg : CfgLI->second) {
//       // Check Cfg does have name "SymName".
//       for (auto &N : Cfg->Nodes) {
//         if (N->ShName == S->Name) {
//           return N.get();
//         }
//       }
//     }
//   }
//   warn(string("No Cfg found for '") + FuncName.str() +
//        "', ordinal: " + std::to_string(SymbolOrdinal) + ".");
  return nullptr;
}

bool Propeller::processFiles(std::vector<lld::elf::InputFile *> &Files,
                             StringRef PropfileName) {
  fprintf(stderr, "Entering into Propeller...\n");

  FILE *PropF = fopen(PropfileName.str().c_str(), "r");
  if (!PropF) {
    error(string("Failed to open '") + PropfileName.str() + "'.");
    return false;
  }
  Propf = llvm::make_unique<Propfile>(PropF, *this);
  if (!Propf->readSymbols()) {
    error(string("Invalid propfile: '") + PropfileName.str() + "'.");
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
