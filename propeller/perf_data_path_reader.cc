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

#include "propeller/perf_data_path_reader.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/perfdata_reader.h"
#include "src/quipper/perf_data.pb.h"

namespace propeller {

void PerfDataPathReader::ReadPathsAndApplyCallBack(
    absl::FunctionRef<void(absl::Span<const FlatBbHandleBranchPath>)>
        handle_paths_callback) {
  std::vector<FlatBbHandleBranchPath> result;
  const bool is_kernel_mode = perf_data_reader_->IsKernelMode();
  if (is_kernel_mode) LOG(INFO) << "Input binary is kernel";
  perf_data_reader_->ReadWithSampleCallBack(
      [&](const quipper::PerfDataProto_SampleEvent& event) {
        std::optional<uint32_t> opt_pid = perf_data_reader_->GetPid(event);
        if (!opt_pid.has_value()) return;
        uint32_t pid = *opt_pid;
        std::vector<FlatBbHandleBranchPath> paths;
        BinaryAddressBranchPath lbr_path(
            {.pid = pid,
             .sample_time = absl::FromUnixNanos(event.sample_time_ns())});
        const auto& branch_stack = event.branch_stack();
        if (branch_stack.empty()) return;
        for (int p = branch_stack.size() - 1; p >= 0; --p) {
          const auto& branch_entry = branch_stack.Get(p);
          uint64_t from = perf_data_reader_->RuntimeAddressToBinaryAddress(
              pid, branch_entry.from_ip());
          uint64_t to = perf_data_reader_->RuntimeAddressToBinaryAddress(
              pid, branch_entry.to_ip());
          lbr_path.branches.push_back({.from = from, .to = to});
        }
        handle_paths_callback(
            address_mapper_->ExtractIntraFunctionPaths(lbr_path));
      });
}
}  // namespace propeller
