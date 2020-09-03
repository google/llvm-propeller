//===-- BasicBlockSections.cpp ---=========--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BasicBlockSections implementation.
//
// The purpose of this pass is to assign sections to basic blocks when
// -fbasic-block-sections= option is used. Further, with profile information
// only the subset of basic blocks with profiles are placed in separate sections
// and the rest are grouped in a cold section. The exception handling blocks are
// treated specially to ensure they are all in one seciton.
//
// Basic Block Sections
// ====================
//
// With option, -fbasic-block-sections=list, every function may be split into
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
// With -fbasic-block-sections=labels, or when a basic block is placed in a
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

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/BasicBlockSectionUtils.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetMachine.h"

#include <iostream>

using llvm::SmallSet;
using llvm::SmallVector;
using llvm::StringMap;
using llvm::StringRef;
using namespace llvm;

namespace {
// Converts the path from the from block to the to block to be a fallthrough.
// Requires to to be a successor of from.
// to_block must be placed after from_block in the layout after this call!
// Returns true if the conversion could not be completed.
// If the conversion fails, the parameters will not have changed.
bool ConvertToFallthrough(const TargetInstrInfo *TII,
                          MachineBasicBlock *from_block,
                          const MachineBasicBlock *to_block) {
  if (!from_block || !to_block) {
    std::cerr << "One block is null.\n";
    return true;
  }
  if (!from_block->isSuccessor(to_block)) {
    std::cerr << "The given block must be a successor of from block.\n";
    return true;
  }
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;

  if (TII->analyzeBranch(*from_block, TBB, FBB, Cond)) {
    std::cerr << "Could not analyze branch\n";
    return true;
  }

  if (!TBB && !FBB) {
    // Already falls through, no need to modify the block.
    return false;
  }

  if (TBB && !FBB && Cond.empty()) {
    // The from_block has an unconditional jump at the end.
    // We need to remove that branch so it falls through.

    assert(TBB == to_block &&
           "from_block ends with an unconditional jump, and to_block is it's "
           "successor, the jump must be to to_block.");

    TII->removeBranch(*from_block);
    return false;
  }

  if (TBB && !FBB) {
    // There's a conditional jump to a block. It could be jumping to the
    // original block, or it could be falling through to the original
    // block.

    if (TBB == to_block) {
      // Jumps to original block. We need to make this fall-through to us.
      // Need to invert the branch, make it jump to it's current fall
      // through and fall through to us.
      if (TII->reverseBranchCondition(Cond)) {
        // Could not reverse the condition, abort?
        std::cerr << "Could not reverse branch\n";
        return true;
      }

      auto current_fallthrough = from_block->getFallThrough();
      TII->removeBranch(*from_block);
      TII->insertBranch(*from_block, current_fallthrough, nullptr, Cond,
                        from_block->findBranchDebugLoc());
    } else {
      // Already falls through, no need to modify.
    }
    return false;
  }

  if (TBB && FBB) {
    // The conditional has jump instructions in either direction. We can
    // eliminate one of the jumps and make it fall through to us.

    if (TBB == to_block) {
      // Make the true case fall through.
      if (TII->reverseBranchCondition(Cond)) {
        // Could not reverse the condition.
        std::cerr << "Could not reverse branch\n";
        return true;
      }

      // We will remove this branch and generate a new one to TBB below.
      // Since TBB is the block we want to fall through to, after reversing
      // the branch condition, we also swap the true and false branches.
      std::swap(FBB, TBB);
    } else {
      // Make the false case fall through. This is trivial to do.
      assert(FBB == to_block &&
             "to_block is a successor, but it is neither the true basic block, "
             "nor the false basic block.");
    }

    TII->removeBranch(*from_block);
    TII->insertBranch(*from_block, TBB, nullptr, Cond,
                      from_block->findBranchDebugLoc());

    return false;
  }

  llvm_unreachable("All cases are handled.");
}

MachineBasicBlock* CloneMachineBasicBlock(MachineBasicBlock* block) {
  auto& MF = *block->getParent();
  auto TII = MF.getSubtarget().getInstrInfo();

  // Pass nullptr as this new block doesn't directly correspond to an LLVM basic
  // block.
  auto cloned = MF.CreateMachineBasicBlock(nullptr);
  MF.push_back(cloned);
  for (auto &instr : block->instrs()) {
    cloned->push_back(MF.CloneMachineInstr(&instr));
  }

  cloned->setNumber(MF.addToMBBNumbering(cloned));

  // Add the successors of the original block as the new block's
  // successors as well.
  auto succ_end = block->succ_end();
  for (auto succ_it = block->succ_begin(); succ_it != succ_end; ++succ_it) {
    cloned->copySuccessor(block, succ_it);
  }

  if (auto original_ft = block->getFallThrough()) {
    // The original block has an implicit fall through.
    // Insert an explicit unconditional jump from the cloned block to that
    // same block.
    TII->insertUnconditionalBranch(*cloned, original_ft,
                                   cloned->findBranchDebugLoc());
  }

  for (auto& live : block->liveins()) {
    cloned->addLiveIn(live);
  }

  return cloned;
}

struct UniqueBBID {
  unsigned MBBNumber;
  unsigned CloneNumber;

