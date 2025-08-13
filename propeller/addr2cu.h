// Copyright 2025 The Propeller Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
    const llvm::object::ObjectFile& obj, absl::string_view dwp_file = "");

// Utility class that gets the module name for a code address with
// the help of debug information.
class Addr2Cu {
 public:
  explicit Addr2Cu(llvm::DWARFContext& dwarf_context)
      : dwarf_context_(dwarf_context) {}

  Addr2Cu(const Addr2Cu&) = delete;
  Addr2Cu& operator=(const Addr2Cu&) = delete;

  Addr2Cu(Addr2Cu&&) = delete;
  Addr2Cu& operator=(Addr2Cu&&) = delete;

  // Returns the file name for the compile unit that contains the given code
  // address. Note: the returned string_view lives as long as `dwarf_context_`.
  absl::StatusOr<absl::string_view> GetCompileUnitFileNameForCodeAddress(
      uint64_t code_address) const;

 private:
  llvm::DWARFContext& dwarf_context_;
};
}  // namespace propeller
#endif  // PROPELLER_ADDR2CU_H_
