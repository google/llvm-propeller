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
std::ostream &operator<<(std::ostream &os, const CFGEdgeKind &kind);

}  // namespace propeller
#endif  // PROPELLER_CFG_EDGE_KIND_H_
