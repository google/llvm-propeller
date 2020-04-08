//===------------------------ Propeller.h ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See README.md for propeller framework.
//
//=========================================================================
//
// Propeller.h defines Propeller framework classes:
//
//   Propfile - parses and wraps propeller profile
//
//   Propeller - the main propeller framework class
//
// Propeller starts by checking if "-o" file matches propeller profile
// (Propeller::checkPropellerTarget), then it enters
// Propeller::processFiles(), which builds control flow graph (controlFlowGraph)
// for each valid ELF object file (Propeller::processFile ->
// CFGBuilder::buildCFGs).
//
// After control flow graphs are build, Propeller starts parsing
// Propeller profile (the Propfile class). In this step, basicblock
// (bb) symbols are created, branch/call counters are read, and *very
// importantly*, the counters are applied to the controlFlowGraph. For example,
// upon seeing two consecutive branch records in the propeller
// profile: a->b, c->d, we not only increment edge counters for a->b,
// c->d, we also walks from b->c, and increment basicblock and edge
// counters in between. This last step can only be done after we have
// a complete controlFlowGraph for the function.
//
// The controlFlowGraph information is stored in Propeller::cfgMap.
//
// After we have cfgs with complete counters for edges/bbs, we pass the
// information to optimization passes. For now, depending on
// propellerReorderFuncs, propellerReorderBlocks or propellerSplitFuncs,
// propeller generates a list of basicblock symbol orders and feed it the origin
// linker phase. This step is done in Propeller::genSymbolOrderingFile.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_PROPELLER_H
#define LLD_ELF_PROPELLER_H

#include "PropellerProtobuf.h"

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ProfileData/BBSectionsProf.h"
#include "llvm/Support/StringSaver.h"

#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

using llvm::propeller::SymbolEntry;

namespace lld {
namespace propeller {

extern class Propeller *prop;

class ControlFlowGraph;
class CFGEdge;
class CFGNode;
class ObjectView;
class Propeller;
class CodeLayout;
struct PropellerConfig;

// Propeller profile parser.
//
// A sample propeller profile is like below:
//
// @clang
// !func1
// !!1
// !!2
// !func2
// !!1
// !!3
// Symbols
// 1 0 N.init/_init
// 2 0 N.plt
// ...
// ...
// 11 7c Nmain
// 12 f 11.1
// 13 28 11.2
// 14 b 11.3
// 15 a 11.4r
// 16 65 N__libc_csu_init
// 17 2 N__libc_csu_fini
// 18 0 N.fini/_fini
// 19 5e N_ZN9assistantD2Ev/_ZN9assistantD1Ev
// Branches
// 10 12 232590 R
// 12 10 234842 C
// 12 14 143608
// Fallthroughs
// 10 10 225131
//
// The file consists of 4 parts, "hot symbols", "Symbols", "Branches" and
// "Fallthroughs".
//
// [hot symbols] section
// The "hot symbols" section contains lines that starts with "!" followed by a
// function name or another "!" followed by a bb index. This section is to guide
// the compiler in generating bb sections only for hot BBs.
//
// [Symbols] section
// Each line in "Symbols" section contains the following field:
//   index    - in decimal, unique for each symbol, start from 1
//   size     - in hex, without "0x"
//   name     - either starts with "N" or a digit. In the former case,
//              everything after N is the symbol name. In the latter case, it's
//              in the form of "a.b[rlL]", "a" is a symbol index, "b" is the bb
//              index and [rlL] is the optional bb type suffix. For the above
//              example, name "11.2" means "main.bb.2", as "11" points to
//              symbol main; name "11.4r" means bb4 is a return bb. Also note,
//              symbols could have aliases, in such case, aliases are
//              concatenated with the original name with a '/'. For example,
//              symbol 19 contains 2 aliases. Also, the optional bb type
//              suffix l|L|r, which, when exists, indicates that the bb is a
//              landingpad, return-and-landingpad and return respecitvely.
//
// Note, the symbols listed are in strict non-decreasing address order.
//
// [Branches] section
// Each line in "Branches" section contains the following field:
//   from     - sym_index, in decimal
//   to       - sym_index, in decimal
//   cnt      - counter, in decimal
//   C/R      - a field indicate whether this is a function call or a return,
//              could be empty if it's just a normal branch.
//
// [Fallthrough] section
// Each line in "Fallthroughs" section contains exactly the same fields as in
// "Branches" section, except the "C/R" field.
//
class Propfile {
public:
  Propfile(const std::string &pName)
      : propfileStrSaver(bpAllocator), propfName(pName), propfStream(),
        allBBMode(false) {}