  friend bool operator<(const UniqueBBID& left, const UniqueBBID& right) {
    return std::tie(left.MBBNumber, left.CloneNumber) < std::tie(right.MBBNumber, right.CloneNumber);
  }

  friend bool operator==(const UniqueBBID& left, const UniqueBBID& right) {
    return left.MBBNumber == right.MBBNumber && left.CloneNumber == right.CloneNumber;
  }
};

// This struct represents the cluster information for a machine basic block.
struct BBTempClusterInfo {
  // MachineBasicBlock ID.
  UniqueBBID MBBNumber;
  // Cluster ID this basic block belongs to.
  unsigned ClusterID;
  // Position of basic block within the cluster.
  unsigned PositionInCluster;
};

// This struct represents the cluster information for a machine basic block.
struct BBClusterInfo {
  // MachineBasicBlock ID.
  unsigned MBBNumber;
  // Cluster ID this basic block belongs to.
  unsigned ClusterID;
  // Position of basic block within the cluster.
  unsigned PositionInCluster;
};

struct BBCloneInfo {
  UniqueBBID Original;
  UniqueBBID Predecessor;
  UniqueBBID Clone;
};

using ProgramBBTemporaryInfoMapTy = StringMap<std::pair<SmallVector<BBTempClusterInfo, 4>, SmallVector<BBCloneInfo, 4>>>;
using ProgramBBClusterInfoMapTy = StringMap<std::map<int, BBClusterInfo>>;

class BasicBlockSections : public MachineFunctionPass {
public:
  static char ID;

  // This contains the basic-block-sections profile.
  const MemoryBuffer *MBuf = nullptr;

  // This encapsulates the BB cluster information for the whole program.
  //
  // For every function name, it contains the cluster information for (all or
  // some of) its basic blocks. The cluster information for every basic block
  // includes its cluster ID along with the position of the basic block in that
  // cluster.
  ProgramBBClusterInfoMapTy ProgramBBClusterInfo;

  ProgramBBTemporaryInfoMapTy ProgramBBTemporaryInfo;

  // Some functions have alias names. We use this map to find the main alias
  // name for which we have mapping in ProgramBBClusterInfo.
  StringMap<StringRef> FuncAliasMap;

  BasicBlockSections(const MemoryBuffer *Buf)
      : MachineFunctionPass(ID), MBuf(Buf) {
    initializeBasicBlockSectionsPass(*PassRegistry::getPassRegistry());
  };

