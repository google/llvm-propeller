#ifndef PROPELLER_ADDR2CU_H_
#define PROPELLER_ADDR2CU_H_

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Object/ObjectFile.h"

namespace propeller {

// Creates an `llvm::DWARFContext` instance, which can then be used to create
// an `Addr2Cu` instance.
absl::StatusOr<std::unique_ptr<llvm::DWARFContext>> CreateDWARFContext(
    const llvm::object::ObjectFile &obj, absl::string_view dwp_file = "");

// Utility class that gets the module name for a code address with
// the help of debug information.
class Addr2Cu {
 public:
  explicit Addr2Cu(llvm::DWARFContext &dwarf_context)
      : dwarf_context_(dwarf_context) {}

  Addr2Cu(const Addr2Cu &) = delete;
  Addr2Cu &operator=(const Addr2Cu &) = delete;

  Addr2Cu(Addr2Cu &&) = delete;
  Addr2Cu &operator=(Addr2Cu &&) = delete;

  // Returns the file name for the compile unit that contains the given code
  // address. Note: the returned string_view lives as long as `dwarf_context_`.
  absl::StatusOr<absl::string_view> GetCompileUnitFileNameForCodeAddress(
      uint64_t code_address);

 private:
  llvm::DWARFContext &dwarf_context_;
};
}  // namespace propeller
#endif  // PROPELLER_ADDR2CU_H_
