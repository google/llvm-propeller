// Copyright 2026 The Propeller Authors.
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

#ifndef PROPELLER_ML_CODE_LAYOUT_SCORER_H_
#define PROPELLER_ML_CODE_LAYOUT_SCORER_H_

#include "propeller/function_layout_info.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {

class NodeChain;
class NodeChainAssembly;

void LogAssemblyEvaluation(
    const propeller::PropellerCodeLayoutParameters& code_layout_params,
    const NodeChain& split_chain, const NodeChain& unsplit_chain,
    const NodeChainAssembly& assembly, bool is_chosen,
    int64_t global_decision_id);

double GetAssemblyScoreML(
    const propeller::PropellerCodeLayoutParameters& code_layout_params,
    const NodeChain& split_chain, const NodeChain& unsplit_chain,
    const NodeChainAssembly& assembly, bool is_chosen,
    int64_t global_decision_id);

}  // namespace propeller

#endif  // PROPELLER_ML_CODE_LAYOUT_SCORER_H_
