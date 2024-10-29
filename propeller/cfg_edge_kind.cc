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

std::ostream &operator<<(std::ostream &os, const CFGEdgeKind &kind) {
  return os << GetCfgEdgeKindString(kind);
}

}  // namespace propeller
