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

#include "propeller/resolve_mmap_name.h"

#include <string>

namespace propeller {

std::string ResolveMmapName(const PropellerOptions &options) {
  if (options.has_profiled_binary_name()) {
    // If user specified "--profiled_binary_name", we use it.
    return options.profiled_binary_name();
  } else if (!options.ignore_build_id()) {
    // Return empty string so PerfDataReader::SelectPerfInfo auto
    // picks filename based on build-id, if build id is present; otherwise,
    // PerfDataReader::SelectPerfInfo uses options.binary_name to match mmap
    // event file name.
    return "";
  }
  return options.binary_name();
};
}  // namespace propeller
