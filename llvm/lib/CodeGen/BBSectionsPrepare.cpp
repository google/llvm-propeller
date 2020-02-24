//===-- BBSectionsPrepare.cpp ---=========---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BBSectionsPrepare implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {
struct BBSectionsPrepare : public MachineFunctionPass {
  static char ID;

  BBSectionsPrepare() : MachineFunctionPass(ID) {
    initializeBBSectionsPreparePass(*PassRegistry::getPassRegistry());
  };

  /// This function sorts basic blocks according to the sections in which they
  /// are emitted.  Basic block sections automatically turn on function sections
  /// so the entry block is in the function section.  The other sections that
  /// are created are: 1) Exception section - basic blocks that are landing pads
  /// 2) Cold section - basic blocks that will not have unique sections.
  /// 3) Unique section - one per basic block that is emitted in a unique
  /// section.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!MF.getBBSections())
      return false;

    DenseMap<const MachineBasicBlock *, unsigned> MBBOrder;
    unsigned MBBOrderN = 0;

    SmallSet<unsigned, 4> S = MF.getTarget().getBBSectionsSet(MF.getName());

    bool AllEHPadsAreCold = true;

    for (auto &MBB : MF) {
      // A unique BB section can only be created if this basic block is not
      // used for exception table computations.  Entry basic block cannot
      // start another section because the function starts one already.
      if (MBB.getNumber() == MF.front().getNumber())
        continue;
      // Also, check if this BB is a cold basic block in which case sections
      // are not required with the list option.
      bool isColdBB =
          ((MF.getTarget().getBBSections() == llvm::BasicBlockSection::List) &&
           !S.empty() && !S.count(MBB.getNumber()));
      if (isColdBB) {
        MBB.setColdSection();
      } else if (MBB.isEHPad()) {
        // We handle non-cold basic eh blocks later.
        AllEHPadsAreCold = false;
      } else {
        // Place this MBB in a unique section.  A unique section begins and ends
        // that section.
        MBB.setBeginSection();
        MBB.setEndSection();
      }
      MBBOrder[&MBB] = MBBOrderN++;
    }

    for (auto &MBB : MF) {
      // Handle eh blocks: if all eh pads are cold, we don't need to create a
      // separate section for them and we can group them with the cold section.
      // Otherwise, even if one landing pad is hot, we create a separate section
      // (which will include all landing pads, hot or cold).
      if (MBB.isEHPad()) {
        if (AllEHPadsAreCold)
          MBB.setColdSection();
        else
          MBB.setExceptionSection();
      }

      // With -fbasicblock-sections, fall through blocks must be made
      // explicitly reachable.  Do this after sections is set as
      // unnecessary fallthroughs can be avoided.
      MBB.insertUnconditionalFallthroughBranch();
    }

    // Order : Entry Block, Exception Section, Cold Section,
    // Other Unique Sections.
    auto SectionType = ([&](MachineBasicBlock &X) {
      if (X.getNumber() == MF.front().getNumber())
        return 0;
      if (X.isExceptionSection())
        return 1;
      else if (X.isColdSection())
        return 2;
      return 3;
    });

    MF.sort(([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
      auto TypeX = SectionType(X);
      auto TypeY = SectionType(Y);

      return (TypeX != TypeY) ? TypeX < TypeY : MBBOrder[&X] < MBBOrder[&Y];
    }));

    // Set the basic block that begins or ends every section.  For unique
    // sections, the same basic block begins and ends it.
    MachineBasicBlock *PrevMBB = nullptr;
    for (auto &MBB : MF) {
      // Entry block
      if (MBB.getNumber() == MF.front().getNumber()) {
        PrevMBB = &MBB;
        continue;
      }
      assert(PrevMBB != nullptr && "First block was not a regular block!");
      int TypeP = SectionType(*PrevMBB);
      int TypeT = SectionType(MBB);
      if (TypeP != TypeT) {
        PrevMBB->setEndSection();
        MBB.setBeginSection();
      }
      assert((TypeT != 3 || (MBB.isBeginSection() && MBB.isEndSection())) &&
             "Basic block does not correctly begin or end a section");
      PrevMBB = &MBB;
    }
    PrevMBB->setEndSection();
    return true;
  }
};
} // namespace

char BBSectionsPrepare::ID = 0;
char &llvm::BBSectionsPrepareID = BBSectionsPrepare::ID;
INITIALIZE_PASS(BBSectionsPrepare, "bb-sections-prepare",
                "BB Sections Prepare Pass", false, false)
