// Copyright 2025 The Propeller Authors.
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

#ifndef PROPELLER_PATH_PROFILE_AGGREGATOR_H_
#define PROPELLER_PATH_PROFILE_AGGREGATOR_H_

#include "absl/status/statusor.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_options.pb.h"
#include "propeller/program_cfg.h"

namespace propeller {
// Interface for aggregating path profiles.
class PathProfileAggregator {
 public:
  virtual ~PathProfileAggregator() = default;

  // Returns the aggregated path profile.
  virtual absl::StatusOr<ProgramPathProfile> Aggregate(
      const BinaryContent &binary_content,
      const BinaryAddressMapper &binary_address_mapper,
      const ProgramCfg &program_cfg) = 0;
};

}  // namespace propeller
#endif  // PROPELLER_PATH_PROFILE_AGGREGATOR_H_
