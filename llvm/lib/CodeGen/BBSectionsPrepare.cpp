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
// -fbasicblock-sections= option is used. Further, with profile information only
// the subset of basic blocks with profiles are placed in separate sections and
// the rest are grouped in a cold section. The exception handling blocks are
// treated specially to ensure they are all in one seciton.
//
// Basic Block Sections
// ====================
//
// With option, -fbasicblock-sections=list, every function may be split into
// clusters of basic blocks. Every cluster will be emitted into a separate
// section with its basic blocks sequenced in the given order. To get the
// optimized performance, the clusters must form an optimal BB layout for the
// function. Every cluster's section is labeled with a symbol to allow the
// linker to reorder the sections in any arbitrary sequence. A global order of
// these sections would encapsulate the function layout.
//
// There are a couple of challenges to be addressed:
//
// 1. The last basic block of every cluster should not have any implicit
//    fallthrough to its next basic block, as it can be reordered by the linker.
//    The compiler should make these fallthroughs explicit by adding
//    unconditional jumps..
//
// 2. All inter-cluster branch targets would now need to be resolved by the
//    linker as they cannot be calculated during compile time. This is done
//    using static relocations. Further, the compiler tries to use short branch
//    instructions on some ISAs for small branch offsets. This is not possible
//    for inter-cluster branches as the offset is not determined at compile
//    time, and therefore, long branch instructions have to be used for those.
//
// 3. Debug Information (DebugInfo) and Call Frame Information (CFI) emission
//    needs special handling with basic block sections. DebugInfo needs to be
//    emitted with more relocations as basic block sections can break a
//    function into potentially several disjoint pieces, and CFI needs to be
//    emitted per cluster. This also bloats the object file and binary sizes.
//
// Basic Block Labels
// ==================
//
// With -fbasicblock-sections=labels, or when a basic block is placed in a
// unique section, it is labelled with a symbol.  This allows easy mapping of
// virtual addresses from PMU profiles back to the corresponding basic blocks.
// Since the number of basic blocks is large, the labeling bloats the symbol
// table sizes and the string table sizes significantly. While the binary size
// does increase, it does not affect performance as the symbol table is not
// loaded in memory during run-time. The string table size bloat is kept very
// minimal using a unary naming scheme that uses string suffix compression. The
// basic blocks for function foo are named "a.BB.foo", "aa.BB.foo", ... This
// turns out to be very good for string table sizes and the bloat in the string
// table size for a very large binary is ~8 %.  The naming also allows using
// the --symbol-ordering-file option in LLD to arbitrarily reorder the
// sections.
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

#include <iterator>
#include <sstream>
#include <string>

using llvm::SmallSet;
using llvm::StringMap;
using llvm::StringRef;
using namespace llvm;

namespace {

struct BBClusterInfo {
  unsigned MBBNumber;
  unsigned ClusterID;
  unsigned PositionInCluster;
};

class BBSectionsPrepare : public MachineFunctionPass {
public:
  static char ID;

  // This would hold the basic-block-sections profile.
  const MemoryBuffer *MBuf = nullptr;

  // This encapsulates the BB cluster information for every function.
  // For every function name, we have a map from (some of) its basic block ids
  // to its cluster information. The cluster information for every basic block
  // includes its cluster ID along with the position of the basic block in that
  // cluster.
  StringMap<SmallVector<BBClusterInfo, 4>> ProgramBBClusterInfo;

  // Some functions have alias names. We use this map to find the main alias
  // name for which we have mapping in ProgramBBClusterInfoMap.
  StringMap<StringRef> FuncAliasMap;

  BBSectionsPrepare(const MemoryBuffer *Buf)
      : MachineFunctionPass(ID), MBuf(Buf) {
    initializeBBSectionsPreparePass(*PassRegistry::getPassRegistry());
  };

