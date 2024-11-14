#ifndef PROPELLER_LBR_BRANCH_AGGREGATOR_H_
#define PROPELLER_LBR_BRANCH_AGGREGATOR_H_

#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregation.h"
#include "propeller/branch_aggregator.h"
#include "propeller/lazy_evaluator.h"
#include "propeller/lbr_aggregation.h"
#include "propeller/lbr_aggregator.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// An implementation of `BranchAggregator` that builds a branch aggregation
// from aggregated LBR data.
class LbrBranchAggregator : public BranchAggregator {
 public:
  // Constructs an `LbrBranchAggregator` from an `LbrAggregation` directly.
  explicit LbrBranchAggregator(LbrAggregation aggregation,
                               PropellerStats stats = {});

  // Constructs an `LbrBranchAggregator` from an `LbrAggregator`. When the
  // aggregation is first needed, it will be obtained from the `LbrAggregator`
  // and cached for future use.
  explicit LbrBranchAggregator(
      std::unique_ptr<LbrAggregator> aggregator, PropellerOptions options,
      const BinaryContent& binary_content ABSL_ATTRIBUTE_LIFETIME_BOUND);

  // LbrBranchAggregator is move-only; define the move operations and explicitly
  // delete the copy operations.
  LbrBranchAggregator(LbrBranchAggregator&& other) = default;
  LbrBranchAggregator& operator=(LbrBranchAggregator&& other) = default;
  LbrBranchAggregator(const LbrBranchAggregator&) = delete;
  LbrBranchAggregator& operator=(const LbrBranchAggregator&) = delete;

  absl::StatusOr<absl::flat_hash_set<uint64_t>> GetBranchEndpointAddresses()
      override;

  absl::StatusOr<BranchAggregation> Aggregate(
      const BinaryAddressMapper& binary_address_mapper,
      PropellerStats& stats) override;

 private:
  struct LbrAggregationInputs {
    std::unique_ptr<LbrAggregator> aggregator;
    const PropellerOptions options;
    const BinaryContent& binary_content;
  };

  struct LbrAggregationOutputs {
    absl::StatusOr<LbrAggregation> aggregation;
    PropellerStats stats;
  };

  // Performs LBR aggregation, converting the inputs to outputs. This is a pure
  // function, and it lives within `LbrBranchAggregator` to have access to
  // `LbrAggregation{In,Out}puts`
  static LbrAggregationOutputs AggregateLbrData(LbrAggregationInputs inputs);

  // Lazily evaluates the LBR aggregation, caching the result after the first
  // evaluation.
  LazyEvaluator<LbrAggregationOutputs(LbrAggregationInputs)> lazy_aggregator_;
};
}  // namespace propeller

#endif  // PROPELLER_LBR_BRANCH_AGGREGATOR_H_
