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
// Basic Block Sections
// ====================
//
// With option, -fbasicblock-sections=, each basic block could be placed in a
// unique ELF text section in the object file along with a symbol labelling the
// basic block. The linker can then order the basic block sections in any
// arbitrary sequence which when done correctly can encapsulate block layout,
// function layout and function splitting optimizations. However, there are a
// couple of challenges to be addressed for this to be feasible:
//
// 1. The compiler must not allow any implicit fall-through between any two
//    adjacent basic blocks as they could be reordered at link time to be
//    non-adjacent. In other words, the compiler must make a fall-through
//    between adjacent basic blocks explicit by retaining the direct jump
//    instruction that jumps to the next basic block.
//
// 2. All inter-basic block branch targets would now need to be resolved by the
//    linker as they cannot be calculated during compile time. This is done
//    using static relocations. Further, the compiler tries to use short branch
//    instructions on some ISAs for small branch offsets. This is not possible
//    with basic block sections as the offset is not determined at compile time,
//    and long branch instructions have to be used everywhere.
//
// 3. Each additional section bloats object file sizes by tens of bytes.  The
//    number of basic blocks can be potentially very large compared to the size
//    of functions and can bloat object sizes significantly. Option
//    fbasicblock-sections= also takes a file path which can be used to specify
//    a subset of basic blocks that needs unique sections to keep the bloats
//    small.
//
// 4. Debug Information (DebugInfo) and Call Frame Information (CFI) emission
//    needs special handling with basic block sections. DebugInfo needs to be
//    emitted with more relocations as basic block sections can break a
//    function into potentially several disjoint pieces, and CFI needs to be
//    emitted per basic block. This also bloats the object file and binary
//    sizes.
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
  unsigned ClusterID;
  unsigned PositionInCluster;
};

class BBSectionsPrepare : public MachineFunctionPass {
public:
  static char ID;
  StringMap<DenseMap<unsigned, BBClusterInfo>> BBClusterInfoMap;
  StringMap<StringRef> FuncAliasMap;
  const MemoryBuffer *MBuf = nullptr;

  bool getBBClusterInfoForFunction(StringRef s, DenseMap<unsigned, BBClusterInfo>& m) {
    auto r = FuncAliasMap.find(s);
    StringRef aliasName = r == FuncAliasMap.end() ? s : r->second;
    auto p = BBClusterInfoMap.find(aliasName);
    if (p == BBClusterInfoMap.end())
      return false;
    m = p->second;
    return true;
  }

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
};

} // end anonymous namespace

char BBSectionsPrepare::ID = 0;
INITIALIZE_PASS(BBSectionsPrepare, "bbsections-prepare",
                "Determine if a basic block needs a special section", false,
                false)