  // Check whether "outputFile" matches "@" directives in the propeller profile.
  bool matchesOutputFileName(const StringRef outputFile);

  // Read "Symbols" sections in the propeller profile and create
  // symbolOrdinalMap and symbolNameMap.
  bool readSymbols();

  // Find SymbolEntry instance by name.
  SymbolEntry *findSymbol(StringRef symName);

  // Main method of propeller profile processing.
  bool processProfile();

  bool processProfile2();

  // Helper method. Returns true when func or bbIndex.func (when bbIndex is
  // provided) is hot.
  //   func: the function symbole
  //   hotBBSymbols: hot bb symbols index, which is constructed from the hot bbs
  //                 section in the propeller file
  //   bbIndex: an optional StringRef to bbIndex.
  //   bbtt: basic block tag type, when a basic block is a landing pad, it vetos
  //         it's hotness.
  bool
  isHotSymbol(SymbolEntry *func,
              const std::map<std::string, std::set<std::string>> &hotBBSymbols,
              StringRef bbIndex = "",
              SymbolEntry::BBTagTypeEnum bbtt = SymbolEntry::BB_NONE);

  // Helper method - process a symbol line.
  bool processSymbolLine(
      StringRef symLine,
      std::list<std::tuple<uint64_t, uint64_t, StringRef, uint64_t,
                           SymbolEntry::BBTagTypeEnum>> &bbSymbolsToPostProcess,
      const std::map<std::string, std::set<std::string>> &hotBBSymbols);

  // For each function symbol line defintion, this function creates a
  // SymbolEntry instance and places it in symbolOrdinalMap and symbolNameMap.
  // Function symbol defintion line is like below. (See comments at the head of
  // the file)
  //    11     7c     Nmain/alias1/alias2
  //    ^^     ^^      ^^
  //  ordinal  size    name and aliases
  SymbolEntry *createFunctionSymbol(
      uint64_t ordinal, const StringRef &name, SymbolEntry::AliasesTy &&aliases,
      uint64_t size,
      const std::map<std::string, std::set<std::string>> &hotBBSymbols) {
    auto *sym = new SymbolEntry(ordinal, name, std::move(aliases),
                                SymbolEntry::INVALID_ADDRESS, size, false);
    sym->containingFunc = sym;
    symbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(ordinal),
                             std::forward_as_tuple(sym));
    for (auto &a : sym->aliases)
      symbolNameMap[a][""] = sym;

    if (sym->aliases.size() > 1)
      functionsWithAliases.push_back(sym);

    // Function symbols is always hot.
    // sym->hotTag = true;
    sym->hotTag = isHotSymbol(sym, hotBBSymbols);
    return sym;
  }

  // For each bb symbol line defintion, this function creates a
  // SymbolEntry instance and places it in symbolOrdinalMap and symbolNameMap.
  // Function symbol defintion line is like below. (See comments at the head of
  // the file)
  //    12     f      11.1[rlL]
  //    ^^     ^      ^^^^
  //  ordinal  size   func_ordinal.bb_index
  //  [rlL]: optional suffix BBTagTypeEnum: return / landingpad /
  //  return-and-landingpad.
  SymbolEntry *createBasicBlockSymbol(uint64_t ordinal, SymbolEntry *function,
                                      StringRef &bbIndex, uint64_t size,
                                      bool hotTag,
                                      SymbolEntry::BBTagTypeEnum bbtt) {
    // bbIndex is of the form "1", "2", it's a stringref to integer.
    assert(!function->bbTag && function->isFunction());
    auto *sym =
        new SymbolEntry(ordinal, bbIndex, SymbolEntry::AliasesTy(),
                        SymbolEntry::INVALID_ADDRESS, size, true, function);
    // Landing pads are always treated as cold.
    if (bbtt == SymbolEntry::BB_RETURN_AND_LANDING_PAD ||
        bbtt == SymbolEntry::BB_LANDING_PAD)
      sym->hotTag = false;
    else
      sym->hotTag = hotTag;
    sym->bbTagType = bbtt;
    symbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(ordinal),
                             std::forward_as_tuple(sym));
    for (auto &a : function->aliases)
      symbolNameMap[a][bbIndex] = sym;
    return sym;
  }

