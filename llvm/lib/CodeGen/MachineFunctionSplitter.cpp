//===-- FEntryInsertion.cpp - Patchable prologues for LLVM -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TODO(snehasishk): Update description.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/BasicBlockSectionUtils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

namespace {

class MachineFunctionSplitter : public MachineFunctionPass {
public:
  static char ID;
  MachineFunctionSplitter() : MachineFunctionPass(ID) {
    initializeMachineFunctionSplitterPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Machine Function Splitter Transformation";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &F) override;
};
} // end anonymous namespace

bool MachineFunctionSplitter::runOnMachineFunction(MachineFunction &MF) {
  // We only target functions with profile data.
  if (!MF.getFunction().hasProfileData()) {
    return false;
  }

  // We don't want to proceed further for cold functions
  // or functions of unknown hotness. Lukewarm functions have no prefix.
  Optional<StringRef> SectionPrefix = MF.getFunction().getSectionPrefix();
  if (SectionPrefix.hasValue() &&
      (SectionPrefix.getValue().equals(".unlikely") ||
       SectionPrefix.getValue().equals(".unknown"))) {
    return false;
  }

  MF.RenumberBlocks();
  MF.setBBSectionsType(BasicBlockSection::Preset);

  auto *MBFI = &getAnalysis<MachineBlockFrequencyInfo>();

  for (auto &MBB : MF) {
    if (MBB.isEHPad()) {
      MBB.setSectionID(MBBSectionID::ExceptionSectionID);
    } else {
      Optional<uint64_t> Count = MBFI->getBlockProfileCount(&MBB);
      bool HasCount = (Count.hasValue() && Count.getValue() > 0);
      if (!(MBB.pred_empty() || HasCount)) {
        MBB.setSectionID(MBBSectionID::ColdSectionID);
      }
    }
  }

  auto Comparator = [](const MachineBasicBlock &X, const MachineBasicBlock &Y) {
    return X.getSectionID().Type < Y.getSectionID().Type;
  };
  llvm::sortBasicBlocksAndUpdateBranches(MF, Comparator);

  return true;
}

void MachineFunctionSplitter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineModuleInfoWrapperPass>();
  AU.addRequired<MachineBlockFrequencyInfo>();
}

char MachineFunctionSplitter::ID = 0;
INITIALIZE_PASS(MachineFunctionSplitter, "machine-function-splitter",
                "Split machine functions using profile information", false,
                false)

MachineFunctionPass *llvm::createMachineFunctionSplitterPass() {
  return new MachineFunctionSplitter();
}
