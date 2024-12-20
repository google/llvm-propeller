#ifndef PROPELLER_SPE_PID_PROVIDER_H_
#define PROPELLER_SPE_PID_PROVIDER_H_

#include "absl/status/statusor.h"
#include "src/quipper/arm_spe_decoder.h"
namespace propeller {

// An interface for a class that provides a process ID (PID) for SPE records.
// Unlike LBR, SPE doesn't stamp records with a PID, only the TID.
class SpePidProvider {
 public:
  virtual ~SpePidProvider() = default;

  // Gets the PID of the process that the `record` refers to.
  virtual absl::StatusOr<int> GetPid(
      const quipper::ArmSpeDecoder::Record& record) const = 0;
};

}  // namespace propeller

#endif  // PROPELLER_SPE_PID_PROVIDER_H_
