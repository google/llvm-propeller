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
  StringMap<StringRef> FuncAliases;
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
// S. This is necessary with basic block sections as MBB and S could be potentially reordered.
static void insertUnconditionalFallthroughBranch(MachineBasicBlock &MBB) {
  MachineBasicBlock *Fallthrough = MBB.getFallThrough();

  if (Fallthrough == nullptr)
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
    const StringMap<SmallVector<SmallVector<unsigned, 4>,2>> &BBSectionsList,
    const StringMap<StringRef> &FuncAliases) {
  auto R = FuncAliases.find(MF.getName());
  auto FuncName = R == FuncAliases.end() ? MF.getName() : R->second ;
  SmallVector<SmallVector<unsigned, 4>,2> S = BBSectionsList.lookup(FuncName);

  std::map<unsigned, std::pair<unsigned, unsigned>> BBIndexMap;

  for(unsigned i=0; i<S.size(); ++i)
    for(unsigned j=0; j<S[i].size(); ++j)
      BBIndexMap.emplace(S[i][j], std::make_pair(i, j));

  // This is the set of sections which have EHPads in them.
  SmallSet<unsigned, 2> EHPadsSections;

  // All BBs are initially assigned the cold section. With the list option, all cold BBs can be clustered in a single cold section.
  for (auto &MBB : MF) {
    // With the 'all' option, every basic block is placed in a unique section.
    // With the 'list' option, every basic block is placed in a section
    // associated with its cluster, unless we want sections for every basic
    // block in this function (if S is empty).
    if (MF.getTarget().getBBSectionsType() == llvm::BasicBlockSection::All
        || S.empty())
      MBB.setSectionID(MBB.getNumber());
    else if (BBIndexMap.count(MBB.getNumber()))
      MBB.setSectionID(BBIndexMap.at(MBB.getNumber()).first);

    if (MBB.isEHPad())
      EHPadsSections.insert(MBB.getSectionID());
  }

  // If EHPads are in more than one section, we move all of them to a specific
  // exception section, as we need all EH Pads to be in a single section.
  if (EHPadsSections.size() > 1) {
    std::for_each(MF.begin(), MF.end(), [&](MachineBasicBlock &MBB) {
      if (MBB.isEHPad())
        MBB.setSectionID(MachineBasicBlock::ExceptionSectionID);
    });
  }

  for (auto &MBB : MF) {
    // With -fbasicblock-sections, fall through blocks must be made
    // explicitly reachable.  Do this after sections is set as
    // unnecessary fallthroughs can be avoided.
    insertUnconditionalFallthroughBranch(MBB);
  }

  auto EntrySectionID = MF.front().getSectionID();

  // We sort all basic blocks to make sure the basic blocks of every cluster are
  // contiguous and in the given order. Furthermore, clusters are ordered in
  // increasing order of their section IDs, with the exception and the
  // cold section placed at the end of the function.
  MF.sort([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
    auto XSectionID = X.getSectionID();
    auto YSectionID = Y.getSectionID();
    // If the two basic block are in the same section, the order is decided by
    // their order within the section.
    if (XSectionID == YSectionID)
      return XSectionID < S.size() ? BBIndexMap.at(X.getNumber()).second < BBIndexMap.at(Y.getNumber()).second : X.getNumber() < Y.getNumber();
    // We make sure that the section containing the entry block precedes all the
    // other sections.
    if (XSectionID == EntrySectionID || YSectionID == EntrySectionID)
      return XSectionID == EntrySectionID;
    return XSectionID < YSectionID;
  });

  // After the basic blocks are sorted, the branching instructions of every
  // basic block (except those at the end of the sections) are optimized to
  // remove the previously inserted branches.
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  for (auto &MBB : MF)
    if (!MBB.isEndSection()) {
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
  assignSectionsAndSortBasicBlocks(MF, BBSectionsList, FuncAliases);

  return true;
}

// Basic Block Sections can be enabled for a subset of machine basic blocks.
// This is done by passing a file containing names of functions for which basic
// block sections are desired.  Additionally, machine basic block ids of the
// functions can also be specified for a finer granularity. Moreover, a cluster
// of basic blocks could be assigned to the same section.
// A file with basic block sections for all of function main and three blocks for
// function foo (two of which share a section) looks like this:
// ----------------------------
// list.txt:
// !main
// !foo
// !!1 2
// !!4
static bool getBBSectionsList(StringRef profFileName,
                              StringMap<SmallVector<SmallVector<unsigned, 4>, 2>> &bbClusterMap,
                              StringMap<StringRef> &funcAliasMap) {
  if (profFileName.empty())
    return false;

  auto MbOrErr = MemoryBuffer::getFile(profFileName);
  if (std::error_code EC = MbOrErr.getError()) {
    errs() << "Could not open profile: " << EC.message();
    return false;
  }

  MemoryBuffer &Buffer = *MbOrErr.get();
  line_iterator LineIt(Buffer, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  StringMap<SmallVector<SmallVector<unsigned, 4>, 2>>::iterator fi = bbClusterMap.end();

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef s(*LineIt);
    if (s[0] == '@')
      continue;
    // Check for the leading "!"
    if (!s.consume_front("!") || s.empty())
      break;
    // Check for second "!" which indicates a cluster of basic blocks.
    if (s.consume_front("!")) {
      if (fi == bbClusterMap.end()) {
        errs() << "Could not process profile: " << profFileName << " at line " << Twine(LineIt.line_number()) << " Does not follow a function name.\n";
        return false;
      }
      std::istringstream iss(s.str());
      std::vector<std::string> BBIndexes((std::istream_iterator<std::string>(iss)),
                                         std::istream_iterator<std::string>());
      if(!BBIndexes.empty())
        fi->second.emplace_back();
      for (auto& BBIndexStr : BBIndexes) {
        unsigned BBIndex;
        if (StringRef(BBIndexStr).getAsInteger(10, BBIndex)) {
          errs() << "Could not process profile: " << profFileName << " at line " << Twine(LineIt.line_number()) << " " << BBIndexStr << " is not a number!\n";
          return false;
        }
        if(!BBIndex && !fi->second.back().empty()) {
          errs() << "Could not process profile " << profFileName << " at line " << Twine(LineIt.line_number()) << " Entry BB in the middle of the BB Cluster list!\n";
          return false;
        }
        fi->second.back().push_back(BBIndex);
      }
    } else {
      auto P = s.split('/');
      // Start a new function.
      fi = bbClusterMap.try_emplace(P.first).first;

      auto aliasStr = P.second;
      while(aliasStr!=""){
        auto Q = aliasStr.split('/');
        funcAliasMap.try_emplace(Q.first, P.first);
        aliasStr = Q.second;
      }
    }
  }
  return true;
}

bool BBSectionsPrepare::doInitialization(Module &M) {
  if (!ProfileFileName.empty())
    if (!getBBSectionsList(ProfileFileName, BBSectionsList, FuncAliases))
      BBSectionsList.clear();
  return false;
}

void BBSectionsPrepare::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineModuleInfoWrapperPass>();
}

MachineFunctionPass *
llvm::createBBSectionsPreparePass(const std::string &ProfileFile) {
  return new BBSectionsPrepare(ProfileFile);
}