  BBSectionsPrepare() : MachineFunctionPass(ID) {
    initializeBBSectionsPreparePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Basic Block Sections Analysis";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Read profiles of basic blocks if available here.
  bool doInitialization(Module &M) override;

  /// Identify basic blocks that need separate sections and prepare to emit them
  /// accordingly.
  bool runOnMachineFunction(MachineFunction &MF) override;

  // This function provides the BBCluster information associated with a function
  // name. It returns true if mapping is found and false otherwise.
  bool getBBClusterInfoForFunction(StringRef S,
                                   SmallVector<BBClusterInfo, 4> &M) {
    auto R = FuncAliasMap.find(S);
    StringRef AliasName = R == FuncAliasMap.end() ? S : R->second;
    auto P = ProgramBBClusterInfo.find(AliasName);
    if (P == ProgramBBClusterInfo.end())
      return false;
    M = P->second;
    return true;
  }
};

} // end anonymous namespace

char BBSectionsPrepare::ID = 0;
INITIALIZE_PASS(BBSectionsPrepare, "bbsections-prepare",
                "Prepares for basic block sections, by splitting functions "
                "into clusters of basic blocks.",
                false, false)

// This inserts an unconditional branch at the end of MBB to the next basic
// block S if and only if the control-flow implicitly falls through from MBB to
// S. This is necessary with basic block sections as they can be reordered by
// clustering or by the linker.
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

// This function optimizes the branching instructions of every
// basic block (except those at the end of the sections) in a given function.
static void optimizeBBJumps(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  for (auto &MBB : MF)
    if (!MBB.isEndSection()) {
      Cond.clear();
      MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.
      if (!TII->analyzeBranch(MBB, TBB, FBB, Cond))
        MBB.updateTerminator();
    }
}

// This function sorts basic blocks according to the cluster's information.
// All explicitly specified clusters of basic blocks will be ordered
// accordingly. All non-specified BBs go into a separate "Cold" section.
// Additionally, if exception handling landing pads end up in more than one
// clusters, they are moved into a single "Exception" section. Eventually,
// clusters are ordered in increasing order of their IDs, with the "Exception"
// and "Cold" succeeding all other clusters.
static bool assignSectionsAndSortBasicBlocks(
    MachineFunction &MF, SmallVector<BBClusterInfo, 4> &FuncBBClusterInfo) {

  std::vector<Optional<BBClusterInfo>> allBBClusterInfo(MF.getNumBlockIDs());
  for (auto bbClusterInfo : FuncBBClusterInfo)
    allBBClusterInfo[bbClusterInfo.MBBNumber] = bbClusterInfo;

  // This is the set of sections which have EHPads in them.
  SmallSet<unsigned, 2> EHPadsSections;

  for (auto &MBB : MF) {
    // With the 'all' option, every basic block is placed in a unique section.
    // With the 'list' option, every basic block is placed in a section
    // associated with its cluster, unless we want sections for every basic
    // block in this function (if FuncBBClusterInfo is empty).
    if (MF.getTarget().getBBSectionsType() == llvm::BasicBlockSection::All ||
        FuncBBClusterInfo.empty()) {
      // If unique sections are desired for all basic blocks of the function, we
      // set every basic block's section ID equal to its number (basic block
      // id). This further ensures that basic blocks are ordered canonically.
      MBB.setSectionID(MBB.getNumber());
    } else if (allBBClusterInfo[MBB.getNumber()].hasValue())
      MBB.setSectionID(allBBClusterInfo[MBB.getNumber()]->ClusterID);
    else {
      // BB goes into the special cold section if it is not specified in the
      // cluster info map.
      MBB.setSectionID(MachineBasicBlock::ColdSectionID);
    }

    if (MBB.isEHPad())
      EHPadsSections.insert(MBB.getSectionID().getValue());
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
    // explicitly reachable.
    insertUnconditionalFallthroughBranch(MBB);
  }

  // We make sure that the cluster including the entry basic block precedes all
  // other clusters.
  auto EntrySectionID = MF.front().getSectionID().getValue();

  // We sort all basic blocks to make sure the basic blocks of every cluster are
  // contiguous and ordered accordingly. Furthermore, clusters are ordered in
  // increasing order of their section IDs, with the exception and the
  // cold section placed at the end of the function.
  MF.sort([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
    auto XSectionID = X.getSectionID().getValue();
    auto YSectionID = Y.getSectionID().getValue();
    // If the two basic block are in the same section, the order is decided by
    // their position within the section.
    if (XSectionID == YSectionID)
      return XSectionID < MachineBasicBlock::ExceptionSectionID
                 ? allBBClusterInfo[X.getNumber()]->PositionInCluster <
                       allBBClusterInfo[Y.getNumber()]->PositionInCluster
                 : X.getNumber() < Y.getNumber();
    // We make sure that the section containing the entry block precedes all the
    // other sections.
    if (XSectionID == EntrySectionID || YSectionID == EntrySectionID)
      return XSectionID == EntrySectionID;
    return XSectionID < YSectionID;
  });

  // After ordering basic blocks, we optimize/remove branches which were
  // previously added by insertUnconditionalFallthroughBranch.
  optimizeBBJumps(MF);

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
    return true;
  }

