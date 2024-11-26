#ifndef PROPELLER_PROFILE_H_
#define PROPELLER_PROFILE_H_

#include <memory>
#include <vector>

#include "absl/container/btree_map.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/function_chain_info.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

struct PropellerProfile {
  std::unique_ptr<ProgramCfg> program_cfg;
  // Layout of functions in each section.
  absl::btree_map<llvm::StringRef, std::vector<FunctionChainInfo>>
      functions_chain_info_by_section_name;
  PropellerStats stats;
};
}  // namespace propeller

#endif  // PROPELLER_PROFILE_H_
