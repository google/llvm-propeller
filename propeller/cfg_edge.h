#ifndef PROPELLER_CFG_EDGE_H_
#define PROPELLER_CFG_EDGE_H_

#include <algorithm>
#include <ostream>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "propeller/cfg_edge_kind.h"

namespace propeller {

class CFGNode;  // This comment should do nothing!

// All instances of CFGEdge are owned by their cfg_.
class CFGEdge final {
 public:
  CFGEdge(CFGNode *n1, CFGNode *n2, int weight, CFGEdgeKind kind,
          bool inter_section)
      : src_(n1),
        sink_(n2),
        weight_(weight),
        kind_(kind),
        inter_section_(inter_section) {}

  CFGNode *src() const { return src_; }
  CFGNode *sink() const { return sink_; }
  int weight() const { return weight_; }
  CFGEdgeKind kind() const { return kind_; }
  bool inter_section() const { return inter_section_; }

  bool IsBranchOrFallthrough() const {
    return kind_ == CFGEdgeKind::kBranchOrFallthough;
  }
  bool IsCall() const { return kind_ == CFGEdgeKind::kCall; }
  bool IsReturn() const { return kind_ == CFGEdgeKind::kRet; }

  void IncrementWeight(int increment) { weight_ += increment; }

  // Decrements the weight of this edge by the minimum of `value` and `weight_`.
  // Returns the weight reduction applied.
  int DecrementWeight(int value) {
    int reduction = std::min(value, weight_);
    if (weight_ < value) {
      LOG(ERROR) << absl::StrFormat(
          "Edge weight is lower than value (%lld): %v", value, *this);
    }
    weight_ -= reduction;
    return reduction;
  }

  // Returns a string to be used as the label in the dot format.
  std::string GetDotFormatLabel() const {
    return absl::StrCat(GetDotFormatLabelForEdgeKind(kind_), "#", weight_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const CFGEdge &edge);

 private:
  CFGNode *src_ = nullptr;
  CFGNode *sink_ = nullptr;
  int weight_ = 0;
  const CFGEdgeKind kind_;
  // Whether the edge is across functions in different sections.
  bool inter_section_ = false;
};

std::ostream &operator<<(std::ostream &out, const CFGEdge &edge);

}  // namespace propeller
#endif  // PROPELLER_CFG_EDGE_H_
