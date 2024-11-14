#ifndef DEVTOOLS_CROSSTOOL_AUTOFDO_PROTO_BRANCH_FREQUENCIES_AGGREGATOR_H_
#define DEVTOOLS_CROSSTOOL_AUTOFDO_PROTO_BRANCH_FREQUENCIES_AGGREGATOR_H_

#include <utility>

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/branch_frequencies_aggregator.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// `ProtoBranchFrequenciesAggregator` is an implementation of
// `BranchFrequenciesAggregator` that builds `BranchFrequencies` from a
// `BranchFrequenciesProto`.
class ProtoBranchFrequenciesAggregator : public BranchFrequenciesAggregator {
 public:
  // Directly create a ProtoBranchFrequenciesAggregator from a
  // BranchFrequenciesProto.
  static ProtoBranchFrequenciesAggregator Create(BranchFrequenciesProto proto);

  // ProtoBranchFrequenciesAggregator is copyable and movable; explicitly define
  // both the move operations and copy operations.
  ProtoBranchFrequenciesAggregator(const ProtoBranchFrequenciesAggregator&) =
      default;
  ProtoBranchFrequenciesAggregator& operator=(
      const ProtoBranchFrequenciesAggregator&) = default;
  ProtoBranchFrequenciesAggregator(ProtoBranchFrequenciesAggregator&&) =
      default;
  ProtoBranchFrequenciesAggregator& operator=(
      ProtoBranchFrequenciesAggregator&&) = default;

  absl::StatusOr<BranchFrequencies> AggregateBranchFrequencies(
      const PropellerOptions& options, const BinaryContent& binary_content,
      PropellerStats& stats) override;

 private:
  explicit ProtoBranchFrequenciesAggregator(BranchFrequenciesProto proto)
      : proto_(std::move(proto)) {}

  BranchFrequenciesProto proto_;
};

}  // namespace propeller

#endif  // DEVTOOLS_CROSSTOOL_AUTOFDO_PROTO_BRANCH_FREQUENCIES_AGGREGATOR_H_
