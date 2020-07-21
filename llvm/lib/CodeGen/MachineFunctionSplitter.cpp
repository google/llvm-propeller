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

static cl::opt<bool> HotFunctionsOnly("mfs-hot-funcs-only", cl::Hidden,
                                      cl::desc("Split hot functions only."),
                                      cl::init(true));

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

bool isColdBlock(const MachineBasicBlock &MBB,
                 const MachineBlockFrequencyInfo *MBFI) {
  Optional<uint64_t> Count = MBFI->getBlockProfileCount(&MBB);
  return !(Count.hasValue() && Count.getValue() > 0);
}

bool MachineFunctionSplitter::runOnMachineFunction(MachineFunction &MF) {
  auto *MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
  // We don't want to proceed further for cold functions
  // or functions of unknown hotness.
  Optional<StringRef> SectionPrefix = MF.getFunction().getSectionPrefix();
  if (!SectionPrefix.hasValue() ||
      SectionPrefix.getValue().equals(".unlikely") ||
      SectionPrefix.getValue().equals(".unknown")) {
    return false;
  }

  // Further constrain the functions we split to hot functions only if the flag
  // is set.
  if (HotFunctionsOnly && !SectionPrefix.getValue().equals(".hot")) {
    return false;
  }

  MF.RenumberBlocks();
  MF.setBBSectionsType(BasicBlockSection::Preset);

  for (auto &MBB : MF) {
    if (MBB.pred_empty() || MBB.succ_empty()) {
      continue;
    } else if (MBB.isEHPad()) {
      MBB.setSectionID(MBBSectionID::ExceptionSectionID);
    } else if (isColdBlock(MBB, MBFI)) {
      MBB.setSectionID(MBBSectionID::ColdSectionID);
    }
  }

  // All cold blocks are grouped together at the end.
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
