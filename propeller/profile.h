// Copyright 2025 The Propeller Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
