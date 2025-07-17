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

#ifndef PROPELLER_PERF_DATA_PATH_READER_H_
#define PROPELLER_PERF_DATA_PATH_READER_H_

#include "absl/functional/function_ref.h"
#include "absl/types/span.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/perfdata_reader.h"

namespace propeller {

// Reads and returns the LBR paths of a perfdata profile.
class PerfDataPathReader {
 public:
  // Does not take ownership of `perf_data_reader` and `address_mapper` which
  // should point to valid objects which will outlive the constructed
  // `PerfDataPathReader`.
  PerfDataPathReader(const PerfDataReader* perf_data_reader,
                     const BinaryAddressMapper* address_mapper)
      : perf_data_reader_(perf_data_reader), address_mapper_(address_mapper) {}

  PerfDataPathReader(const PerfDataPathReader&) = delete;
  PerfDataPathReader& operator=(const PerfDataPathReader&) = delete;
  PerfDataPathReader(PerfDataPathReader&&) = default;
  PerfDataPathReader& operator=(PerfDataPathReader&&) = default;

  // Reads intra-function paths from every LBR sample event and calls
  // `handle_paths_callback` on the set of paths captured from each sample.
  void ReadPathsAndApplyCallBack(
      absl::FunctionRef<void(absl::Span<const FlatBbHandleBranchPath>)>
          handle_paths_callback);

 private:
  const PerfDataReader* perf_data_reader_;
  const BinaryAddressMapper* address_mapper_;
};
}  // namespace propeller
#endif  // PROPELLER_PERF_DATA_PATH_READER_H_
