#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/BasicBlockSectionUtils.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {
MachineBasicBlock *CloneMachineBasicBlock(MachineBasicBlock &bb) {
  auto MF = bb.getParent();

  // Pass nullptr as this new block doesn't directly correspond to an LLVM basic
  // block.
  auto MBB = MF->CreateMachineBasicBlock(nullptr);
  MF->push_back(MBB);

  for (auto &instr : bb.instrs()) {
    MBB->push_back(MF->CloneMachineInstr(&instr));
  }

  return MBB;
}

// Converts the path from the from block to the to block to be a fallthrough.
// Requires to to be a successor of from.
// to_block must be placed after from_block in the layout after this call!
// Returns true if the conversion could not be completed.
// If the conversion fails, the parameters will not have changed.
bool ConvertToFallthrough(const TargetInstrInfo *TII,
                          MachineBasicBlock *from_block,
                          const MachineBasicBlock *to_block) {
  assert(from_block->isSuccessor(to_block) &&
         "The given block must be a successor of from block.");
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;

  if (TII->analyzeBranch(*from_block, TBB, FBB, Cond)) {
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
  }

  llvm_unreachable("All cases are handled.");
}

class HotPath : public MachineFunctionPass {
public:
  static char ID;

  HotPath() : MachineFunctionPass(ID) {
    initializeHotPathPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    auto TII = MF.getSubtarget().getInstrInfo();

    // The function name and the path to generate is hard coded. It will be
    // read from the llvm profile data.
    if (MF.getName().find("do_work") != llvm::StringRef::npos) {
      using HotPathBlocks = std::vector<MachineBasicBlock *>;

      HotPathBlocks hotpath;
      hotpath.push_back(&*std::next(MF.begin(), 1));
      hotpath.push_back(&*std::next(MF.begin(), 3));

      std::vector<HotPathBlocks> paths{std::move(hotpath)};

      for (auto &hot_path : paths) {
        auto block = hot_path[1]; // The middle block is to be cloned.

        // Remove the original block from the successors of the previous block.
        // The remove call removes pred_block from predecessors of block as
        // well.
        auto pred_block = hot_path[0];
        auto layout_succ = pred_block->getFallThrough();

        if (ConvertToFallthrough(TII, pred_block, block)) {
          WithColor::warning() << "Hot path generation failed.";
          continue;
        }

        // The pred_block falls through to block now,
        pred_block->removeSuccessor(block);

        auto cloned = CloneMachineBasicBlock(*block);

        // Add the successors of the original block as the new block's
        // successors as well.
        auto succ_end = block->succ_end();
        for (auto succ_it = block->succ_begin(); succ_it != succ_end;
             ++succ_it) {
          cloned->copySuccessor(block, succ_it);
        }

        // Add the block as a successor to the previous block in the hot path.
        // TODO: get this probability from the profile.
        pred_block->addSuccessor(cloned, BranchProbability::getOne());

        // The pred block always falls through to us.
        cloned->moveAfter(pred_block);

        // Not sure if we need this.
        pred_block->updateTerminator(layout_succ);

        if (auto original_ft = block->getFallThrough()) {
          // The original block has an implicit fall through.
          // Insert an explicit unconditional jump from the cloned block to that
          // same block.
          TII->insertUnconditionalBranch(*cloned, original_ft,
                                         cloned->findBranchDebugLoc());
        }

        assert(pred_block->getFallThrough() == cloned &&
               "Hot path pass did not generate a fall-through path!");

        for (auto &live : block->liveins()) {
          cloned->addLiveIn(live);
        }
      }
    }

    return true;
  }
};
} // namespace

INITIALIZE_PASS(HotPath, "hotpath-pass",
                "Creates long basic block chains for hot paths.", false, false)

char HotPath::ID = 0;

MachineFunctionPass *llvm::createHotPathPass() { return new HotPath(); }
