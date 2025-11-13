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
#include <string>

#include "absl/container/btree_map.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/function_layout_info.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

// Contains profile information for a function.
struct FunctionProfileInfo {
  FunctionLayoutInfo layout_info;
};

// Contains profile information for functions in a section.
struct SectionProfileInfo {
  absl::btree_map<int, FunctionProfileInfo> profile_infos_by_function_index;
};

// Represents a Propeller profile, including CFGs, layout information and
// statistics.
struct PropellerProfile {
  std::unique_ptr<ProgramCfg> program_cfg;
  absl::btree_map<llvm::StringRef, SectionProfileInfo>
      profile_infos_by_section_name;
  PropellerStats stats;
  // Build ID of the binary for which the profile was collected.
  std::string build_id;
};
}  // namespace propeller

#endif  // PROPELLER_PROFILE_H_
