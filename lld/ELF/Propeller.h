#ifndef LLD_ELF_PROPELLER_H
#define LLD_ELF_PROPELLER_H

#include "InputFiles.h"

#include "lld/Common/PropellerCommon.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/StringSaver.h"

#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

using llvm::SmallVector;
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
    SymbolNameMap[Name][""] = Sym;  // See SymbolNameMap comment.
    for (auto &A: Sym->Aliases) {
      SymbolNameMap[A][""] = Sym;
    }
    return Sym;
  }

  SymbolEntry *createBasicBlockSymbol(uint64_t Ordinal, SymbolEntry *Function,
                                      StringRef &BBIndex, uint64_t Size) {
    assert(!Function->isBBSymbol && Function->isFunction);
    auto *Sym =
        new SymbolEntry(Ordinal, BBIndex, SymbolEntry::AliasesTy(),
                        SymbolEntry::INVALID_ADDRESS, Size,
                        llvm::object::SymbolRef::ST_Unknown, true, Function);
    SymbolOrdinalMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(Ordinal),
                             std::forward_as_tuple(Sym));
    SymbolNameMap[Function->Name][BBIndex] = Sym;
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
  template <class Visitor>
  void forEachCfgRef(Visitor V) {
    for (auto &P : CfgMap) {
      V(*(*(P.second.begin())));
    }
  }

  lld::elf::SymbolTable *Symtab;
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
  map<StringRef, set<ELFCfg *, ELFViewOrdinalComparator>> CfgMap;
  unique_ptr<Propfile> Propf;
  // Lock to access / modify global data structure.
  mutex Lock;
};

} // namespace propeller
} // namespace lld
#endif
