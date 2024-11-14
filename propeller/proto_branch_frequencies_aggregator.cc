#include "propeller/proto_branch_frequencies_aggregator.h"

#include <utility>

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/branch_frequencies.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

ProtoBranchFrequenciesAggregator ProtoBranchFrequenciesAggregator::Create(
    BranchFrequenciesProto proto) {
  return ProtoBranchFrequenciesAggregator(std::move(proto));
}

absl::StatusOr<BranchFrequencies>
ProtoBranchFrequenciesAggregator::AggregateBranchFrequencies(
    const PropellerOptions& options, const BinaryContent& binary_content,
    PropellerStats& stats) {
  return BranchFrequencies::Create(proto_);
}

}  // namespace propeller
