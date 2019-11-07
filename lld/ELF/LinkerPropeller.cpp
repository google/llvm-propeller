#include "LinkerPropeller.h"

#include "Config.h"
#include "Propeller/Propeller.h"
#include "Propeller/PropellerConfig.h"

namespace lld {
namespace propeller {

void setupConfig() {
  using lld::elf::config;
  propellerConfig.optPropeller = config->propeller;
  propellerConfig.optLinkerOutputFile = config->outputFile;

#define COPY_CONFIG(ConfigName) \
    propellerConfig.opt##ConfigName = config->propeller##ConfigName
  COPY_CONFIG(BackwardJumpDistance);
  COPY_CONFIG(BackwardJumpWeight);
  COPY_CONFIG(ChainSplitThreshold);
  COPY_CONFIG(DebugSymbols);
  COPY_CONFIG(DumpCfgs);
  COPY_CONFIG(DumpSymbolOrder);
  COPY_CONFIG(FallthroughWeight);
  COPY_CONFIG(ForwardJumpDistance);
  COPY_CONFIG(ForwardJumpWeight);
  COPY_CONFIG(Opts);
  COPY_CONFIG(PrintStats);
  COPY_CONFIG(ReorderBlocks);
  COPY_CONFIG(ReorderFuncs);
  COPY_CONFIG(SplitFuncs);
#undef COPY_CONFIG
}

} // namespace propeller
} // namespace lld