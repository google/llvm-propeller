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

// A standalone tool to generate Propeller {cc,ld} profiles from a binary and
// input perf/proto profiles.
//
// `--profile`, `--cc_profile`, and `ld_profile` must be file paths to valid
// locations in the file system.
//
// `--profile` can refer to multiple profiles and should be specified by file
// path. If no profile type is specified, it is assumed to be Perf LBR data.
//
// Usage:
// ```
//   ./generate_propeller_profiles \
//     --binary=sample.bin \
//     --profile=sample.perfdata [--profile_type=perf_lbr] \
//     --cc_profile=sample_cc_profile.txt \
//     --ld_profile=sample_ld_profile.txt
// ```

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "propeller/profile_generator.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/text_proto_flag.h"

namespace {
enum class ProfileType {
  kPerfLbr,
  kPerfSpe,
  kFrequenciesProto,
};

inline bool AbslParseFlag(absl::string_view text, ProfileType* out,
                          std::string* err);

inline std::string AbslUnparseFlag(ProfileType type);
}  // namespace

ABSL_FLAG(std::string, binary, "", "Path to the binary.");
ABSL_FLAG(std::vector<std::string>, profile, {},
          "Comma-separated file paths of the input profile files.");
ABSL_FLAG(ProfileType, profile_type, ProfileType::kPerfLbr,
          "Type of input profiles (possible values: \"PERF_LBR\", "
          "\"PERF_SPE\", \"FREQUENCIES_PROTO\").");
ABSL_FLAG(std::string, cc_profile, "", "Output cc profile");
ABSL_FLAG(std::string, ld_profile, "", "Output ld profile");
ABSL_FLAG(propeller::TextProtoFlag<propeller::PropellerOptions>,
          propeller_options, {},
          "Override for propeller options (debug only).");

namespace {
using ::propeller::GeneratePropellerProfiles;
using ::propeller::InputProfile;
using ::propeller::PropellerOptions;

inline bool AbslParseFlag(absl::string_view text, ProfileType* out,
                          std::string* err) {
  const absl::flat_hash_map<absl::string_view, ProfileType> kFlagOptions = {
      {"PERF_LBR", ProfileType::kPerfLbr},
      {"PERF_SPE", ProfileType::kPerfSpe},
      {"FREQUENCIES_PROTO", ProfileType::kFrequenciesProto},
  };

  auto found = kFlagOptions.find(text);
  if (found != kFlagOptions.end()) {
    *out = found->second;
    return true;
  }
  *err = absl::StrCat("Unknown profile type \"", text, "\"");
  return false;
}

inline std::string AbslUnparseFlag(ProfileType type) {
  switch (type) {
    case ProfileType::kPerfLbr:
      return "PERF_LBR";
    case ProfileType::kPerfSpe:
      return "PERF_SPE";
    case ProfileType::kFrequenciesProto:
      return "FREQUENCIES_PROTO";
  }
}

propeller::ProfileType ToProtoProfileType(ProfileType type) {
  switch (type) {
    case ProfileType::kPerfLbr:
      return propeller::ProfileType::PERF_LBR;
    case ProfileType::kPerfSpe:
      return propeller::ProfileType::PERF_SPE;
    case ProfileType::kFrequenciesProto:
      return propeller::ProfileType::FREQUENCIES_PROTO;
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  absl::SetProgramUsageMessage(argv[0]);
  absl::ParseCommandLine(argc, argv);

  PropellerOptions options = absl::GetFlag(FLAGS_propeller_options).message;
  options.set_binary_name(absl::GetFlag(FLAGS_binary));
  options.set_cluster_out_name(absl::GetFlag(FLAGS_cc_profile));
  options.set_symbol_order_out_name(absl::GetFlag(FLAGS_ld_profile));

  for (const std::string& profile : absl::GetFlag(FLAGS_profile)) {
    InputProfile* input_profile = options.add_input_profiles();
    input_profile->set_name(profile);
    input_profile->set_type(
        ToProtoProfileType(absl::GetFlag(FLAGS_profile_type)));
  }

  QCHECK_OK(GeneratePropellerProfiles(options));
}