  SmallVector<BBClusterInfo, 4> FuncBBClusterInfo;
  if (BBSectionsType == BasicBlockSection::List &&
      !getBBClusterInfoForFunction(MF.getName(), FuncBBClusterInfo))
    return true;
  MF.setBBSectionsType(BBSectionsType);
  MF.createBBLabels();
  assignSectionsAndSortBasicBlocks(MF, FuncBBClusterInfo);

  return true;
}

// Basic Block Sections can be enabled for a subset of machine basic blocks.
// This is done by passing a file containing names of functions for which basic
// block sections are desired.  Additionally, machine basic block ids of the
// functions can also be specified for a finer granularity. Moreover, a cluster
// of basic blocks could be assigned to the same section.
// A file with basic block sections for all of function main and three blocks
// for function foo (of which 1 and 2 are placed in a cluster) looks like this:
// ----------------------------
// list.txt:
// !main
// !foo
// !!1 2
// !!4
static bool
getBBClusterInfo(const MemoryBuffer *MBuf,
                 StringMap<SmallVector<BBClusterInfo, 4>> &programBBClusterInfo,
                 StringMap<StringRef> &funcAliasMap) {
  if (!MBuf)
    return false;

  line_iterator LineIt(*MBuf, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  auto fi = programBBClusterInfo.end();

  unsigned CurrentCluster = 0;
  unsigned CurrentPosition = 0;

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef s(*LineIt);
    if (s[0] == '@')
      continue;
    // Check for the leading "!"
    if (!s.consume_front("!") || s.empty())
      break;
    // Check for second "!" which indicates a cluster of basic blocks.
    if (s.consume_front("!")) {
      if (fi == programBBClusterInfo.end()) {
        errs() << "Could not process profile: " << MBuf->getBufferIdentifier()
               << " at line " << Twine(LineIt.line_number())
               << ": Does not follow a function name.\n";
        return false;
      }
      std::istringstream iss(s.str());
      std::vector<std::string> BBIndexes(
          (std::istream_iterator<std::string>(iss)),
          std::istream_iterator<std::string>());
      // Current position in the current cluster of basic blocks.
      CurrentPosition = 0;
      for (auto &BBIndexStr : BBIndexes) {
        unsigned BBIndex;
        if (StringRef(BBIndexStr).getAsInteger(10, BBIndex)) {
          errs() << "Could not process profile: " << MBuf->getBufferIdentifier()
                 << " at line " << Twine(LineIt.line_number()) << ": "
                 << BBIndexStr << " is not a number!\n";
          return false;
        }
        if (!BBIndex && CurrentPosition) {
          errs() << "Could not process profile " << MBuf->getBufferIdentifier()
                 << " at line " << Twine(LineIt.line_number())
                 << ": Entry BB in the middle of the BB Cluster list!\n";
          return false;
        }
        fi->second.emplace_back(
            BBClusterInfo{BBIndex, CurrentCluster, CurrentPosition++});
      }
      CurrentCluster++;
    } else {
      // Function aliases are separated using '/'. We use the first function
      // name for the cluster info mapping and delegate all other aliases to
      // this one.
      auto P = s.split('/');
      // Start a new function.
      fi = programBBClusterInfo.try_emplace(P.first).first;

      auto aliasStr = P.second;
      while (aliasStr != "") {
        auto Q = aliasStr.split('/');
        funcAliasMap.try_emplace(Q.first, P.first);
        aliasStr = Q.second;
      }
      CurrentCluster = 0;
    }
  }
  return true;
}

bool BBSectionsPrepare::doInitialization(Module &M) {
  if (MBuf)
    if (!getBBClusterInfo(MBuf, ProgramBBClusterInfo, FuncAliasMap))
      ProgramBBClusterInfo.clear();
  return true;
}

void BBSectionsPrepare::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineModuleInfoWrapperPass>();
}

MachineFunctionPass *
llvm::createBBSectionsPreparePass(const MemoryBuffer *Buf) {
  return new BBSectionsPrepare(Buf);
}
