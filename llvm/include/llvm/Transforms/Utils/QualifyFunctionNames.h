#ifndef LLVM_TRANSFORMS_UTILS_QUALIFYFUNCTIONNAMES_H
#define LLVM_TRANSFORMS_UTILS_QUALIFYFUNCTIONNAMES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

class QualifyFunctionNames : public PassInfoMixin<QualifyFunctionNames> {
public:
  PreservedAnalyses run(Module &F, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_QUALIFYFUNCTIONNAMES_H

