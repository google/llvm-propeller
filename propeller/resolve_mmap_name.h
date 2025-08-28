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

#ifndef PROPELLER_RESOLVE_MMAP_NAME_H_
#define PROPELLER_RESOLVE_MMAP_NAME_H_
#include <string>

#include "propeller/propeller_options.pb.h"

namespace propeller {
// Returns the name of the profiled binary, which will be used to identify
// relevant Perf MMAP events. Returns the name of the binary, or it may return
// an empty string, which indicates that the build ID should be used instead.
std::string ResolveMmapName(const PropellerOptions& options);
}  // namespace propeller
#endif  // PROPELLER_RESOLVE_MMAP_NAME_H_
