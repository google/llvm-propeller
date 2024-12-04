#include "propeller/perf_data_path_reader.h"

#include <cstdint>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "propeller/binary_address_branch_path.h"
#include "propeller/binary_address_mapper.h"
#include "src/quipper/perf_data.pb.h"

namespace propeller {

void PerfDataPathReader::ReadPathsAndApplyCallBack(
    absl::FunctionRef<void(absl::Span<const BbHandleBranchPath>)>
        handle_paths_callback) {
  std::vector<BbHandleBranchPath> result;
  perf_data_reader_->ReadWithSampleCallBack(
      [&](const quipper::PerfDataProto_SampleEvent &event) {
        std::vector<BbHandleBranchPath> paths;
        BinaryAddressBranchPath lbr_path(
            {.pid = event.pid(),
             .sample_time = absl::FromUnixNanos(event.sample_time_ns())});
        const auto &branch_stack = event.branch_stack();
        if (branch_stack.empty()) return;
        for (int p = branch_stack.size() - 1; p >= 0; --p) {
          const auto &branch_entry = branch_stack.Get(p);
          uint64_t from = perf_data_reader_->RuntimeAddressToBinaryAddress(
              event.pid(), branch_entry.from_ip());
          uint64_t to = perf_data_reader_->RuntimeAddressToBinaryAddress(
              event.pid(), branch_entry.to_ip());
          lbr_path.branches.push_back({.from = from, .to = to});
        }
        handle_paths_callback(
            address_mapper_->ExtractIntraFunctionPaths(lbr_path));
      });
}
}  // namespace propeller
