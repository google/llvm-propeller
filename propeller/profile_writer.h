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

#ifndef PROPELLER_PROFILE_WRITER_H_
#define PROPELLER_PROFILE_WRITER_H_

#include <fstream>
#include <string>

#include "absl/log/log.h"
#include "propeller/function_prefetch_info.h"
#include "propeller/profile.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"

namespace propeller {
// Writes the propeller profiles to output files.
class PropellerProfileWriter {
 public:
  explicit PropellerProfileWriter(const PropellerOptions& options)
      : options_(options),
        profile_encoding_(GetProfileEncoding(options.cluster_out_version())) {}

  // Writes code layout result in `all_functions_cluster_info` into the output
  // file.
  void Write(const PropellerProfile& profile) const;

 private:
  struct ProfileEncoding;

  // Writes prefetch hints in `prefetch_info` to `out`.
  void WritePrefetchInfo(const FunctionPrefetchInfo& prefetch_info,
                         const ProgramCfg& program_cfg,
                         std::ofstream& out) const;

  struct ProfileEncoding {
    ClusterEncodingVersion version;
    std::string version_specifier;
    std::string function_name_specifier;
    std::string function_name_separator;
    std::string module_name_specifier;
    std::string cluster_specifier;
    std::string clone_path_specifier;
    std::string prefetch_hint_specifier;
    std::string prefetch_target_specifier;
  };

  static ProfileEncoding GetProfileEncoding(
      const ClusterEncodingVersion& version) {
    switch (version) {
      case ClusterEncodingVersion::VERSION_0:
        return {.version = version,
                .version_specifier = "v0",
                .function_name_specifier = "!",
                .function_name_separator = "/",
                .module_name_specifier = " M=",
                .cluster_specifier = "!!",
                .clone_path_specifier = "#NOT_SUPPORTED",
                .prefetch_hint_specifier = "#NOT_SUPPORTED",
                .prefetch_target_specifier = "#NOT_SUPPORTED"};
      case ClusterEncodingVersion::VERSION_1:
        return {.version = version,
                .version_specifier = "v1",
                .function_name_specifier = "f ",
                .function_name_separator = " ",
                .module_name_specifier = "m ",
                .cluster_specifier = "c",
                .clone_path_specifier = "p",
                .prefetch_hint_specifier = "i",
                .prefetch_target_specifier = "t"};
      default:
        LOG(FATAL) << "Unknown value for ClusterEncodingVersion: "
                   << static_cast<int>(version);
    }
  }
  PropellerOptions options_;
  ProfileEncoding profile_encoding_;
};
}  // namespace propeller
#endif  // PROPELLER_PROFILE_WRITER_H_
