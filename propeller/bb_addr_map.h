// Copyright 2026 The Propeller Authors.
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

#ifndef PROPELLER_BB_ADDR_MAP_BB_ADDR_MAP_H_
#define PROPELLER_BB_ADDR_MAP_BB_ADDR_MAP_H_

#include <string>

#include "propeller/bb_addr_map.pb.h"

namespace propeller {
// Returns the BbAddrMap for the given binary path. Returns an empty proto if
// failed to get the BbAddrMap.
// google3-begin
// This uses a std::string for binary_path instead of absl::string_view to be
// compatible with Go SWIG wrappers.
// google3-end
BbAddrMapPb GetBbAddrMap(const std::string& binary_path);
}  // namespace propeller

#endif  // PROPELLER_BB_ADDR_MAP_BB_ADDR_MAP_H_
