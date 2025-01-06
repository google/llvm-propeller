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

#ifndef PROPELLER_MINI_DISASSEMBLER_H_
#define PROPELLER_MINI_DISASSEMBLER_H_

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/ObjectFile.h"

namespace propeller {
class MiniDisassembler {
 public:
  // Creates a MiniDisassembler for `object_file`. Does not take ownership of
  // `object_file`, which must point to a valid object that outlives the
  // `MiniDisassembler`.
  static absl::StatusOr<absl::Nonnull<std::unique_ptr<MiniDisassembler>>>
  Create(const llvm::object::ObjectFile *object_file);

  MiniDisassembler(const MiniDisassembler &) = delete;
  MiniDisassembler(MiniDisassembler &&) = delete;

  MiniDisassembler &operator=(const MiniDisassembler &) = delete;
  MiniDisassembler &operator=(MiniDisassembler &&) = delete;

  absl::StatusOr<llvm::MCInst> DisassembleOne(uint64_t binary_address);
  bool MayAffectControlFlow(const llvm::MCInst &inst);
  llvm::StringRef GetInstructionName(const llvm::MCInst &inst) const;
  absl::StatusOr<bool> MayAffectControlFlow(uint64_t binary_address);

 private:
  explicit MiniDisassembler(const llvm::object::ObjectFile *object_file)
      : object_file_(object_file) {}

  const llvm::object::ObjectFile *object_file_;
  std::unique_ptr<const llvm::MCRegisterInfo> mri_;
  std::unique_ptr<const llvm::MCAsmInfo> asm_info_;
  std::unique_ptr<const llvm::MCSubtargetInfo> sti_;
  std::unique_ptr<const llvm::MCInstrInfo> mii_;
  std::unique_ptr<llvm::MCContext> ctx_;
  std::unique_ptr<const llvm::MCInstrAnalysis> mia_;
  std::unique_ptr<const llvm::MCDisassembler> disasm_;
};
}  // namespace propeller

#endif  // PROPELLER_MINI_DISASSEMBLER_H_