  void reportParseError(StringRef msg) const;

  llvm::BumpPtrAllocator bpAllocator;
  llvm::UniqueStringSaver propfileStrSaver;
  std::string propfName;
  std::ifstream propfStream;
  // Ordial -> SymbolEntry map. This also owns SymbolEntry instances.
  std::map<uint64_t, std::unique_ptr<SymbolEntry>> symbolOrdinalMap;
  // symbolNameMap is ordered in the following way:
  //   symbolNameMap[foo][""] = functionSymbol;
  //     symbolNameMap[foo]["1"] = fun.bb.1.Symbol;
  //     symbolNameMap[foo]["2"] = fun.bb.2.Symbol;
  //   etc...
  std::map<StringRef, std::map<StringRef, SymbolEntry *>> symbolNameMap;
  std::vector<SymbolEntry *> functionsWithAliases;
  uint64_t lineNo;
  char lineTag;

  std::map<uint64_t, uint64_t> ordinalRemapping;

  bool allBBMode;
};

class Propeller {
public:
  Propeller();
  ~Propeller();

  // Returns true if linker output target matches propeller profile.
  bool checkTarget();
  bool processFiles(std::vector<ObjectView *> &files);
  void processFile(ObjectView *view);
  CFGNode *findCfgNode(uint64_t symbolOrdinal);
  void calculateNodeFreqs();
  std::vector<StringRef> genSymbolOrderingFile();
  void calculateLegacy(std::list<StringRef> &symList,
                       std::list<StringRef>::iterator hotPlaceHolder,
                       std::list<StringRef>::iterator coldPlaceHolder);
  template <class Visitor> void forEachCfgRef(Visitor v) {
    for (auto &p : cfgMap)
      v(*(*(p.second.begin())));
  }

  bool dumpCfgs();

  static ObjectView *createObjectView(const StringRef &vn,
                                      const uint32_t ordinal,
                                      const MemoryBufferRef &fr);

  std::vector<std::unique_ptr<ObjectView>> Views;
  // Same named cfgs may exist in different object files (e.g. weak
  // symbols.)  We always choose symbols that appear earlier on the
  // command line.  Note: implementation is in the .cpp file, because
  // ControlFlowGraph here is an incomplete type.
  struct ObjectViewOrdinalComparator {
    bool operator()(const ControlFlowGraph *a, const ControlFlowGraph *b) const;
  };
  using CfgMapTy =
      std::map<StringRef,
               std::set<ControlFlowGraph *, ObjectViewOrdinalComparator>>;
  CfgMapTy cfgMap;
  std::unique_ptr<Propfile> propf;
  uint32_t processFailureCount; // Number of files that are not processed by
                                // Propeller.
  // We call Propeller::processFile in parallel to create cfgs for
  // each file, after the cfgs are created, each processFile thread
  // then puts cfgs into Propeller::cfgMap (see above). lock is used
  // to guard this Propeller::cfgMap critical section.
  std::mutex lock;

  CodeLayout *propLayout;

#ifdef PROPELLER_PROTOBUF
  std::unique_ptr<lld::propeller::ProtobufPrinter> protobufPrinter;
#endif
};

// When no "-propeller-keep-named-symbols" specified, we remove all bb symbols
// that are hot, and we keep only the first code bb symbol that starts the cold
// code region of the same function. See Below:
// hot:
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
  std::set<StringRef> bbSymbolsToKeep;

  bool shouldKeepBBSymbol(StringRef symName) {
    if (!SymbolEntry::isBBSymbol(symName))
      return true;
    return bbSymbolsToKeep.find(symName) != bbSymbolsToKeep.end();
  }
};

extern PropellerLegacy propLeg;

extern PropellerConfig propConfig;

} // namespace propeller
} // namespace lld
#endif
