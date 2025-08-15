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

#include "propeller/cfg_edge_kind.h"

#include <ostream>
#include <string>

#include "absl/log/log.h"

namespace propeller {
std::string GetCfgEdgeKindString(CFGEdgeKind kind) {
  switch (kind) {
    case CFGEdgeKind::kBranchOrFallthough:
      return "BranchOrFallthrough";
    case CFGEdgeKind::kCall:
      return "Call";
    case CFGEdgeKind::kRet:
      return "Return";
  }
  LOG(FATAL) << "Invalid edge kind.";
}

std::string GetDotFormatLabelForEdgeKind(CFGEdgeKind kind) {
  return GetCfgEdgeKindString(kind).substr(0, 1);
}

std::ostream& operator<<(std::ostream& os, const CFGEdgeKind& kind) {
  return os << GetCfgEdgeKindString(kind);
}

}  // namespace propeller
