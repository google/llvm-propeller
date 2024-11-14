#ifndef PROPELLER_BRANCH_FREQUENCIES_AGGREGATOR_H_
#define PROPELLER_BRANCH_FREQUENCIES_AGGREGATOR_H_

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// `BranchFrequenciesAggregator` is an abstraction around producing
// `BranchFrequencies`, making the source of the frequency information opaque to
// the user.
class BranchFrequenciesAggregator {
 public:
  // The mandatory virtual destructor implicitly deletes some constructors, so
  // we must specify them explicitly.
  BranchFrequenciesAggregator() = default;
  BranchFrequenciesAggregator(const BranchFrequenciesAggregator&) = default;
  BranchFrequenciesAggregator& operator=(const BranchFrequenciesAggregator&) =
      default;
  BranchFrequenciesAggregator(BranchFrequenciesAggregator&&) = default;
  BranchFrequenciesAggregator& operator=(BranchFrequenciesAggregator&&) =
      default;
  virtual ~BranchFrequenciesAggregator() = default;

  // Returns `BranchFrequencies` for the specified binary according to the given
  // options, or an `absl::Status` if valid branch frequencies can't be
  // produced.
  virtual absl::StatusOr<BranchFrequencies> AggregateBranchFrequencies(
      const PropellerOptions& options, const BinaryContent& binary_content,
      PropellerStats& stats) = 0;
};

}  // namespace propeller

#endif  // PROPELLER_BRANCH_FREQUENCIES_AGGREGATOR_H_
