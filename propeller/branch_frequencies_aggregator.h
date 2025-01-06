// Copyright 2024 The Propeller Authors.
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
