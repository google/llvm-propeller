#ifndef PROPELLER_CHAIN_MERGE_ORDER_H_
#define PROPELLER_CHAIN_MERGE_ORDER_H_

#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace propeller {
// This enum represents different merge orders for two chains S and U.
// S1 and S2 are the slices of the S chain when it is split. S2US1 is ignored
// since it is rarely beneficial.
enum class ChainMergeOrder {
  kSU,
  kS2S1U,
  kS1US2,
  kUS2S1,
  kS2US1,
};

inline absl::string_view GetMergeOrderName(ChainMergeOrder merge_order) {
  switch (merge_order) {
    case ChainMergeOrder::kSU:
      return "SU";
    case ChainMergeOrder::kS2S1U:
      return "S2S1U";
    case ChainMergeOrder::kS1US2:
      return "S1US2";
    case ChainMergeOrder::kUS2S1:
      return "US2S1";
    case ChainMergeOrder::kS2US1:
      return "S2US1";
  }
  LOG(FATAL) << "invalid merge order.";
}
}  // namespace propeller

#endif  // PROPELLER_CHAIN_MERGE_ORDER_H_
