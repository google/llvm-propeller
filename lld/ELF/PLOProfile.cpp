#include "PLOProfile.h"

#include "PLO.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"

#include <fstream>
#include <iostream>

#include <llvm/ADT/SmallVector.h>

using std::ostream;
using std::string;

using llvm::SmallVector;

namespace lld {
namespace plo {

bool LBREntry::fillEntry(const StringRef &SR, LBREntry &Entry) {
  auto L1 = SR.split('/');
  // Passing "0" as radix enables autosensing, so no need to skip "0x"
  // prefix.
  if (L1.first.empty() || L1.first.getAsInteger(0, Entry.From))
    return false;
  auto L2 = L1.second.split('/');
  if (L2.first.empty() || L2.first.getAsInteger(0, Entry.To))
    return false;
  auto L3 = L2.second.split('/');
  if (L3.first.empty() || L3.second.empty())
    return false;
  Entry.Predict = *(L3.first.data());
  if (Entry.Predict != 'M' && Entry.Predict != 'P' && Entry.Predict != '-')
    return false;
  if (L3.second.rsplit('/').second.getAsInteger(10, Entry.Cycles))
    return false;
  return true;
}

PLOProfile::~PLOProfile() {}

bool PLOProfile::process(StringRef &ProfileName) {
  std::ifstream fin(ProfileName.str());
  if (!fin.good()) return false;
  string line;
  // Preallocate "Entries" (total size = sizeof(LBREntry) * 32 bytes =
  // 6k). Which is way more faster than create space everytime.
  LBREntry EntryArray[32];
  while (fin.good() && !std::getline(fin, line).eof()) {
    if (line.empty()) continue;
    int EntryIndex = 0;
    const char *p = line.c_str();
    const char *q = p + 1;
    do {
      while (*(q++) != ' ');
      StringRef EntryString = StringRef(p, q - p - 1);
      if (!LBREntry::fillEntry(EntryString, EntryArray[EntryIndex])) {
        fprintf(stderr, "Invalid entry: %s\n", EntryString.str().c_str());
        break;
      }
      if (*q == '\0') break;
      p = q + 1;
      q = p + 1;
      ++EntryIndex;
    } while(true);
    if (EntryIndex) {
      processLBR(EntryArray, EntryIndex);
    }
  }
  return true;
}

static bool
isBBSymbol(const StringRef &SymName, StringRef &FuncName) {
  FuncName = "";
  auto R = SymName.split(".bb.");
  if (R.second.empty()) return false;
  for (const char *I = R.second.data(), *J = R.second.data() + R.second.size();
       I != J; ++I)
    if (*I < '0' || *I > '9') return false;
  FuncName = R.first;
  return true;
}

bool PLOProfile::symContainsAddr(StringRef &SymName,
                                 uint64_t   SymAddr,
                                 uint64_t   SymSize,
                                 uint64_t   Addr,
                                 StringRef &FuncName) {
  if (!isBBSymbol(SymName, FuncName)) {
    FuncName = SymName;
  }
  auto PairI = Plo.Syms.NameMap.find(FuncName);
  if (PairI != Plo.Syms.NameMap.end()) {
    uint64_t FuncAddr = Plo.Syms.getAddr(PairI->second);
    uint64_t FuncSize = Plo.Syms.getSize(PairI->second);
    if (FuncSize > 0 && FuncAddr <= Addr && Addr < FuncAddr + FuncSize) {
      return true;
    }
  }
  FuncName = "";
  return false;
}

void PLOProfile::cacheSearchResult(uint64_t Addr, ELFCfgNode *Node) {
  if (SearchTimeline.size() > MaxCachedResults) {
    auto A = SearchTimeline.begin();
    auto EraseResult = SearchCacheMap.erase(*A);
    (void)EraseResult;
    assert(EraseResult > 0);
    SearchTimeline.erase(A);
  }

  assert(SearchCacheMap.find(Addr) == SearchCacheMap.end());
  SearchTimeline.push_back(Addr);
  SearchCacheMap.emplace(std::piecewise_construct,
                         std::forward_as_tuple(Addr),
                         std::forward_as_tuple(Node));
}

bool PLOProfile::findCfgForAddress(uint64_t Addr,
                                   ELFCfg *&ResultCfg,
                                   ELFCfgNode *&ResultNode) {
  auto CacheI = SearchCacheMap.find(Addr);
  if (CacheI != SearchCacheMap.end()) {
    ResultNode = CacheI->second;
    ResultCfg = ResultNode->Cfg;
    return true;
  }
  ResultCfg = nullptr, ResultNode = nullptr;
  auto T = Plo.Syms.AddrMap.upper_bound(Addr);  // first element > Addr.
  if (T == Plo.Syms.AddrMap.begin())
    return false;
  auto T0 = std::prev(T);
  // There are multiple symbols registered on the same address.
  uint64_t SymAddr = T0->first;
  for (auto Handler: T0->second) {
    StringRef IndexName;
    StringRef SymName = Plo.Syms.getName(Handler);
    uint64_t  SymSize = Plo.Syms.getSize(Handler);
    if (symContainsAddr(SymName, SymAddr, SymSize, Addr, IndexName)) {
      auto CfgLI = Plo.CfgMap.find(IndexName);
      if (CfgLI != Plo.CfgMap.end()) {
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
        for (auto *Cfg: CfgLI->second) {
          // Check Cfg does have name "SymName".
          for (auto &N: Cfg->Nodes) {
            if (N->ShName == SymName) {
              ResultCfg = Cfg;
              ResultNode = N.get();
              cacheSearchResult(Addr, ResultNode);
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

void PLOProfile::processLBR(LBREntry *EntryArray, int EntryIndex) {
  ELFCfg *LastToCfg{nullptr};
  ELFCfgNode *LastToNode{nullptr};
  uint64_t LastFromAddr{0}, LastToAddr{0};

  // The fist entry in the record is the branch that happens last in
  // history.  The second entry happens earlier than the first one,
  // ..., etc.  So we iterate the entries in reverse order - the
  // earliest in history -> the latest.
  for (int P = EntryIndex - 1; P >= 0; --P) {
    auto &Entry = EntryArray[P];
    uint64_t From = Entry.From, To = Entry.To;
    ELFCfg *FromCfg, *ToCfg;
    ELFCfgNode *FromNode, *ToNode;
    findCfgForAddress(From, FromCfg, FromNode);
    findCfgForAddress(To, ToCfg, ToNode);

    if (FromCfg && FromCfg == ToCfg) {
      FromCfg->mapBranch(FromNode, ToNode);
      ++IntraFunc;
    } else if (FromCfg && ToCfg /* implies: FromCfg != ToCfg */ ) {
      FromCfg->mapCallOut(FromNode, ToNode, To);
      ++InterFunc;
    }
    // Mark everything between LastToCfg[LastToNode] and FromCfg[FromNode].
    if (FromCfg && LastToCfg == FromCfg) {
      ++IntraFunc;
      if (LastToCfg->markPath(LastToNode, FromNode) == false) {
        if (!(LastFromAddr == From && LastToAddr == To && P == 0)) {
          ++NonMarkedIntraFunc;
          // std::cout << "*****" << std::endl;
          // std::cout << "Failed to map " << std::showbase << std::hex
          //           << LastToAddr << " -> " << From
          //           << " LBR@" << std::noshowbase << std::dec << Idx << " : "
          //           << *LastToNode << " -> "
          //           << *FromNode << std::endl;
          // std::cout << *FromCfg << std::endl;
        }
      }
    } else {
      ++InterFunc;
      if (LastToCfg && FromCfg && LastToCfg != FromCfg) {
        if (!(LastFromAddr == From && LastToAddr == To && P == 0)) {
          // std::cout << "Failed to map: " << std::showbase << std::hex
          //           << LastToAddr << " -> " << From << std::endl;
          // std::cout << "Last entry:    " << *LastEntry << std::endl;
          // std::cout << "Current Entry: " << *Entry << std::endl;
          // std::cout << "Last: " << *LastToNode << std::endl;
          // std::cout << "From: " << *FromNode << std::endl;
          // exit(1);
          ++NonMarkedInterFunc;
        }
      }
    }
    LastToCfg = ToCfg;
    LastToNode = ToNode;
    LastFromAddr = From;
    LastToAddr = To;
  }
}

ostream & operator << (ostream &Out, const LBREntry &Entry) {
  Out << std::showbase << std::hex << Entry.From << " -> "
      << std::showbase << std::hex << Entry.To;
  return Out;
}

}  // end of namespace plo
}  // end of namespace lld