  BasicBlockSections() : MachineFunctionPass(ID) {
    initializeBasicBlockSectionsPass(*PassRegistry::getPassRegistry());
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

char BasicBlockSections::ID = 0;
INITIALIZE_PASS(BasicBlockSections, "bbsections-prepare",
                "Prepares for basic block sections, by splitting functions "
                "into clusters of basic blocks.",
                false, false)

// This function updates and optimizes the branching instructions of every basic
// block in a given function to account for changes in the layout.
static void updateBranches(
    MachineFunction &MF,
    const SmallVector<MachineBasicBlock *, 4> &PreLayoutFallThroughs) {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  for (auto &MBB : MF) {
    auto NextMBBI = std::next(MBB.getIterator());
    auto *FTMBB = PreLayoutFallThroughs[MBB.getNumber()];
    // If this block had a fallthrough before we need an explicit unconditional
    // branch to that block if either
    //     1- the block ends a section, which means its next block may be
    //        reorderd by the linker, or
    //     2- the fallthrough block is not adjacent to the block in the new
    //        order.
    if (FTMBB && (MBB.isEndSection() || &*NextMBBI != FTMBB))
      TII->insertUnconditionalBranch(MBB, FTMBB, MBB.findBranchDebugLoc());

    // We do not optimize branches for machine basic blocks ending sections, as
    // their adjacent block might be reordered by the linker.
    if (MBB.isEndSection())
      continue;

    // It might be possible to optimize branches by flipping the branch
    // condition.
    Cond.clear();
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.
    if (TII->analyzeBranch(MBB, TBB, FBB, Cond))
      continue;
    MBB.updateTerminator(FTMBB);
  }
}

// Performs the cloning instructions in the profile data for the given machine
// function.
// After all clones are performed, it will fill the actual cluster info with
// the correct linear IDs which is used by the block sorting.
// Alongside the cluster info, it also fills out a set of block that have been
// modified by the path layout. These blocks should not have their branches
// adjusted.
// Finally, it also fills out a map from the unique BB IDs in the profile info
// to linear BB ids in the MF for cloned blocks.
static bool performCloningAndPathLayouts(MachineFunction& MF,
                                         const StringMap<StringRef> FuncAliasMap,
                                         std::set<const MachineBasicBlock*>& modified_blocks,
                                         const ProgramBBTemporaryInfoMapTy& temp,
                                         ProgramBBClusterInfoMapTy& out,
                                         std::map<UniqueBBID,
                                                  unsigned>& bb_id_to_linear_index) {
  auto FuncName = MF.getName();
  auto R = FuncAliasMap.find(FuncName);
  StringRef AliasName = R == FuncAliasMap.end() ? FuncName : R->second;

  // Find the assoicated cluster information.
  auto P = temp.find(AliasName);
  if (P == temp.end())
  {
    return false;
  }

  auto get_linear_id =
      [&bb_id_to_linear_index, &MF](const UniqueBBID& id) -> Optional<long long> {
        if (id.CloneNumber == 0) {
          if (id.MBBNumber >= MF.getNumBlockIDs()) {
            return None;
          }
          return id.MBBNumber;
        }
        auto it = bb_id_to_linear_index.find(id);
        if (it != bb_id_to_linear_index.end()) {
          return it->second;
        }
        return None;
      };

  auto TII = MF.getSubtarget().getInstrInfo();

  for (auto& clone : P->second.second) {
    auto pred_linear_id = get_linear_id(clone.Predecessor);
    auto orig_linear_id = get_linear_id(clone.Original);

    if (!pred_linear_id || !orig_linear_id) {
      WithColor::warning() << "Unknown block in " << MF.getName().str() << '\n';
      return false;
    }

    auto pred_block = MF.getBlockNumbered(*pred_linear_id);
    auto orig_block = MF.getBlockNumbered(*orig_linear_id);

    if (!pred_block->isSuccessor(orig_block)) {
      WithColor::warning() << "Clone predecessor is wrong in " << MF.getName().str() << '\n';
      return false;
    }

    if (!pred_block->empty() && pred_block->back().isIndirectBranch()) {
      WithColor::warning() << "Predecessor with an indirect branch in " << MF.getName().str() << '\n';

      return false;
    }

    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;

    if (TII->analyzeBranch(*pred_block, TBB, FBB, Cond)) {
      WithColor::warning() << "Could not analyze branch in " << MF.getName().str() << '\n';
      return false;
    }
    bb_id_to_linear_index[clone.Clone] = *orig_linear_id;
  }
  for (auto& bb : P->second.first) {
    auto linear_id = get_linear_id(bb.MBBNumber);
    if (!linear_id) {
      return false;
    }
  }

  bb_id_to_linear_index.clear();

  // This step creates all the necessary clones. It does not adjust the branches.
  for (auto& clone : P->second.second) {
    auto pred_linear_id = get_linear_id(clone.Predecessor);
    if (!pred_linear_id) {
      WithColor::warning() << "Unknown predecessor " << clone.Predecessor.MBBNumber << "#" << clone.Predecessor.CloneNumber << '\n';
      return false;
    }

    auto orig_linear_id = get_linear_id(clone.Original);
    if (!orig_linear_id) {
      WithColor::error() << "Unknown original " << clone.Original.MBBNumber << "#" << clone.Original.CloneNumber << '\n';
      return false;
    }
    
    std::cerr << "pred, original " << clone.Predecessor.MBBNumber << "#" << clone.Predecessor.CloneNumber << " " << clone.Original.MBBNumber << "#" << clone.Original.CloneNumber << '\n';
    std::cerr << "clone id " << clone.Clone.MBBNumber << "#" << clone.Clone.CloneNumber << '\n';
    std::cerr << "pred, original " << *pred_linear_id << " " << *orig_linear_id << '\n';
    auto orig_block = MF.getBlockNumbered(*orig_linear_id);

    auto cloned = CloneMachineBasicBlock(orig_block);

    if (!cloned) {
      WithColor::error() << "A basic block clone failed at "
                         << MF.getName().str() << '\n';
      return false;
    }

    bb_id_to_linear_index[clone.Clone] = cloned->getNumber();
    std::cerr << "Clone number: " << cloned->getNumber() << '\n';

    auto pred_block = MF.getBlockNumbered(*pred_linear_id);

        std::cerr << "===\nConverting to fallthrough\n"                         << "pred, original " << clone.Predecessor.MBBNumber
                         << "#" << clone.Predecessor.CloneNumber << " "
                         << clone.Original.MBBNumber << "#"
                         << clone.Original.CloneNumber << '\n'
                         << "clone id " << clone.Clone.MBBNumber << "#"
                         << clone.Clone.CloneNumber << '\n'
                         << "pred, original " << *pred_linear_id << " " << *orig_linear_id << "\n===\n";

    if (ConvertToFallthrough(TII, pred_block, orig_block)) {
      WithColor::error() << "Hot path generation failed.\n";
      std::cerr << MF.getName().str() << '\n';

      for (auto& bb : MF) {
        std::cerr << bb.getName().str() << '\n';
        bb.dump();
      }

      llvm::report_fatal_error("Hot path generation failed");
      return false;
    }

    TII->insertUnconditionalBranch(*pred_block, cloned,
                                   pred_block->findBranchDebugLoc());

    // The pred_block falls through to block at this point.

    // Remove the original block from the successors of the previous block.
    // The remove call removes pred_block from predecessors of block as
    // well.
    pred_block->removeSuccessor(orig_block);

    //TODO(fbakir): what should be the probability here?
    pred_block->addSuccessor(cloned, BranchProbability::getOne());
  }

//  // This step adjusts the branches of predecessors of clones. A clone's
//  // predecessor must always fallthrough to it.
//  for (auto& clone : P->second.second) {
//    auto orig_copy = clone.Original;
//    orig_copy.CloneNumber = 0;
//    auto pred_linear_id = get_linear_id(clone.Predecessor);
//    auto orig_linear_id = get_linear_id(orig_copy);
//    auto clone_linear_id = get_linear_id(clone.Clone);
//
//    auto pred_block = MF.getBlockNumbered(*pred_linear_id);
//    auto orig_block = MF.getBlockNumbered(*orig_linear_id);
//    auto clone_block = MF.getBlockNumbered(*clone_linear_id);
//
//
////    modified_blocks.emplace(pred_block);
////    modified_blocks.emplace(clone_block);
//  }

  auto& output = out[AliasName];
  for (auto& bb : P->second.first) {
    auto linear_id = get_linear_id(bb.MBBNumber);
    if (!linear_id) {
      WithColor::warning() << "Could not find a bb in " << MF.getName().str() << '\n';
      return false;
    }
    output.emplace(*linear_id, BBClusterInfo{
        static_cast<unsigned>(*linear_id), bb.ClusterID, bb.PositionInCluster
    });
  }

  return true;
}

// This function provides the BBCluster information associated with a function.
// Returns true if a valid association exists and false otherwise.
static bool getBBClusterInfoForFunction(
    const MachineFunction &MF, const StringMap<StringRef> FuncAliasMap,
    const ProgramBBClusterInfoMapTy &ProgramBBClusterInfo,
    std::vector<Optional<BBClusterInfo>> &V) {
  // Get the main alias name for the function.
  auto FuncName = MF.getName();
  auto R = FuncAliasMap.find(FuncName);
  StringRef AliasName = R == FuncAliasMap.end() ? FuncName : R->second;

  // Find the assoicated cluster information.
  auto P = ProgramBBClusterInfo.find(AliasName);
  if (P == ProgramBBClusterInfo.end())
    return false;

  if (P->second.empty()) {
    // This indicates that sections are desired for all basic blocks of this
    // function. We clear the BBClusterInfo vector to denote this.
    V.clear();
    return true;
  }

  V.resize(MF.getNumBlockIDs());
  for (auto [key, bbClusterInfo] : P->second) {
    // Bail out if the cluster information contains invalid MBB numbers.
    if (bbClusterInfo.MBBNumber >= MF.getNumBlockIDs())
      return false;
    V[bbClusterInfo.MBBNumber] = bbClusterInfo;
  }
  return true;
}

// This function sorts basic blocks according to the cluster's information.
// All explicitly specified clusters of basic blocks will be ordered
// accordingly. All non-specified BBs go into a separate "Cold" section.
// Additionally, if exception handling landing pads end up in more than one
// clusters, they are moved into a single "Exception" section. Eventually,
// clusters are ordered in increasing order of their IDs, with the "Exception"
// and "Cold" succeeding all other clusters.
// FuncBBClusterInfo represent the cluster information for basic blocks. If this
// is empty, it means unique sections for all basic blocks in the function.
static void
assignSections(MachineFunction &MF,
               const std::vector<Optional<BBClusterInfo>> &FuncBBClusterInfo) {
  assert(MF.hasBBSections() && "BB Sections is not set for function.");
  // This variable stores the section ID of the cluster containing eh_pads (if
  // all eh_pads are one cluster). If more than one cluster contain eh_pads, we
  // set it equal to ExceptionSectionID.
  Optional<MBBSectionID> EHPadsSectionID;

  for (auto &MBB : MF) {
    // With the 'all' option, every basic block is placed in a unique section.
    // With the 'list' option, every basic block is placed in a section
    // associated with its cluster, unless we want individual unique sections
    // for every basic block in this function (if FuncBBClusterInfo is empty).
    if (MF.getTarget().getBBSectionsType() == llvm::BasicBlockSection::All ||
        FuncBBClusterInfo.empty()) {
      // If unique sections are desired for all basic blocks of the function, we
      // set every basic block's section ID equal to its number (basic block
      // id). This further ensures that basic blocks are ordered canonically.
      MBB.setSectionID({static_cast<unsigned int>(MBB.getNumber())});
    } else if (FuncBBClusterInfo[MBB.getNumber()].hasValue())
      MBB.setSectionID(FuncBBClusterInfo[MBB.getNumber()]->ClusterID);
    else {
      // BB goes into the special cold section if it is not specified in the
      // cluster info map.
      MBB.setSectionID(MBBSectionID::ColdSectionID);
    }

    if (MBB.isEHPad() && EHPadsSectionID != MBB.getSectionID() &&
        EHPadsSectionID != MBBSectionID::ExceptionSectionID) {
      // If we already have one cluster containing eh_pads, this must be updated
      // to ExceptionSectionID. Otherwise, we set it equal to the current
      // section ID.
      EHPadsSectionID = EHPadsSectionID.hasValue()
                            ? MBBSectionID::ExceptionSectionID
                            : MBB.getSectionID();
    }
  }

  // If EHPads are in more than one section, this places all of them in the
  // special exception section.
  if (EHPadsSectionID == MBBSectionID::ExceptionSectionID)
    for (auto &MBB : MF)
      if (MBB.isEHPad())
        MBB.setSectionID(EHPadsSectionID.getValue());
}

// This function is exposed externally by BasicBlockSectionUtils.h
void llvm::sortBasicBlocksAndUpdateBranches(
    MachineFunction &MF, MachineBasicBlockComparator MBBCmp) {
  SmallVector<MachineBasicBlock *, 4> PreLayoutFallThroughs(
      MF.getNumBlockIDs());
  for (auto &MBB : MF)
    PreLayoutFallThroughs[MBB.getNumber()] = MBB.getFallThrough();

  MF.sort(MBBCmp);

  // If any of the BBs have their address taken, we place all basic blocks in
  // one section.
  for(auto &MBB : MF) {
    if (MBB.hasAddressTaken()) {
      for(auto &MBB: MF)
        MBB.setSectionID(0);
      break;
    }
  }

  // Set IsBeginSection and IsEndSection according to the assigned section IDs.
  MF.assignBeginEndSections();

  // After reordering basic blocks, we must update basic block branches to
  // insert explicit fallthrough branches when required and optimize branches
  // when possible.
  updateBranches(MF, PreLayoutFallThroughs);
}

bool BasicBlockSections::runOnMachineFunction(MachineFunction &MF) {
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

  std::map<UniqueBBID, unsigned> bb_id_to_linear_index;
  std::set<const MachineBasicBlock*> cloning_modified;
  if (!performCloningAndPathLayouts(MF,
                                    FuncAliasMap,
                                    cloning_modified,
                                    ProgramBBTemporaryInfo,
                                    ProgramBBClusterInfo,
                                    bb_id_to_linear_index)) {
    return true;
  }

  std::vector<Optional<BBClusterInfo>> FuncBBClusterInfo;
  if (BBSectionsType == BasicBlockSection::List &&
      !getBBClusterInfoForFunction(MF, FuncAliasMap, ProgramBBClusterInfo,
                                   FuncBBClusterInfo))
    return true;

  MF.setBBSectionsType(BBSectionsType);
  MF.createBBLabels();
  assignSections(MF, FuncBBClusterInfo);

  // We make sure that the cluster including the entry basic block precedes all
  // other clusters.
  auto EntryBBSectionID = MF.front().getSectionID();

  // Helper function for ordering BB sections as follows:
  //   * Entry section (section including the entry block).
  //   * Regular sections (in increasing order of their Number).
  //     ...
  //   * Exception section
  //   * Cold section
  auto MBBSectionOrder = [EntryBBSectionID](const MBBSectionID &LHS,
                                            const MBBSectionID &RHS) {
    // We make sure that the section containing the entry block precedes all the
    // other sections.
    if (LHS == EntryBBSectionID || RHS == EntryBBSectionID)
      return LHS == EntryBBSectionID;
    return LHS.Type == RHS.Type ? LHS.Number < RHS.Number : LHS.Type < RHS.Type;
  };

  // We sort all basic blocks to make sure the basic blocks of every cluster are
  // contiguous and ordered accordingly. Furthermore, clusters are ordered in
  // increasing order of their section IDs, with the exception and the
  // cold section placed at the end of the function.
  auto Comparator = [&](const MachineBasicBlock &X,
                        const MachineBasicBlock &Y) {
    auto XSectionID = X.getSectionID();
    auto YSectionID = Y.getSectionID();
    if (XSectionID != YSectionID)
      return MBBSectionOrder(XSectionID, YSectionID);
    // If the two basic block are in the same section, the order is decided by
    // their position within the section.
    if (XSectionID.Type == MBBSectionID::SectionType::Default)
      return FuncBBClusterInfo[X.getNumber()]->PositionInCluster <
             FuncBBClusterInfo[Y.getNumber()]->PositionInCluster;
    return X.getNumber() < Y.getNumber();
  };

  sortBasicBlocksAndUpdateBranches(MF, Comparator);

  auto FuncName = MF.getName();
  auto R = FuncAliasMap.find(FuncName);
  StringRef AliasName = R == FuncAliasMap.end() ? FuncName : R->second;

  // Find the assoicated cluster information.
  auto P = ProgramBBTemporaryInfo.find(AliasName);
  if (P == ProgramBBTemporaryInfo.end())
  {
    return true;
  }

  auto get_linear_id =
      [&bb_id_to_linear_index, &MF](const UniqueBBID& id) -> Optional<long long> {
        if (id.CloneNumber == 0) {
          if (id.MBBNumber >= MF.getNumBlockIDs()) {
            return None;
          }
          return id.MBBNumber;
        }
        auto it = bb_id_to_linear_index.find(id);
        if (it != bb_id_to_linear_index.end()) {
          return it->second;
        }
        return None;
      };
  for (auto& clone : P->second.second) {
    auto mbb_num = get_linear_id(clone.Clone);
    auto mbb = MF.getBlockNumbered(mbb_num.getValue());
    auto pred_num = get_linear_id(clone.Predecessor);
    auto pred_mbb = MF.getBlockNumbered(pred_num.getValue());
    if (pred_mbb->getFallThrough() != mbb) {
      WithColor::warning() << MF.getName().str() << '\n';
      WithColor::warning() << "Clone is not a fallthrough! " << clone.Predecessor.MBBNumber
                         << "#" << clone.Predecessor.CloneNumber << " "
                         << clone.Original.MBBNumber << "#"
                         << clone.Original.CloneNumber << '\n'
                         << "clone id " << clone.Clone.MBBNumber << "#"
                         << clone.Clone.CloneNumber << '\n';
    }
  }

  return true;
}

static Optional<UniqueBBID> parseBBId(StringRef str) {
  SmallVector<StringRef, 2> parts;
  str.split(parts, '#');
  unsigned long long BBIndex;
  if (getAsUnsignedInteger(parts[0], 10, BBIndex)) {
    return None;
  }
  unsigned long long clone_id;
  if (getAsUnsignedInteger(parts[1], 10, clone_id)) {
    return None;
  }
  return UniqueBBID{static_cast<unsigned>(BBIndex), static_cast<unsigned>(clone_id)};
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
static Error getBBClusterInfo(const MemoryBuffer *MBuf,
                              ProgramBBTemporaryInfoMapTy &ProgramBBClusterInfo,
                              StringMap<StringRef> &FuncAliasMap) {
  assert(MBuf);
  line_iterator LineIt(*MBuf, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  auto invalidProfileError = [&](auto Message) {
    return make_error<StringError>(
        Twine("Invalid profile " + MBuf->getBufferIdentifier() + " at line " +
              Twine(LineIt.line_number()) + ": " + Message),
        inconvertibleErrorCode());
  };

  auto FI = ProgramBBClusterInfo.end();

  // Current cluster ID corresponding to this function.
  unsigned CurrentCluster = 0;
  // Current position in the current cluster.
  unsigned CurrentPosition = 0;

  // Temporary set to ensure every basic block ID appears once in the clusters
  // of a function.
  SmallSet<UniqueBBID, 4> FuncBBIDs;

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef S(*LineIt);
    if (S[0] == '@')
      continue;
    // Check for the leading "!"
    if (!S.consume_front("!") || S.empty())
      break;

    if (S.consume_front("!!")) {
        if (FI == ProgramBBClusterInfo.end()) {
          return invalidProfileError(
              "Clone list does not follow a function name specifier.");
        }
        SmallVector<StringRef, 3> CloneInformation;
        S.split(CloneInformation, ' ');

        if (CloneInformation.size() != 3) {
          return invalidProfileError(
              "Malformed clone information.");
        }

        auto clone_block = parseBBId(CloneInformation[0]);
        auto org_block = parseBBId(CloneInformation[1]);
        auto pred_block = parseBBId(CloneInformation[2]);
        if (!clone_block || !org_block || !pred_block) {
          return invalidProfileError(
              "Invalid BB or clone id.");
        }

        FI->second.second.emplace_back(BBCloneInfo{*org_block, *pred_block, *clone_block});
    } else {
      // Check for second "!" which indicates a cluster of basic blocks.
      if (S.consume_front("!")) {
        if (FI == ProgramBBClusterInfo.end())
          return invalidProfileError(
              "Cluster list does not follow a function name specifier.");
        SmallVector<StringRef, 4> BBIndexes;
        S.split(BBIndexes, ' ');
        // Reset current cluster position.
        CurrentPosition = 0;
        for (auto BBIndexStr : BBIndexes) {
          auto bbId = parseBBId(BBIndexStr);
          if(!bbId) {
            return invalidProfileError(Twine("BB Id expected: '") +
                                       BBIndexStr + "'.");
          }

          if (!FuncBBIDs.insert(*bbId).second)
            return invalidProfileError(
                Twine("Duplicate basic block id found '") + BBIndexStr + "'.");

          if (!bbId->MBBNumber && CurrentPosition)
            return invalidProfileError(
                "Entry BB (0) does not begin a cluster.");

          FI->second.first.emplace_back(BBTempClusterInfo{
              *bbId, CurrentCluster, CurrentPosition++});
        }
        CurrentCluster++;
      } else { // This is a function name specifier.
        // Function aliases are separated using '/'. We use the first function
        // name for the cluster info mapping and delegate all other aliases to
        // this one.
        SmallVector<StringRef, 4> Aliases;
        S.split(Aliases, '/');
        for (size_t i = 1; i < Aliases.size(); ++i)
          FuncAliasMap.try_emplace(Aliases[i], Aliases.front());

        // Prepare for parsing clusters of this function name.
        // Start a new cluster map for this function name.
        FI = ProgramBBClusterInfo.try_emplace(Aliases.front()).first;
        CurrentCluster = 0;
        FuncBBIDs.clear();
      }
    }
  }
  return Error::success();
}

bool BasicBlockSections::doInitialization(Module &M) {
  if (!MBuf)
    return false;
  if (auto Err = getBBClusterInfo(MBuf, ProgramBBTemporaryInfo, FuncAliasMap))
    report_fatal_error(std::move(Err));
  return false;
}

void BasicBlockSections::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachineFunctionPass *
llvm::createBasicBlockSectionsPass(const MemoryBuffer *Buf) {
  return new BasicBlockSections(Buf);
}
