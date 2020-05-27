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

using namespace llvm;

extern cl::opt<bool> TreatUnknownAsCold;

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

bool isColdBlock(const MachineBasicBlock &MBB, ProfileSummaryInfo *PSI,
                 const MachineBlockFrequencyInfo *MBFI) {
  auto Count = MBFI->getBlockProfileCount(&MBB);
  return Count && PSI->isColdCount(*Count);
}

bool MachineFunctionSplitter::runOnMachineFunction(MachineFunction &MF) {
  auto *MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
  auto *PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();

  // For now we only target functions with profile coverage. In the future we
  // can extend this to use existing heuristics to identify cold blocks.
  if (!PSI->hasProfileSummary()) {
    return false;
  }

  // We don't want to split funciton which are marked cold already. Based
  // on the TreatUnknownAsCold flag below it may move blocks from unlikely
  // to unknown.
  if (PSI->isFunctionEntryCold(&MF.getFunction())) {
    return false;
  }

  MF.RenumberBlocks();
  for (auto &MBB : MF) {
    if (!MBB.pred_empty() && isColdBlock(MBB, PSI, MBFI)) {
      MBB.setSectionID( TreatUnknownAsCold ?
        MBBSectionID::ColdSectionID : MBBSectionID::UnknownSectionID);
    }
  }

  MF.setBBSectionsType(BasicBlockSection::Preset);
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
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
}

char MachineFunctionSplitter::ID = 0;
INITIALIZE_PASS(MachineFunctionSplitter, "machine-function-splitter",
                "Split machine functions using profile information", false,
                false)

MachineFunctionPass *llvm::createMachineFunctionSplitterPass() {
  return new MachineFunctionSplitter();
}
