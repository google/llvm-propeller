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

#ifndef PROPELLER_CODE_LAYOUT_H_
#define PROPELLER_CODE_LAYOUT_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/types/span.h"
#include "llvm/ADT/StringRef.h"
#include "propeller/cfg.h"
#include "propeller/cfg_node.h"
#include "propeller/chain_cluster_builder.h"
#include "propeller/code_layout_scorer.h"
#include "propeller/function_layout_info.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

// Contains layout information for all functions in a section.
struct SectionLayoutInfo {
  absl::btree_map<int, FunctionLayoutInfo> layouts_by_function_index;
};

// Runs `CodeLayout` on every section in `program_cfg` and returns
// the code layout results as a map keyed by section names, and valued by the
// `SectionLayoutInfo` of all functions in each section.
absl::btree_map<llvm::StringRef, SectionLayoutInfo> GenerateLayoutBySection(
    const ProgramCfg& program_cfg,
    const PropellerCodeLayoutParameters& code_layout_params,
    PropellerStats::CodeLayoutStats& code_layout_stats);

// Performs code layout on a set of CFGs that belong to the same output section.
class CodeLayout {
 public:
  // `initial_chains` describes the cfg nodes that must be placed in single
  // chains initially to make chain merging faster.
  CodeLayout(const PropellerCodeLayoutParameters& code_layout_params,
             const std::vector<const ControlFlowGraph*>& cfgs,
             absl::flat_hash_map<int, std::vector<FunctionLayoutInfo::BbChain>>
                 initial_chains = {})
      : code_layout_scorer_(code_layout_params),
        cfgs_(cfgs),
        initial_chains_(std::move(initial_chains)) {}

  // This performs code layout on all cfgs in the instance and returns the
  // layout information for all functions.
  SectionLayoutInfo GenerateLayout();

  PropellerStats::CodeLayoutStats stats() const { return stats_; }

 private:
  const PropellerCodeLayoutScorer code_layout_scorer_;
  // CFGs targeted for code layout.
  const std::vector<const ControlFlowGraph*> cfgs_;
  // Initial node chains, specified as a map from every function index to the
  // vector of initial node chains for the corresponding CFG. Each node chain is
  // specified by a vector of bb_indexes of its nodes.
  const absl::flat_hash_map<int, std::vector<FunctionLayoutInfo::BbChain>>
      initial_chains_;
  PropellerStats::CodeLayoutStats stats_;

  // Returns the intra-procedural ext-tsp scores for the given CFGs given a
  // function for getting the address of each CFG node.
  // This is called by ComputeOrigLayoutScores and ComputeOptLayoutScores below.
  absl::flat_hash_map<int, CFGScore> ComputeCfgScores(
      absl::FunctionRef<uint64_t(const CFGNode*)>);

  // Returns the intra-procedural ext-tsp scores for the given CFGs under the
  // original layout.
  absl::flat_hash_map<int, CFGScore> ComputeOrigLayoutScores();

  // Returns the intra-procedural ext-tsp scores for the given CFGs under the
  // new layout, which is described by the 'clusters' parameter.
  absl::flat_hash_map<int, CFGScore> ComputeOptLayoutScores(
      absl::Span<const std::unique_ptr<const ChainCluster>> clusters);
};

}  // namespace propeller
#endif  // PROPELLER_CODE_LAYOUT_H_
