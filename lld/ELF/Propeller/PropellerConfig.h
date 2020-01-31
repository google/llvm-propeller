//===-------------------- PropellerConfig.h -------------------------------===//
//

#ifndef LLD_ELF_PROPELLER_CONFIG_H
#define LLD_ELF_PROPELLER_CONFIG_H

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

using llvm::StringRef;

namespace lld {
namespace propeller {

struct PropellerConfig {
  uint64_t optBackwardJumpDistance;
  uint64_t optBackwardJumpWeight;
  uint64_t optChainSplitThreshold;
  std::vector<llvm::StringRef> optDebugSymbols;
  std::vector<llvm::StringRef> optDumpCfgs;
  uint64_t optClusterMergeSizeThreshold;
  StringRef optDumpSymbolOrder;
  uint64_t optFallthroughWeight;
  uint64_t optForwardJumpDistance;
  uint64_t optForwardJumpWeight;
  bool optKeepNamedSymbols;
  StringRef optLinkerOutputFile;
  std::vector<llvm::StringRef> optOpts;
  bool optPrintStats;
  StringRef optPropeller;
  bool optReorderBlocks;
  bool optReorderFuncs;
  bool optSplitFuncs;
  bool optReorderIP;
};

extern PropellerConfig propellerConfig;

} // namespace propeller
} // namespace lld

#endif
