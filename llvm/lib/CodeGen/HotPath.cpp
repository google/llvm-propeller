#include "llvm/ADT/SmallSet.h"
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

  bool runOnMachineFunction(MachineFunction &MF) override { return true; }
};
} // namespace

INITIALIZE_PASS(HotPath, "hotpath-pass",
                "Creates long basic block chains for hot paths.", false, false)

char HotPath::ID = 0;

MachineFunctionPass *llvm::createHotPathPass() { return new HotPath(); }
