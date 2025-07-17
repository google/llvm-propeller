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

#ifndef PROPELLER_CFG_EDGE_KIND_H_
#define PROPELLER_CFG_EDGE_KIND_H_

#include <ostream>
#include <string>

namespace propeller {

// Branch kind.
enum class CFGEdgeKind {
  kBranchOrFallthough,
  kCall,
  kRet,
};

std::string GetCfgEdgeKindString(CFGEdgeKind kind);
std::string GetDotFormatLabelForEdgeKind(CFGEdgeKind kind);
std::ostream& operator<<(std::ostream& os, const CFGEdgeKind& kind);

}  // namespace propeller
#endif  // PROPELLER_CFG_EDGE_KIND_H_
