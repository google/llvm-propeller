#ifndef LLD_ELF_PROPELLER_H
#define LLD_ELF_PROPELLER_H

#include "InputFiles.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/StringSaver.h"

#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

using llvm::SmallVector;
using llvm::StringRef;

using std::list;
using std::map;
using std::mutex;
using std::pair;
using std::set;
using std::unique_ptr;

namespace lld {
namespace elf {
class SymbolTable;
}

namespace plo {
class ELFCfg;
class ELFCfgNode;
class ELFView;
} // namespace plo

namespace propeller {

using lld::plo::ELFCfg;
using lld::plo::ELFCfgNode;
using lld::plo::ELFView;

struct SymbolEntry {

  using AliasesTy = SmallVector<StringRef, 3>;
  SymbolEntry(uint64_t O, const StringRef &N, AliasesTy &&As, uint64_t A,
              uint64_t S, uint8_t T, bool BB = false,
              SymbolEntry *FuncPtr = nullptr)
      : Ordinal(O), Name(N), Aliases(As), Addr(A), Size(S), Type(T),
        isBBSymbol(BB), ContainingFunc(FuncPtr) {}
  ~SymbolEntry() {}

  uint64_t Ordinal;
  StringRef Name;  // If this is a bb, Name is only the BBIndex part.
  AliasesTy Aliases;
  uint64_t Addr;
  uint64_t Size;
  uint8_t Type;
  bool isBBSymbol;
  SymbolEntry *ContainingFunc;

  // Given a basicblock symbol (e.g. "foo.bb.5"), return bb index "5".
  // Need to be changed if we use another bb label schema.
  StringRef getBBIndex() const {
    assert(isBBSymbol);
    return Name.rsplit('.').second;
  }

  bool containsAddress(uint64_t A) const {
    return Addr <= A && A < Addr + Size;
  }

  bool containsAnotherSymbol(SymbolEntry *O) const {
    if (O->Size == 0) {
      // Note if O's size is 0, we allow O on the end boundary. For example,
      // if foo.bb.4 is at address 0x10. foo is [0x0, 0x10), we then assume
      // foo contains foo.bb.4.
      return this->Addr <= O->Addr && O->Addr <= this->Addr + this->Size;
    }
    return containsAddress(O->Addr) && containsAddress(O->Addr + O->Size - 1);
  }

  bool operator<(const SymbolEntry &Other) const {
    return this->Ordinal < Other.Ordinal;
  }

  bool isFunction() const {
    return this->Type == llvm::object::SymbolRef::ST_Function;
  }

  // This might be changed if we use a different bb name scheme.
  bool isFunctionForBBName(StringRef BBName) {
    if (BBName.startswith(Name))
      return true;
    for (auto N : Aliases) {
      if (BBName.startswith(N))
        return true;
    }
    return false;
  }

  static const uint64_t INVALID_ADDRESS = uint64_t(-1);
};

class Propeller {
public:
  Propeller(lld::elf::SymbolTable *ST)
      : BPAllocator(), PropellerStrSaver(BPAllocator), Symbols(),
        Symtab(ST), Views(), CfgMap() {}
  // Dtor has to be defined in Propeller.cc file. Because at this point, type
  // ELFView is incomplete, it's a forward declared type only. To use this type
  // in unique_ptr, ELFView must be completely defined at the place where
  // unique_ptr is destructed, which is in .cc file.
  ~Propeller() { BPAllocator.Reset(); }

  bool processFiles(std::vector<lld::elf::InputFile *> &Files,
                    StringRef PropellerProf);

  llvm::BumpPtrAllocator BPAllocator;
  llvm::StringSaver PropellerStrSaver;
  list<unique_ptr<SymbolEntry>> Symbols;
  lld::elf::SymbolTable *Symtab;
  struct ELFViewDeleter {
    void operator()(ELFView *V);
  };
  list<unique_ptr<lld::plo::ELFView, ELFViewDeleter>> Views;

  // Same named Cfgs may exist in different object files (e.g. weak
  // symbols.)  We always choose symbols that appear earlier on the
  // command line.  Note: implementation is in the .cpp file, because
  // ELFCfg here is an incomplete type.
  struct ELFViewOrdinalComparator {
    bool operator()(const ELFCfg *A, const ELFCfg *B) const;
  };
  map<StringRef, set<ELFCfg *, ELFViewOrdinalComparator>> CfgMap;

private:
  bool processPropellerProfile(StringRef &PropellerProf);
  void processFile(const pair<elf::InputFile *, uint32_t> &Pair);

  SymbolEntry *createFunctionSymbol(uint64_t Ordinal, const StringRef &Name,
                                    SymbolEntry::AliasesTy &&Aliases,
                                    uint64_t Size) {
    return (*(Symbols.emplace(
                Symbols.end(),
                new SymbolEntry(Ordinal, Name, std::move(Aliases),
                                SymbolEntry::INVALID_ADDRESS, Size,
                                llvm::object::SymbolRef::ST_Function))))
        .get();
  }

  SymbolEntry *createBasicBlockSymbol(uint64_t Ordinal, SymbolEntry *Function,
                                      StringRef &BBIndex, uint64_t Size) {
    assert(!Function->isBBSymbol && Function->isFunction);
    return (*(Symbols.emplace(
                Symbols.end(),
                new SymbolEntry(Ordinal, BBIndex, SymbolEntry::AliasesTy(),
                                SymbolEntry::INVALID_ADDRESS, Size,
                                llvm::object::SymbolRef::ST_Unknown, true,
                                Function))))
        .get();
  }

  // Lock to access / modify global data structure.
  mutex Lock;
};

} // namespace propeller
} // namespace lld
#endif
