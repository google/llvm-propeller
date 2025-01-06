// Copyright 2024 The Propeller Authors.
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

#ifndef PROPELLER_CODE_LAYOUT_SCORER_H_
#define PROPELLER_CODE_LAYOUT_SCORER_H_

#include "propeller/cfg_edge.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {

// This class is used to calculate the layout's extended TSP score as described
// in https://ieeexplore.ieee.org/document/9050435. Specifically, it calculates
// the contribution of a single edge with a given distance based on the
// specified code layout parameters.
class PropellerCodeLayoutScorer {
 public:
  explicit PropellerCodeLayoutScorer(
      const PropellerCodeLayoutParameters &params);
  double GetEdgeScore(const CFGEdge &edge, int src_sink_distance) const;
  const PropellerCodeLayoutParameters &code_layout_params() const {
    return code_layout_params_;
  }

 private:
  const PropellerCodeLayoutParameters code_layout_params_;
};

}  // namespace propeller

#endif  // PROPELLER_CODE_LAYOUT_SCORER_H_
