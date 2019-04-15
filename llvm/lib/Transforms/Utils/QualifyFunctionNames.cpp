#include "llvm/Transforms/Utils/QualifyFunctionNames.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils.h"
#include <string>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "qualify-function-names"

static cl::opt<bool> DoQualifyFunctionNames(
    "qualify-function-names", cl::init(false),
    cl::desc("HACK: Qualify function names by appending module name."));

static cl::opt<bool> UseFileName(
    "use-file-names", cl::init(false),
    cl::desc("HACK: Use file name when qualifying."));

std::string getMangledName(StringRef Orig) {
  llvm::SmallString<1024> Cleaned = Orig;
  llvm::sys::path::remove_dots(Cleaned, true);
  std::string CleanedString = Cleaned.str();
  std::replace(CleanedString.begin(), CleanedString.end(), '/', '_');
    std::replace(CleanedString.begin(), CleanedString.end(), '-', '_');
  return CleanedString;
}

PreservedAnalyses QualifyFunctionNames::run(Module &M,
                                            ModuleAnalysisManager &AM) {
  static const StringRef Separator = ".module.";
  if (!DoQualifyFunctionNames)
    return PreservedAnalyses::all();
  bool Changed = false;
  for (auto &F : M) {
    if (F.hasLocalLinkage() && F.hasName()) {
      StringRef OldName = F.getName();
      if (OldName.contains(Separator))
        continue;
      std::string ParentName = F.getParent()->getName().str();
      llvm::DISubprogram *SP = F.getSubprogram();
      if (UseFileName && SP) {
        if (const auto* File = SP->getFile()) {
          // llvm::SmallString<1024> Buff;
          // Buff = File->getDirectory().split("google3").second;
          // llvm::sys::path::append(Buff, File->getFilename());
          // ParentName = Buff.str();
          ParentName = File->getFilename().str();
        }
      }

      F.setName(F.getName() + Separator + getMangledName(ParentName.c_str()));
      StringRef NewName = F.getName();
      if (SP && !SP->getLinkageName().empty())
        SP->replaceLinkageName(NewName);
      Changed |= true;
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
