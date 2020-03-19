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
#include <sstream>
#include <iterator>

using llvm::SmallSet;
using llvm::StringMap;
using llvm::StringRef;
using namespace llvm;

namespace {

class BBSectionsPrepare : public MachineFunctionPass {
public:
  static char ID;
  StringMap<SmallVector<SmallVector<unsigned, 4>, 2>> BBSectionsList;
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
  //if (MBB.sameSection(Fallthrough))
  //  return;

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
    const StringMap<SmallVector<SmallVector<unsigned, 4>,2>> &BBSectionsList) {
  SmallVector<SmallVector<unsigned, 4>,2> S = BBSectionsList.lookup(MF.getName());

  std::map<unsigned, std::pair<unsigned, unsigned>> BBIndexMap;

  for(unsigned i=1; i<S.size(); ++i)
    if (S[i].front() == 0) {
      S[0].swap(S[i]);
      break;
    }

  errs() << "ASSIGN SECTION: " << MF.getName() << "\n";
  for(unsigned i=0; i<S.size(); ++i) {
    for(unsigned j=0; j<S[i].size(); ++j)
      errs() << S[i][j] << " -> ";
    errs() << "\n";
  }


  for(unsigned i=0; i<S.size(); ++i)
    for(unsigned j=0; j<S[i].size(); ++j)
      BBIndexMap.emplace(S[i][j], std::make_pair(i, j));

  bool HasHotEHPads = false;

  for (auto &MBB : MF) {
    // Entry basic block cannot start another section because the function
    // starts one already.
    //if (MBB.getNumber() == MF.front().getNumber()) {
    //  MBB.setSectionType(MachineBasicBlockSection::MBBS_Entry);
    //  continue;
    //}
    // Check if this BB is a cold basic block.  With the list option, all cold
    // basic blocks can be clustered in a single cold section.
    // All Exception landing pads must be in a single section.  If all the
    // landing pads are cold, it can be kept in the cold section.  Otherwise, we
    // create a separate exception section.
    bool isColdBB = ((MF.getTarget().getBBSectionsType() ==
                      llvm::BasicBlockSection::List) &&
                     !S.empty() && !BBIndexMap.count(MBB.getNumber()));
    if (isColdBB) {
      MBB.setSectionType(llvm::MBBS_Cold);
    } else if (MBB.isEHPad()) {
      // We handle non-cold basic eh blocks later.
      HasHotEHPads = true;
    } else {
      // Place this MBB in a unique section.  A unique section begins and ends
      // that section by definition.
      MBB.setSectionType(BBIndexMap.at(MBB.getNumber()).first);
      //MBB.setSectionType(MachineBasicBlockSection::MBBS_Unique);
    }
  }

//  errs() << "BBIndexMap: ";
 // for(auto &elem: BBIndexMap)
  //  errs()  << "[ " << elem.first << " --> " << " ( " << elem.second.first << " : " << elem.second.second << " )   ";
  //errs() << "\n";

  // If some EH Pads are not cold then we move all EH Pads to the exception
  // section as we require that all EH Pads be in a single section.
  if (HasHotEHPads) {
    std::for_each(MF.begin(), MF.end(), [&](MachineBasicBlock &MBB) {
      if (MBB.isEHPad())
        MBB.setSectionType(llvm::MBBS_Exception);
    });
  }

  /*
  errs() << "INITIAL ORDER : ";
  for (auto &MBB : MF)
    errs() << MBB.getNumber() << " -> ";
  errs() << "\n";
  */

  bool EntryCold = MF.front().getSectionType() == llvm::MBBS_Cold;

  for (auto &MBB : MF) {
    // With -fbasicblock-sections, fall through blocks must be made
    // explicitly reachable.  Do this after sections is set as
    // unnecessary fallthroughs can be avoided.
    insertUnconditionalFallthroughBranch(MBB);
  }


  MF.sort(([&EntryCold, &BBIndexMap](MachineBasicBlock &X, MachineBasicBlock &Y) {
    auto XSectionType = X.getSectionType();
    auto YSectionType = Y.getSectionType();
    if (XSectionType == YSectionType)
      return XSectionType < 0 ? X.getNumber() < Y.getNumber() : BBIndexMap.at(X.getNumber()).second < BBIndexMap.at(Y.getNumber()).second;
    if (XSectionType == llvm::MBBS_Cold || YSectionType == llvm::MBBS_Cold)
        return EntryCold ? XSectionType == llvm::MBBS_Cold : YSectionType == llvm::MBBS_Cold;
    if (XSectionType < 0 || YSectionType < 0)
      return YSectionType < XSectionType;
    return XSectionType < YSectionType;
  }));

  /*
  int stype = -3;
  SmallSet<int, 16> stypes;
  errs() << "SORTED ORDER : ";
  for (auto &MBB : MF) {
    errs() << MBB.getNumber() << ":" << MBB.getSectionType() << " -> ";
    if (MBB.getSectionType() != stype) {
      stype = MBB.getSectionType();
      auto R = stypes.insert(stype);
      if (!R.second)
        report_fatal_error("BAD ONE: HERE:");
    }
  }
  errs() << "\n";
  */

  // Compute the Section Range of cold and exception basic blocks.  Find the
  // first and last block of each range.
  auto SectionRange =
      ([&](int SectionType) -> std::pair<int, int> {
        auto MBBP = std::find_if(MF.begin(), MF.end(),
                                 [&](MachineBasicBlock &MBB) -> bool {
                                   return MBB.getSectionType() == SectionType;
                                 });
        if (MBBP == MF.end())
          return std::make_pair(-1, -1);

        auto MBBQ = std::find_if(MF.rbegin(), MF.rend(),
                                 [&](MachineBasicBlock &MBB) -> bool {
                                   return MBB.getSectionType() == SectionType;
                                 });
        assert(MBBQ != MF.rend() && "Section begin not found!");
        for (auto it=MBBP; it->getNumber() != MBBQ->getNumber(); ++it)
          if(it->getSectionType() != SectionType)
            report_fatal_error("Not right for: " + MF.getName());
        return std::make_pair(MBBP->getNumber(), MBBQ->getNumber());
      });

  for(int i=-2; i< ((int)S.size()); ++i) {
    auto r = SectionRange(i);
    if (r.first != -1)
      MF.setSectionRange(i, r);
  }


  /*
  errs() << "SECTION RANGES:\n";
  for(auto &elem: MF.SectionRanges)
    errs() << elem.first << " : " << "( " << elem.second.first << " --> " << elem.second.second << ")\n";
    */

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  for (auto &MBB : MF)
    if (!MF.isSectionEndMBB(MBB.getNumber())) {
      Cond.clear();
      MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.
      if (!TII->analyzeBranch(MBB, TBB, FBB, Cond))
        MBB.updateTerminator();
    }
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
                              StringMap<SmallVector<SmallVector<unsigned, 4>, 2>> &bbMap) {
  if (profFileName.empty())
    return false;

  auto MbOrErr = MemoryBuffer::getFile(profFileName);
  if (MbOrErr.getError())
    return false;

  MemoryBuffer &Buffer = *MbOrErr.get();
  line_iterator LineIt(Buffer, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  StringMap<SmallVector<SmallVector<unsigned, 4>, 2>>::iterator fi = bbMap.end();

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef s(*LineIt);
    if (s[0] == '@')
      continue;
    // Check for the leading "!"
    if (!s.consume_front("!") || s.empty())
      break;
    // Check for second "!" which encodes basic block ids.
    if (s.consume_front("!")) {
      if (fi != bbMap.end()) {
        std::istringstream iss(s.str());
        std::vector<std::string> results((std::istream_iterator<std::string>(iss)),
                                          std::istream_iterator<std::string>());
        if(!results.empty())
          fi->second.emplace_back();
        for (auto& bbIndexStr : results) {
          unsigned bbIndex;
          if (StringRef(bbIndexStr).getAsInteger(10, bbIndex)) {
            errs() << "COULD NOt turn this into an int: '" << bbIndexStr << "'\n";
            return false;
          }
          fi->second.back().push_back(bbIndex);
        }
      } else
        return false;
    } else {
      // Start a new function.
      auto R = bbMap.try_emplace(s.split('/').first);
      fi = R.first;
      //fi->second.emplace_back();
      //assert(R.second);
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
