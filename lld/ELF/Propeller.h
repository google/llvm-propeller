#ifndef LLD_ELF_PROPELLER_H
#define LLD_ELF_PROPELLER_H

#include "InputFiles.h"

#include "lld/Common/PropellerCommon.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/StringSaver.h"

#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

using llvm::StringRef;

using std::list;
using std::map;
using std::mutex;
using std::pair;
using std::set;
using std::unique_ptr;
using std::vector;

namespace lld {
namespace elf {
class SymbolTable;
}

namespace propeller {

class ELFCfg;
class ELFCfgEdge;
class ELFCfgNode;
class ELFView;
class Propeller;

// Propeller Profile
class Propfile {
public:
  Propfile(FILE *PS, Propeller &P)
      : BPAllocator(), PropfileStrSaver(BPAllocator), PStream(PS), Prop(P),
        SymbolOrdinalMap(), LineSize(1024) {
    // LineBuf is to be used w/ getline, which requires the storage allocated by
    // malloc.
    LineBuf = (char *)malloc(LineSize);
  }
  ~Propfile() {
    if (LineBuf) {
      free(LineBuf);
    }
    BPAllocator.Reset();
    if (PStream) fclose(PStream);
  }

  bool readSymbols();
  SymbolEntry *findSymbol(StringRef SymName);
  bool processProfile();

  SymbolEntry *createFunctionSymbol(uint64_t Ordinal, const StringRef &Name,
                                    SymbolEntry::AliasesTy &&Aliases,
                                    uint64_t Size) {
    auto *Sym = new SymbolEntry(Ordinal, Name, std::move(Aliases),
                                SymbolEntry::INVALID_ADDRESS, Size,
                                llvm::object::SymbolRef::ST_Function);
    SymbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(Ordinal),
                             std::forward_as_tuple(Sym));
    for (auto &A: Sym->Aliases) {
      SymbolNameMap[A][""] = Sym;
    }

    if (Sym->Aliases.size() > 1)
      FunctionsWithAliases.push_back(Sym);

    return Sym;
  }

  SymbolEntry *createBasicBlockSymbol(uint64_t Ordinal, SymbolEntry *Function,
                                      StringRef &BBIndex, uint64_t Size) {
    assert(!Function->BBTag && Function->isFunction());
    auto *Sym =
        new SymbolEntry(Ordinal, BBIndex, SymbolEntry::AliasesTy(),
                        SymbolEntry::INVALID_ADDRESS, Size,
                        llvm::object::SymbolRef::ST_Unknown, true, Function);
    SymbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(Ordinal),
                             std::forward_as_tuple(Sym));
    for (auto &A: Function->Aliases) {
      SymbolNameMap[A][BBIndex] = Sym;
    }
    return Sym;
  }

  llvm::BumpPtrAllocator BPAllocator;
  llvm::StringSaver PropfileStrSaver;
  FILE *PStream;
  Propeller &Prop;
  map<uint64_t, unique_ptr<SymbolEntry>> SymbolOrdinalMap;
  // SymbolNameMap is ordered in the following way:
  //   SymbolNameMap[foo][""] = functionSymbol;
  //     SymbolNameMap[foo]["1"] = fun.bb.1.Symbol;
  //     SymbolNameMap[foo]["2"] = fun.bb.2.Symbol;
  //   etc...
  map<StringRef, map<StringRef, SymbolEntry *>> SymbolNameMap;
  list<SymbolEntry*> FunctionsWithAliases;
  uint64_t LineNo;
  char     LineTag;
  size_t   LineSize;
  char    *LineBuf;
};

class Propeller {
public:
  Propeller(lld::elf::SymbolTable *ST)
      : Symtab(ST), Views(), CfgMap(), Propf(nullptr) {}
  ~Propeller() { }

  bool processFiles(std::vector<lld::elf::InputFile *> &Files);
  void processFile(const pair<elf::InputFile *, uint32_t> &Pair);
  ELFCfgNode *findCfgNode(uint64_t SymbolOrdinal);
  void calculateNodeFreqs();
  vector<StringRef> genSymbolOrderingFile();
  void calculatePropellerLegacy(list<StringRef> &SymList,
                                list<StringRef>::iterator HotPlaceHolder,
                                list<StringRef>::iterator ColdPlaceHolder);
  template <class Visitor>
  void forEachCfgRef(Visitor V) {
    for (auto &P : CfgMap) {
      V(*(*(P.second.begin())));
    }
  }

  lld::elf::SymbolTable *Symtab;

  // ELFViewDeleter, which has its implementation in .cpp, saves us from having
  // to have full ELFView definition visibile here.
  struct ELFViewDeleter {
    void operator()(ELFView *V);
  };
  list<unique_ptr<ELFView, ELFViewDeleter>> Views;
  // Same named Cfgs may exist in different object files (e.g. weak
  // symbols.)  We always choose symbols that appear earlier on the
  // command line.  Note: implementation is in the .cpp file, because
  // ELFCfg here is an incomplete type.
  struct ELFViewOrdinalComparator {
    bool operator()(const ELFCfg *A, const ELFCfg *B) const;
  };
  using CfgMapTy = map<StringRef, set<ELFCfg *, ELFViewOrdinalComparator>>;
  CfgMapTy CfgMap;
  unique_ptr<Propfile> Propf;
  // Lock to access / modify global data structure.
  mutex Lock;
};

// When no "-propeller-keep-named-symbols" specified, we remove all BB symbols
// that are hot, and we keep only the first code BB symbol that starts the cold
// code region of the same function. See Below:
// Hot:
//  foo
//  foo.bb.1   <= delete
//  foo.bb.2   <= delete
//  bar
//  bar.bb.1   <= delete
//  bar.bb.3   <= delete
// Cold:
//  foo.bb.3
//  foo.bb.4   <= delete
//  foo.bb.5   <= delete
//  bar.bb.2
//  bar.bb.4   <= delete
//  bar.bb.5   <= delete
struct PropellerLegacy {
  set<StringRef> BBSymbolsToKeep;

  bool shouldKeepBBSymbol(StringRef SymName) {
    if (!SymbolEntry::isBBSymbol(SymName)) return true;
    return BBSymbolsToKeep.find(SymName) != BBSymbolsToKeep.end();
  }
};

extern PropellerLegacy PropLeg;

} // namespace propeller
} // namespace lld
#endif
