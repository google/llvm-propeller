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
