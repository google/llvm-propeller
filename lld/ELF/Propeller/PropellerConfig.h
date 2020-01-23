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
  double optBackwardJumpWeight;
  std::vector<StringRef> optBBOrder;
  uint64_t optChainSplitThreshold;
  std::vector<std::string> optDebugSymbols;
  std::vector<std::string> optDumpCfgs;
  uint64_t optClusterMergeSizeThreshold;
  StringRef optDumpSymbolOrder;
  double optFallthroughWeight;
  uint64_t optForwardJumpDistance;
  double optForwardJumpWeight;
  bool optKeepNamedSymbols;
  StringRef optLinkerOutputFile;
  std::vector<std::string> optOpts;
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
