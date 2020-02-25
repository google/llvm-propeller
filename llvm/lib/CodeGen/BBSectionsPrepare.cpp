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
// The purpose of this pass is to assign sections to basic blocks when
// -fbasicblock-sections= option is used.  Exception landing pad blocks are
// specially handled by grouping them in a single section.  Further, with
// profile information only the subset of basic blocks with profiles are placed
// in a separate section and the rest are grouped in a cold section.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetMachine.h"

#include <string>

using llvm::SmallSet;
using llvm::StringMap;
using llvm::StringRef;
using namespace llvm;

namespace {

class BBSectionsPrepare : public MachineFunctionPass {
public:
  static char ID;
  StringMap<SmallSet<unsigned, 4>> BBSectionsList;
  std::string ProfileFileName;

  BBSectionsPrepare(const std::string &ProfileFile)
      : MachineFunctionPass(ID), ProfileFileName(ProfileFile){};

  StringRef getPassName() const override {
    return "Basic Block Sections Analysis";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Read profiles of basic blocks if available here.
  bool doInitialization(Module &M) override;

  /// Identify basic blocks that need separate sections and prepare to emit them
  /// accordingly.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char BBSectionsPrepare::ID = 0;

// This inserts an unconditional branch at the end of MBB to the next basic
// block S if and only if the control-flow implicitly falls through from MBB to
// S and S and MBB belong to different sections.  This is necessary with basic
// block sections as MBB and S could be potentially reordered.
static void insertUnconditionalFallthroughBranch(MachineBasicBlock &MBB) {
  MachineBasicBlock *Fallthrough = MBB.getFallThrough();

  if (Fallthrough == nullptr)
    return;

  // If this basic block and the Fallthrough basic block are in the same
  // section then do not insert the jump.
  if (MBB.sameSection(Fallthrough))
    return;

  const TargetInstrInfo *TII = MBB.getParent()->getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;

  // If a branch to the fall through block already exists, return.
  if (!TII->analyzeBranch(MBB, TBB, FBB, Cond) &&
      (TBB == Fallthrough || FBB == Fallthrough)) {
    return;
  }

  Cond.clear();
  DebugLoc DL = MBB.findBranchDebugLoc();
  TII->insertBranch(MBB, Fallthrough, nullptr, Cond, DL);
}

/// This function sorts basic blocks according to the sections in which they are
/// emitted.  Basic block sections automatically turn on function sections so
/// the entry block is in the function section.  The other sections that are
/// created are:
/// 1) Exception section - basic blocks that are landing pads
/// 2) Cold section - basic blocks that will not have unique sections.
/// 3) Unique section - one per basic block that is emitted in a unique section.
static bool assignSectionsAndSortBasicBlocks(
    MachineFunction &MF,
    const StringMap<SmallSet<unsigned, 4>> &BBSectionsList) {
  SmallSet<unsigned, 4> S = BBSectionsList.lookup(MF.getName());

  bool HasHotEHPads = false;

  for (auto &MBB : MF) {
    // Entry basic block cannot start another section because the function
    // starts one already.
    if (MBB.getNumber() == MF.front().getNumber()) {
      MBB.setSectionType(MachineBasicBlockSection::MBBS_Entry);
      continue;
    }
    // Check if this BB is a cold basic block.  With the list option, all cold
    // basic blocks can be clustered in a single cold section.
    // All Exception landing pads must be in a single section.  If all the
    // landing pads are cold, it can be kept in the cold section.  Otherwise, we
    // create a separate exception section.
    bool isColdBB = ((MF.getTarget().getBBSectionsType() ==
                      llvm::BasicBlockSection::List) &&
                     !S.empty() && !S.count(MBB.getNumber()));
    if (isColdBB) {
      MBB.setSectionType(MachineBasicBlockSection::MBBS_Cold);
    } else if (MBB.isEHPad()) {
      // We handle non-cold basic eh blocks later.
      HasHotEHPads = true;
    } else {
      // Place this MBB in a unique section.  A unique section begins and ends
      // that section by definition.
      MBB.setSectionType(MachineBasicBlockSection::MBBS_Unique);
    }
  }

  // If some EH Pads are not cold then we move all EH Pads to the exception
  // section as we require that all EH Pads be in a single section.
  if (HasHotEHPads) {
    std::for_each(MF.begin(), MF.end(), [&](MachineBasicBlock &MBB) {
      if (MBB.isEHPad())
        MBB.setSectionType(MachineBasicBlockSection::MBBS_Exception);
    });
  }

  for (auto &MBB : MF) {
    // With -fbasicblock-sections, fall through blocks must be made
    // explicitly reachable.  Do this after sections is set as
    // unnecessary fallthroughs can be avoided.
    insertUnconditionalFallthroughBranch(MBB);
  }

  MF.sort(([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
    unsigned TypeX = X.getSectionType();
    unsigned TypeY = Y.getSectionType();

    return (TypeX != TypeY) ? TypeX < TypeY : X.getNumber() < Y.getNumber();
  }));

  // Compute the Section Range of cold and exception basic blocks.  Find the
  // first and last block of each range.
  auto SectionRange =
      ([&](llvm::MachineBasicBlockSection S) -> std::pair<int, int> {
        auto MBBP = std::find_if(MF.begin(), MF.end(),
                                 [&](MachineBasicBlock &MBB) -> bool {
                                   return MBB.getSectionType() == S;
                                 });
        if (MBBP == MF.end())
          return std::make_pair(-1, -1);

        auto MBBQ = std::find_if(MF.rbegin(), MF.rend(),
                                 [&](MachineBasicBlock &MBB) -> bool {
                                   return MBB.getSectionType() == S;
                                 });
        assert(MBBQ != MF.rend() && "Section begin not found!");
        return std::make_pair(MBBP->getNumber(), MBBQ->getNumber());
      });

  MF.setSectionRange(MBBS_Cold, SectionRange(MBBS_Cold));
  MF.setSectionRange(MBBS_Exception, SectionRange(MBBS_Exception));
  return true;
}

bool BBSectionsPrepare::runOnMachineFunction(MachineFunction &MF) {
  auto BBSectionsType = MF.getTarget().getBBSectionsType();
  assert(BBSectionsType != BasicBlockSection::None &&
         "BB Sections not enabled!");

  // Renumber blocks before sorting them for basic block sections.  This is
  // useful during sorting, basic blocks in the same section will retain the
  // default order.  This renumbering should also be done for basic block
  // labels to match the profiles with the correct blocks.
  MF.RenumberBlocks();

  if (BBSectionsType == BasicBlockSection::Labels) {
    MF.setBBSectionsType(BBSectionsType);
    MF.createBBLabels();
  }

  if (BBSectionsType == BasicBlockSection::Labels ||
      (BBSectionsType == BasicBlockSection::List &&
       BBSectionsList.find(MF.getName()) == BBSectionsList.end()))
    return true;

  MF.setBBSectionsType(BBSectionsType);
  MF.createBBLabels();
  assignSectionsAndSortBasicBlocks(MF, BBSectionsList);

  return true;
}

// Basic Block Sections can be enabled for a subset of machine basic blocks.
// This is done by passing a file containing names of functions for which basic
// block sections are desired.  Additionally, machine basic block ids of the
// functions can also be specified for a finer granularity.
// A file with basic block sections for all of function main and two blocks for
// function foo looks like this:
// ----------------------------
// list.txt:
// !main
// !foo
// !!2
// !!4
static bool getBBSectionsList(StringRef profFileName,
                              StringMap<SmallSet<unsigned, 4>> &bbMap) {
  if (profFileName.empty())
    return false;

  auto MbOrErr = MemoryBuffer::getFile(profFileName);
  if (MbOrErr.getError())
    return false;

  MemoryBuffer &Buffer = *MbOrErr.get();
  line_iterator LineIt(Buffer, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  StringMap<SmallSet<unsigned, 4>>::iterator fi = bbMap.end();

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef s(*LineIt);
    if (s[0] == '@')
      continue;
    // Check for the leading "!"
    if (!s.consume_front("!") || s.empty())
      break;
    // Check for second "!" which encodes basic block ids.
    if (s.consume_front("!")) {
      if (fi != bbMap.end())
        fi->second.insert(std::stoi(s.str()));
      else
        return false;
    } else {
      // Start a new function.
      auto R = bbMap.try_emplace(s.split('/').first);
      fi = R.first;
      assert(R.second);
    }
  }
  return true;
}

bool BBSectionsPrepare::doInitialization(Module &M) {
  if (!ProfileFileName.empty())
    getBBSectionsList(ProfileFileName, BBSectionsList);
  return true;
}

void BBSectionsPrepare::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineModuleInfoWrapperPass>();
}

MachineFunctionPass *
llvm::createBBSectionsPreparePass(const std::string &ProfileFile) {
  return new BBSectionsPrepare(ProfileFile);
}