// This inserts an unconditional branch at the end of MBB to the next basic
// block S if and only if the control-flow implicitly falls through from MBB to
// S. This is necessary with basic block sections as MBB and S could be
// potentially reordered.
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
static bool assignSectionsAndSortBasicBlocks(MachineFunction &MF,
                                             DenseMap<unsigned, BBClusterInfo> &FuncBBClusterInfo) {
  // This is the set of sections which have EHPads in them.
  SmallSet<unsigned, 2> EHPadsSections;

  // All BBs are initially assigned the cold section. With the list option, all
  // cold BBs can be clustered in a single cold section.
  for (auto &MBB : MF) {
    // With the 'all' option, every basic block is placed in a unique section.
    // With the 'list' option, every basic block is placed in a section
    // associated with its cluster, unless we want sections for every basic
    // block in this function (if S is empty).
    if (MF.getTarget().getBBSectionsType() == llvm::BasicBlockSection::All ||
        FuncBBClusterInfo.empty())
      MBB.setSectionID(MBB.getNumber());
    else if (FuncBBClusterInfo.count(MBB.getNumber()))
      MBB.setSectionID(FuncBBClusterInfo.lookup(MBB.getNumber()).ClusterID);
    else
      MBB.setSectionID(MachineBasicBlock::ColdSectionID);

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
    // explicitly reachable.  Do this after sections is set as
    // unnecessary fallthroughs can be avoided.
    insertUnconditionalFallthroughBranch(MBB);
  }

  auto EntrySectionID = MF.front().getSectionID().getValue();

  // We sort all basic blocks to make sure the basic blocks of every cluster are
  // contiguous and in the given order. Furthermore, clusters are ordered in
  // increasing order of their section IDs, with the exception and the
  // cold section placed at the end of the function.
  MF.sort([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
    auto XSectionID = X.getSectionID().getValue();
    auto YSectionID = Y.getSectionID().getValue();
    // If the two basic block are in the same section, the order is decided by
    // their order within the section.
    if (XSectionID == YSectionID)
      return XSectionID < MachineBasicBlock::ExceptionSectionID ? FuncBBClusterInfo.lookup(X.getNumber()).PositionInCluster <
                                         FuncBBClusterInfo.lookup(Y.getNumber()).PositionInCluster
                                   : X.getNumber() < Y.getNumber();
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
    return true;
  }

  DenseMap<unsigned, BBClusterInfo> FuncBBClusterInfo;
  if (BBSectionsType == BasicBlockSection::List && !getBBClusterInfoForFunction(MF.getName(), FuncBBClusterInfo))
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
// for function foo (two of which share a section) looks like this:
// ----------------------------
// list.txt:
// !main
// !foo
// !!1 2
// !!4
static bool getBBClusterInfo(
    const MemoryBuffer *MBuf,
    StringMap<DenseMap<unsigned, BBClusterInfo>> &bbClusterInfoMap,
    StringMap<StringRef> &funcAliasMap) {
  if (!MBuf)
    return false;

  line_iterator LineIt(*MBuf, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  StringMap<DenseMap<unsigned, BBClusterInfo>>::iterator fi =
      bbClusterInfoMap.end();

  unsigned CurrentCluster=0;
  unsigned CurrentPosition=0;

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef s(*LineIt);
    if (s[0] == '@')
      continue;
    // Check for the leading "!"
    if (!s.consume_front("!") || s.empty())
      break;
    // Check for second "!" which indicates a cluster of basic blocks.
    if (s.consume_front("!")) {
      if (fi == bbClusterInfoMap.end()) {
        errs() << "Could not process profile: " << MBuf->getBufferIdentifier()
               << " at line " << Twine(LineIt.line_number())
               << " Does not follow a function name.\n";
        return false;
      }
      std::istringstream iss(s.str());
      std::vector<std::string> BBIndexes(
          (std::istream_iterator<std::string>(iss)),
          std::istream_iterator<std::string>());
      CurrentPosition = 0;
      for (auto &BBIndexStr : BBIndexes) {
        unsigned BBIndex;
        if (StringRef(BBIndexStr).getAsInteger(10, BBIndex)) {
          errs() << "Could not process profile: " << MBuf->getBufferIdentifier()
                 << " at line " << Twine(LineIt.line_number()) << " "
                 << BBIndexStr << " is not a number!\n";
          return false;
        }
        if (!BBIndex && CurrentPosition) {
          errs() << "Could not process profile " << MBuf->getBufferIdentifier()
                 << " at line " << Twine(LineIt.line_number())
                 << " Entry BB in the middle of the BB Cluster list!\n";
          return false;
        }
        fi->second.try_emplace(BBIndex, BBClusterInfo{CurrentCluster, CurrentPosition++});
      }
      CurrentCluster++;
    } else {
      auto P = s.split('/');
      // Start a new function.
      fi = bbClusterInfoMap.try_emplace(P.first).first;

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
    if (!getBBClusterInfo(MBuf, BBClusterInfoMap, FuncAliasMap))
      BBClusterInfoMap.clear();
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
