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

#ifndef PROPELLER_CLONE_APPLICATOR_H_
#define PROPELLER_CLONE_APPLICATOR_H_

#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "propeller/cfg.h"
#include "propeller/path_clone_evaluator.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

// Result of applying clonings to a `ProgramCfg`. `clone_cfgs_by_function_index`
// contains the resulting CFGs with clonings applied. `total_score_gain` is the
// total score gain from applying the clonings.
struct CloneApplicatorStats {
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>>
      clone_cfgs_by_function_index;
  double total_score_gain = 0;
};

// Applies all profitable clonings in `clonings_by_function_index` to
// clones of CFGs in `program_cfg`. Returns a `CloneApplicatorStats` struct
// containing the resulting CFGs with clonings applied and the total score gain
// from applying the clonings.
CloneApplicatorStats ApplyClonings(
    const PropellerCodeLayoutParameters& code_layout_params,
    const PathProfileOptions& path_profile_options,
    absl::flat_hash_map<int, std::vector<EvaluatedPathCloning>>
        clonings_by_function_index,
    const ProgramCfg& program_cfg,
    const absl::flat_hash_map<int, FunctionPathProfile>&
        path_profiles_by_function_index);

// Applies profitable clonings to `program_cfg` and returns the resulting
// `ProgramCfg`. Updates `cloning_stats` accordingly.
std::unique_ptr<ProgramCfg> ApplyClonings(
    const PropellerCodeLayoutParameters& code_layout_params,
    const PathProfileOptions& path_profile_options,
    const ProgramPathProfile& program_path_profile,
    std::unique_ptr<ProgramCfg> program_cfg,
    PropellerStats::CloningStats& cloning_stats);

}  //  namespace propeller
#endif  // PROPELLER_CLONE_APPLICATOR_H_
