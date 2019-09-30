//===------------------------ Propeller.h ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Propeller.h defines Propeller framework classes:
//
//   Propfile - parses and wraps propeller profile
//
//   Propeller - the main propeller framework class
//
// Propeller starts by checking if "-o" file matches propeller profile
// (Propeller::checkPropellerTarget), then it enters Propeller::processFiles(),
// which builds Cfg for each valid ELF object file (Propeller::processFile ->
// ELFCfgBuilder::buildCfgs).
//
// After Cfgs are build, Propeller starts parsing Propeller profile (the
// Propfile class). In this step, bb symbols are created, branch/call counters
// are read, and *very importantly*, the counters are applied to the Cfg. For
// example, upon seeing two consecutive branch records in the propeller profile:
// a->b, c->d, we not only increment edge counters for a->b, c->d, we also walks
// from b->c, and increment basicblock and edge counters in between. This last
// step can only be done after we have a complete Cfg for the function.
//
// The Cfg information is stored in Propeller::CfgMap.
//
// After we have Cfgs with complete counters for edges/bbs, we pass the
// information to optimization passes. For now, depending on 
// propellerReorderFuncs, propellerReorderBlocks or propellerSplitFuncs,
// propeller generates a list of basicblock symbol orders and feed it the origin
// linker phase. This step is done in Propeller::genSymbolOrderingFile.
//
//===----------------------------------------------------------------------===//

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
// a sample propeller profile is like below:
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
  Propfile(FILE *pS, Propeller &p)
      : BPAllocator(), PropfileStrSaver(BPAllocator), PStream(pS), Prop(p),
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

  bool matchesOutputFileName(const StringRef &outputFile);
  bool readSymbols();
  SymbolEntry *findSymbol(StringRef symName);
  bool processProfile();

  SymbolEntry *createFunctionSymbol(uint64_t ordinal, const StringRef &name,
                                    SymbolEntry::AliasesTy &&aliases,
                                    uint64_t size) {
    auto *sym = new SymbolEntry(ordinal, name, std::move(aliases),
                                SymbolEntry::INVALID_ADDRESS, size,
                                llvm::object::SymbolRef::ST_Function);
    SymbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(ordinal),
                             std::forward_as_tuple(sym));
    for (auto &a: sym->Aliases) {
      SymbolNameMap[a][""] = sym;
    }

    if (sym->Aliases.size() > 1)
      FunctionsWithAliases.push_back(sym);

    return sym;
  }

  SymbolEntry *createBasicBlockSymbol(uint64_t ordinal, SymbolEntry *function,
                                      StringRef &bBIndex, uint64_t size) {
    // bBIndex is of the form "1", "2", it's a stringref to integer.
    assert(!function->BBTag && function->isFunction());
    auto *sym =
        new SymbolEntry(ordinal, bBIndex, SymbolEntry::AliasesTy(),
                        SymbolEntry::INVALID_ADDRESS, size,
                        llvm::object::SymbolRef::ST_Unknown, true, function);
    SymbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(ordinal),
                             std::forward_as_tuple(sym));
    for (auto &a: function->Aliases) {
      SymbolNameMap[a][bBIndex] = sym;
    }
    return sym;
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
  bool processFiles(std::vector<lld::elf::InputFile *> &files);
  void processFile(const pair<elf::InputFile *, uint32_t> &pair);
  ELFCfgNode *findCfgNode(uint64_t symbolOrdinal);
  void calculateNodeFreqs();
  vector<StringRef> genSymbolOrderingFile();
  void calculatePropellerLegacy(list<StringRef> &SymList,
                                list<StringRef>::iterator hotPlaceHolder,
                                list<StringRef>::iterator coldPlaceHolder);
  template <class Visitor>
  void forEachCfgRef(Visitor v) {
    for (auto &p : CfgMap) {
      v(*(*(p.second.begin())));
    }
  }

  lld::elf::SymbolTable *Symtab;

  // ELFViewDeleter, which has its implementation in .cpp, saves us from having
  // to have full ELFView definition visibile here.
  struct ELFViewDeleter {
    void operator()(ELFView *v);
  };
  list<unique_ptr<ELFView, ELFViewDeleter>> Views;
  // Same named Cfgs may exist in different object files (e.g. weak
  // symbols.)  We always choose symbols that appear earlier on the
  // command line.  Note: implementation is in the .cpp file, because
  // ELFCfg here is an incomplete type.
  struct ELFViewOrdinalComparator {
    bool operator()(const ELFCfg *a, const ELFCfg *b) const;
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

  bool shouldKeepBBSymbol(StringRef symName) {
    if (!SymbolEntry::isBBSymbol(symName)) return true;
    return BBSymbolsToKeep.find(symName) != BBSymbolsToKeep.end();
  }
};

extern PropellerLegacy PropLeg;

} // namespace propeller
} // namespace lld
#endif
