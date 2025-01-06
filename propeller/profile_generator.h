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

#ifndef PROPELLER_PROFILE_GENERATOR_H_
#define PROPELLER_PROFILE_GENERATOR_H_

#include <memory>

#include "absl/status/status.h"
#include "propeller/perf_data_provider.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
// Propeller interface for SWIG as well as create_llvm_prof.
absl::Status GeneratePropellerProfiles(const propeller::PropellerOptions &opts);

// Like above, but `opts.profiles` is ignored and `perf_data_provider` is
// used instead, and the perf data it yields is interpreted as `profile_type`.
// Returns an error if `profile_type` is not Perf LBR or SPE.
absl::Status GeneratePropellerProfiles(
    const PropellerOptions &opts,
    std::unique_ptr<PerfDataProvider> perf_data_provider,
    ProfileType profile_type = ProfileType::PERF_LBR);
}  // namespace propeller

#endif  // PROPELLER_PROFILE_GENERATOR_H_
