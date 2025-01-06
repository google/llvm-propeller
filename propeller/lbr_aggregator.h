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

#ifndef PROPELLER_LBR_AGGREGATOR_H_
#define PROPELLER_LBR_AGGREGATOR_H_

#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/lbr_aggregation.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
namespace propeller {
// `LbrAggregator` is an abstraction around producing an `LbrAggregation`,
// making the source of the aggregation (Perf data, memtrace, mock) opaque to
// the user of the aggregation.
class LbrAggregator {
 public:
  virtual ~LbrAggregator() = default;

  // Returns an `LbrAggregation` for the specified binary according to the given
  // options, or an `absl::Status` if a valid aggregation can't be produced.
  //
  // `AggregateLbrData` can fail for various reasons, depending on the
  // implementation.
  virtual absl::StatusOr<LbrAggregation> AggregateLbrData(
      const PropellerOptions& options, const BinaryContent& binary_content,
      PropellerStats& stats) = 0;
};

}  // namespace propeller

#endif  // PROPELLER_LBR_AGGREGATOR_H_
