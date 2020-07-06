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

class HotPath : public MachineFunctionPass {
public:
  static char ID;

  HotPath() : MachineFunctionPass(ID) {
    initializeHotPathPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (MF.getName().find("do_work") != llvm::StringRef::npos) {

      using HotPathBlocks = std::vector<MachineBasicBlock *>;

      HotPathBlocks hotpath;
      hotpath.push_back(&*std::next(MF.begin(), 1));
      hotpath.push_back(&*std::next(MF.begin(), 3));

      std::vector<HotPathBlocks> paths{std::move(hotpath)};

      auto TII = MF.getSubtarget().getInstrInfo();

      for (auto &hot_path : paths) {
        auto block = hot_path[1]; // The middle block is to be cloned.

        auto cloned = CloneMachineBasicBlock(*block);

        // Add the successors of the original block as the new block's
        // successors as well.
        for (auto succ : block->successors()) {
          cloned->addSuccessorWithoutProb(succ);
        }

        // Remove the original block from the successors of the previous block.
        // The remove call removes pred_block from predecessors of block as
        // well.
        auto pred_block = hot_path[0];
        auto layout_succ = pred_block->getFallThrough();
        pred_block->removeSuccessor(block);

        // Add the block as a successor to the previous block in the hot path.
        pred_block->addSuccessor(cloned, BranchProbability::getOne());

        MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
        SmallVector<MachineOperand, 4> Cond;

        if (TII->analyzeBranch(*pred_block, TBB, FBB, Cond)) {
          llvm_unreachable("Could not analyze branch");
        }

        if (!TBB && !FBB) {
          // Falls through to it's successor, no need to modify the block.
        } else if (TBB && !FBB && Cond.empty()) {
          if (TBB != block) {
            llvm_unreachable("We were the successor of this block, it should "
                             "be jumping to us");
          }
          // The pred_block has an unconditional jump to the original block.
          // We need to remove that branch so it falls through to us.
          TII->removeBranch(*pred_block);
        } else if (TBB && !FBB) {
          // There's a conditional jump to a block. It could be jumping to the
          // original block, or it could be falling through to the original
          // block.
          if (TBB == block) {
            // Jumps to original block. We need to make this fall-through to us.
            // Need to invert the branch, make it jump to it's current fall
            // through and fall through to us.
            if (!TII->reverseBranchCondition(Cond)) {
              auto current_fallthrough = pred_block->getFallThrough();
              TII->removeBranch(*pred_block);
              TII->insertBranch(*pred_block, current_fallthrough, nullptr, Cond,
                                pred_block->findBranchDebugLoc());
            } else {
              // Could not reverse the condition, abort?
              llvm_unreachable("");
            }
          } else {
            // Falls through to original block, no need to modify.
          }
        } else if (TBB && FBB) {
          // The conditional has jump instructions in either direction. We can
          // eliminate one of the jumps and make it fall through to us.

          if (TBB == block) {
            // Make the true case fall through.
            if (!TII->reverseBranchCondition(Cond)) {
              std::swap(FBB, TBB);
            } else {
              // Could not reverse the condition, abort?
              llvm_unreachable("");
            }
          } else {
            // Make the false case fall through. This is trivial to do.
            assert(FBB == block);
          }

          TII->removeBranch(*pred_block);
          TII->insertBranch(*pred_block, TBB, nullptr, Cond,
                            pred_block->findBranchDebugLoc());
        }

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

        assert(pred_block->getFallThrough() == cloned);

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
