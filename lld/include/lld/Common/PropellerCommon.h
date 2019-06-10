#ifndef LLD_ELF_PROPELLER_COMMON_H
#define LLD_ELF_PROPELLER_COMMON_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"

using llvm::SmallVector;
using llvm::StringRef;

#include <string>

using std::string;

namespace lld {
namespace propeller {

// This data structure is shared between lld propeller component and
// create_llvm_prof.
// The basic block symbols are encoded in this way:
//    index.'bb'.function_name
struct SymbolEntry {

  using AliasesTy = SmallVector<StringRef, 3>;
  SymbolEntry(uint64_t O, const StringRef &N, AliasesTy &&As, uint64_t A,
              uint64_t S, uint8_t T, bool BB = false,
              SymbolEntry *FuncPtr = nullptr)
      : Ordinal(O), Name(N), Aliases(As), Addr(A), Size(S), Type(T),
        BBTag(BB), ContainingFunc(FuncPtr) {}
  ~SymbolEntry() {}

  uint64_t Ordinal;
  // For a function symbol, it's the full name. For a bb symbol this is only the
  // bbindex part.
  StringRef Name;
  AliasesTy Aliases;
  uint64_t Addr;
  uint64_t Size;
  uint8_t Type;    // Of type: llvm::objet::SymbolRef::Type.
  bool BBTag;      // Whether this is a basic block section symbol.
  // For BBTag symbols, this is the containing fuction pointer, for
  // a normal function symbol, this points to itself.
  SymbolEntry *ContainingFunc;

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
  
  bool isFunctionForBBName(StringRef BBName) {
    auto A = BBName.split(".bb.");
    if (A.second == Name)
      return true;
    for (auto N : Aliases)
      if (A.second == N)
        return true;
    return false;
  }

  static std::string toCompactBBName(StringRef UnifiedBBName) {
    StringRef FName, BName;
    if (isBBSymbol(UnifiedBBName, &FName, &BName)) {
      return FName.str() + ".bb." + std::to_string(BName.size());
    }
    return "";
  }

  static bool isBBSymbol(const StringRef &SymName,
                         StringRef *FuncName = nullptr,
                         StringRef *BBIndex = nullptr) {
    if (SymName.empty())
      return false;
    auto R = SymName.split(".bb.");
    if (R.second.empty())
      return false;
    for (auto *I = R.first.bytes_begin(), *J = R.first.bytes_end(); I != J; ++I)
      if (*I != '1') return false;
    if (FuncName)
      *FuncName = R.second;
    if (BBIndex)
      *BBIndex = R.first;
    return true;
  }

  static const uint64_t INVALID_ADDRESS = uint64_t(-1);
};

}
}
#endif
