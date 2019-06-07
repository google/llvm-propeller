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

struct SymbolEntry {

  using AliasesTy = SmallVector<StringRef, 3>;
  SymbolEntry(uint64_t O, const StringRef &N, AliasesTy &&As, uint64_t A,
              uint64_t S, uint8_t T, bool BB = false,
              SymbolEntry *FuncPtr = nullptr)
      : Ordinal(O), Name(N), Aliases(As), Addr(A), Size(S), Type(T),
        BBTag(BB), ContainingFunc(FuncPtr) {}
  ~SymbolEntry() {}

  uint64_t Ordinal;
  StringRef Name;  // If this is a bb, Name is only the BBIndex part.
  AliasesTy Aliases;
  uint64_t Addr;
  uint64_t Size;
  uint8_t Type;
  bool BBTag;
  SymbolEntry *ContainingFunc;

  // Given a basicblock symbol (e.g. "foo.bb.5"), return bb index "5".
  // Need to be changed if we use another bb label schema.
  StringRef getBBIndex() const {
    assert(BBTag);
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

  string getFQN() const {
    if (BBTag) {
      return ContainingFunc->Name.str() + ".bb." + Name.str();
    }
    return Name.str();
  }

  static bool isBBSymbol(const StringRef &SymName,
                         StringRef *FuncName = nullptr,
                         StringRef *BBIndex = nullptr) {
    auto R = SymName.split(".bb.");
    if (R.second.empty())
      return false;
    for (const char *I = R.second.data(),
                    *J = R.second.data() + R.second.size();
         I != J; ++I)
      if (*I < '0' || *I > '9')
        return false;
    if (FuncName)
      *FuncName = R.first;
    if (BBIndex)
      *BBIndex = R.second;
    return true;
  }

  static const uint64_t INVALID_ADDRESS = uint64_t(-1);
};

}
}
#endif
