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

#ifndef PROPELLER_FUNCTION_CHAIN_INFO_H_
#define PROPELLER_FUNCTION_CHAIN_INFO_H_

#include <vector>

#include "absl/algorithm/container.h"
#include "propeller/cfg_id.h"

namespace propeller {

struct CFGScore {
  // Total score across all intra-function edges in a CFG.
  double intra_score = 0;
  // Total score across all inter-function edges for a CFG. We consider
  // only the outgoing edges to prevent from double counting.
  double inter_out_score = 0;
};

// This struct represents the layout information for one function, that is every
// basic block chain and its layout index within the global ordering.
struct FunctionChainInfo {
  struct BbBundle {
    std::vector<FullIntraCfgId> full_bb_ids;
  };
  // This struct represents a chain of basic blocks (belong to the function
  // associated with func_symbol) which are contiguous in the layout.
  struct BbChain {
    // Index of this basic block chain in the global layout (zero-based).
    unsigned layout_index;

    // Ids of basic blocks in this chain.
    std::vector<BbBundle> bb_bundles;

    // Constructor for building a BB chain. The 'full_bb_ids' vector must be
    // populated afterwards.
    explicit BbChain(unsigned _layout_index) : layout_index(_layout_index) {}

    // Returns the flattened vector of all BB ids in this chain in order.
    std::vector<FullIntraCfgId> GetAllBbs() const {
      std::vector<FullIntraCfgId> full_bb_ids;
      for (const auto &bundle : bb_bundles) {
        full_bb_ids.insert(full_bb_ids.end(), bundle.full_bb_ids.begin(),
                           bundle.full_bb_ids.end());
      }
      return full_bb_ids;
    }

    // Returns the total number of BBs in this chain.
    int GetNumBbs() const {
      return absl::c_accumulate(bb_bundles, 0,
                                [](int sum, const BbBundle &bundle) {
                                  return sum + bundle.full_bb_ids.size();
                                });
    }

    // Returns the id of the first BB in this chain.
    const FullIntraCfgId &GetFirstBb() const {
      return bb_bundles.front().full_bb_ids.front();
    }
  };

  // Associated CFG's function_index.
  int function_index = -1;

  // BB chains pertaining to this CFG.
  std::vector<BbChain> bb_chains = {};

  // Score of this CFG in the original layout.
  CFGScore original_score = {};

  // Score of this CFG in the computed layout.
  CFGScore optimized_score = {};

  // Index of the function's cold chain within the cold part.
  unsigned cold_chain_layout_index = 0;
};
}  // namespace propeller

#endif  // PROPELLER_FUNCTION_CHAIN_INFO_H_
