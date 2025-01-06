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
