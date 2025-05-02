// Copyright 2024 The Propeller Authors.
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

#include "propeller/addr2cu.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Object/ObjectFile.h"

namespace propeller {

absl::StatusOr<std::unique_ptr<llvm::DWARFContext>> CreateDWARFContext(
    const llvm::object::ObjectFile &obj, absl::string_view dwp_file) {
  std::unique_ptr<llvm::DWARFContext> dwarf_context =
      llvm::DWARFContext::create(
          obj, llvm::DWARFContext::ProcessDebugRelocations::Process,
          /*const LoadedObjectInfo *L=*/nullptr, std::string(dwp_file));
  CHECK(dwarf_context != nullptr);
  if (dwp_file.empty() &&
      absl::c_any_of(dwarf_context->compile_units(),
                     [](std::unique_ptr<llvm::DWARFUnit> &cu) {
                       return cu->getUnitDIE().getTag() ==
                              llvm::dwarf::DW_TAG_skeleton_unit;
                     })) {
    return absl::FailedPreconditionError(
        "skeleton unit found without a corresponding dwp file");
  }
  if (!dwarf_context->getNumCompileUnits()) {
    return absl::FailedPreconditionError(
        "no compilation unit found, binary must be built with debuginfo");
  }
  return dwarf_context;
}

absl::StatusOr<absl::string_view> Addr2Cu::GetCompileUnitFileNameForCodeAddress(
    uint64_t code_address) const {
  llvm::DWARFCompileUnit *unit =
      dwarf_context_.getCompileUnitForCodeAddress(code_address);
  if (unit == nullptr) {
    return absl::FailedPreconditionError(
        absl::StrFormat("no compile unit found on address 0x%x", code_address));
  }
  llvm::DWARFDie die = unit->getNonSkeletonUnitDIE();
  std::optional<llvm::DWARFFormValue> form_value =
      die.findRecursively({llvm::dwarf::DW_AT_name});
  llvm::StringRef name = llvm::dwarf::toStringRef(form_value, "");
  return absl::string_view(name.data(), name.size());
}
}  // namespace propeller
