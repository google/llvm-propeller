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
