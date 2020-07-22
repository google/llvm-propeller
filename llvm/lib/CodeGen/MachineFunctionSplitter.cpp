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
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "machine-function-splitter"
STATISTIC(NumPostDomsAdded, "Number of post-dominated blocks added.");

static cl::opt<bool> HotFunctionsOnly("mfs-hot-funcs-only", cl::Hidden,
                                      cl::desc("Split hot functions only."),
                                      cl::init(false));

static cl::opt<bool> IncludePostDominators(
    "mfs-include-post-dominators", cl::Hidden,
    cl::desc("Include post-dominators of the included blocks."),
    cl::init(false));

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
  // auto *PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();

  // We don't want to proceed further for cold functions
  // or functions of unknown hotness. Lukewarm functions have no prefix.
  Optional<StringRef> SectionPrefix = MF.getFunction().getSectionPrefix();
  if (SectionPrefix.hasValue() &&
      (SectionPrefix.getValue().equals(".unlikely") ||
       SectionPrefix.getValue().equals(".unknown"))) {
    return false;
  }

  // Further constrain the functions we split to hot functions only if the flag
  // is set.
  if (HotFunctionsOnly && !SectionPrefix.getValue().equals(".hot")) {
    return false;
  }

  MF.RenumberBlocks();
  MF.setBBSectionsType(BasicBlockSection::Preset);

  auto *MBFI = &getAnalysis<MachineBlockFrequencyInfo>();

  SmallSet<const MachineBasicBlock *, 16> RetainedBlocks, SplitBlocks;
  for (auto &MBB : MF) {
    Optional<uint64_t> Count = MBFI->getBlockProfileCount(&MBB);
    // llvm::errs() << "Block: " << MBB.getNumber() << " Count: " << Count <<
    // "\n";
    if (Count.hasValue() && Count.getValue() > 0) {
      RetainedBlocks.insert(&MBB);
    } else if (MBB.pred_empty()) { // Entry block is always retained.
      RetainedBlocks.insert(&MBB);
    } else {
      SplitBlocks.insert(&MBB);
    }
  }

  if (IncludePostDominators) {
    auto *MPDT = &getAnalysis<MachinePostDominatorTree>();

    SmallVector<const MachineBasicBlock *, 4> PostDominatedBlocks;
    for (const auto *A : SplitBlocks) {
      for (const auto *B : RetainedBlocks) {
        if (MPDT->dominates(A, B)) {
          PostDominatedBlocks.push_back(A);
        }
      }
    }

    llvm::errs() << "MFS: " << RetainedBlocks.size() << "/"
                 << SplitBlocks.size() << "/" << PostDominatedBlocks.size()
                 << " " << MF.getName() << "\n";

    NumPostDomsAdded += PostDominatedBlocks.size();
    RetainedBlocks.insert(PostDominatedBlocks.begin(),
                          PostDominatedBlocks.end());
  }

  for (auto &MBB : MF) {
    if (MBB.isEHPad()) {
      MBB.setSectionID(MBBSectionID::ExceptionSectionID);
    } else if (!RetainedBlocks.count(&MBB)) {
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
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  AU.addRequired<MachinePostDominatorTree>();
}

char MachineFunctionSplitter::ID = 0;
INITIALIZE_PASS(MachineFunctionSplitter, "machine-function-splitter",
                "Split machine functions using profile information", false,
                false)

MachineFunctionPass *llvm::createMachineFunctionSplitterPass() {
  return new MachineFunctionSplitter();
}
