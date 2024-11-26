#include "propeller/perf_branch_frequencies_aggregator.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perfdata_reader.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/resolve_mmap_name.h"
#include "propeller/status_macros.h"

namespace propeller {

absl::StatusOr<BranchFrequencies>
PerfBranchFrequenciesAggregator::AggregateBranchFrequencies(
    const PropellerOptions &options, const BinaryContent &binary_content,
    PropellerStats &stats) {
  PropellerStats::ProfileStats &profile_stats = stats.profile_stats;
  BranchFrequencies frequencies;

  while (true) {
    ASSIGN_OR_RETURN(std::optional<PerfDataProvider::BufferHandle> perf_data,
                     perf_data_provider_->GetNext());
    if (!perf_data.has_value()) break;

    const std::string description = perf_data->description;
    LOG(INFO) << "Parsing " << description << " ...";
    absl::StatusOr<PerfDataReader> perf_data_reader = BuildPerfDataReader(
        std::move(*perf_data), &binary_content, ResolveMmapName(options));
    if (!perf_data_reader.ok()) {
      LOG(WARNING) << "Skipped profile " << description << ": "
                   << perf_data_reader.status();
      continue;
    }

    profile_stats.binary_mmap_num += perf_data_reader->binary_mmaps().size();
    ++profile_stats.perf_file_parsed;
    RETURN_IF_ERROR(perf_data_reader->AggregateSpe(frequencies));
  }
  profile_stats.br_counters_accumulated +=
      frequencies.GetNumberOfTakenBranchCounters();
  if (profile_stats.br_counters_accumulated <= 100)
    LOG(WARNING) << "Too few branch records in perf data.";
  if (profile_stats.perf_file_parsed == 0) {
    return absl::FailedPreconditionError(
        "No perf file is parsed, cannot proceed.");
  }
  return frequencies;
}

}  // namespace propeller
