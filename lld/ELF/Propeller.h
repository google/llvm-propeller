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

// Propeller profile parser.
//
// A sample propeller profile is like below:
//
// Symbols
// 1 0 N.init/_init
// 2 0 N.plt
// 3 0 N.plt.got
// 4 0 N.text
// 5 2b N_start
// 6 0 Nderegister_tm_clones
// 7 0 Nregister_tm_clones
// 8 0 N__do_global_dtors_aux
// 9 0 Nframe_dummy
// 10 2c Ncompute_flag
// 11 7c Nmain
// 12 f 11.1
// 13 28 11.2
// 14 b 11.3
// 15 a 11.4
// 16 65 N__libc_csu_init
// 17 2 N__libc_csu_fini
// 18 0 N.fini/_fini
// 19 5e N_ZN9assistantD2Ev/_ZN9assistantD1Ev
// Branches
// 10 12 232590 R
// 12 10 234842 C
// 12 14 143608
// 14 12 227040
// Fallthroughs
// 10 10 225131
// 10 12 2255
// 12 10 2283
// 12 12 362886
// 12 14 77103
// 14 12 1376
// 14 14 140856
// !func1
// !func2
// !func3
//
// The file consists of 4 parts, "Symbols", "Branches", "Fallthroughs" and
// Funclist.
//
// Each line in "Symbols" section contains the following field:
//   index    - in decimal, unique for each symbol, start from 1
//   size     - in hex, without "0x"
//   name     - either starts with "N" or a digit. In the former case,
//              everything after N is the symbol name. In the latter case, it's
//              in the form of "a.b", "a" is a symbol index, "b" is the bb
//              identification string (could be an index number). For the above
//              example, name "14.2" means "main.bb.2", because "14" points to
//              symbol main. Also note, symbols could have aliases, in such
//              case, aliases are concatenated with the original name with a
//              '/'. For example, symbol 17391 contains 2 aliases.
// Note, the symbols listed are in strict non-decreasing address order.
//
// Each line in "Branches" section contains the following field:
//   from     - sym_index, in decimal
//   to       - sym_index, in decimal
//   cnt      - counter, in decimal
//   C/R      - a field indicate whether this is a function call or a return,
//              could be empty if it's just a normal branch.
//
// Each line in "Fallthroughs" section contains exactly the same fields as in
// "Branches" section, except the "C" field.
//
// Funclist contains lines that starts with "!", and everything following that
// will be the function name that's to be consumed by compiler (for bb section
// generation purpose).
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

  bool matchesOutputFileName(const StringRef &OutputFile);
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
    // BBIndex is of the form "1", "2", it's a stringref to integer.
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
  llvm::UniqueStringSaver PropfileStrSaver;
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

  bool checkPropellerTarget();
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
