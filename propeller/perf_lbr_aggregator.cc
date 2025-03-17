// Copyright 2024 The Propeller Authors.
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

#include "propeller/perf_lbr_aggregator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "llvm/MC/MCInst.h"
#include "propeller/binary_address_branch.h"
#include "propeller/binary_content.h"
#include "propeller/lbr_aggregation.h"
#include "propeller/mini_disassembler.h"
#include "propeller/mmap_match_criteria.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perfdata_reader.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_macros.h"

namespace propeller {

absl::StatusOr<LbrAggregation> PerfLbrAggregator::AggregateLbrData(
    const PropellerOptions &options, const BinaryContent &binary_content,
    PropellerStats &stats) {
  PropellerStats::ProfileStats &profile_stats = stats.profile_stats;
  LbrAggregation lbr_aggregation;

  while (true) {
    ASSIGN_OR_RETURN(std::optional<PerfDataProvider::BufferHandle> perf_data,
                     perf_data_provider_->GetNext());

    if (!perf_data.has_value()) break;

    const std::string description = perf_data->description;
    LOG(INFO) << "Parsing " << description << " ...";
    absl::StatusOr<PerfDataReader> perf_data_reader = BuildPerfDataReader(
        std::move(*perf_data), &binary_content, MMapMatchCriteria(options));
    if (!perf_data_reader.ok()) {
      LOG(WARNING) << "Skipped profile " << description << ": "
                   << perf_data_reader.status();
      continue;
    }

    profile_stats.binary_mmap_num += perf_data_reader->binary_mmaps().size();
    ++stats.profile_stats.perf_file_parsed;
    perf_data_reader->AggregateLBR(&lbr_aggregation);
  }
  profile_stats.br_counters_accumulated +=
      lbr_aggregation.GetNumberOfBranchCounters();
  if (profile_stats.br_counters_accumulated <= 100)
    LOG(WARNING) << "Too few branch records in perf data.";
  if (!profile_stats.perf_file_parsed) {
    return absl::FailedPreconditionError(
        "No perf file is parsed, cannot proceed.");
  }

  ASSIGN_OR_RETURN(stats.disassembly_stats,
                   CheckLbrAddress(lbr_aggregation, binary_content));
  return lbr_aggregation;
}

absl::StatusOr<PropellerStats::DisassemblyStats>
PerfLbrAggregator::CheckLbrAddress(const LbrAggregation &lbr_aggregation,
                                   const BinaryContent &binary_content) {
  PropellerStats::DisassemblyStats result = {};

  ASSIGN_OR_RETURN(std::unique_ptr<MiniDisassembler> disassembler,
                   MiniDisassembler::Create(binary_content.object_file.get()));

  absl::flat_hash_map<int64_t, int64_t> counter_sum_by_source_address;
  for (const auto &[branch, counter] : lbr_aggregation.branch_counters) {
    if (branch.from == kInvalidBinaryAddress) continue;
    counter_sum_by_source_address[branch.from] += counter;
  }

  for (const auto &[address, counter] : counter_sum_by_source_address) {
    absl::StatusOr<llvm::MCInst> inst = disassembler->DisassembleOne(address);
    if (!inst.ok()) {
      result.could_not_disassemble.Increment(counter);
      LOG(WARNING) << absl::StrFormat(
          "not able to disassemble address: 0x%x with counter sum %d", address,
          counter);
      continue;
    }
    if (!disassembler->MayAffectControlFlow(*inst)) {
      result.cant_affect_control_flow.Increment(counter);
      LOG(WARNING) << absl::StrFormat(
          "not a potentially-control-flow-affecting "
          "instruction at address: "
          "0x%x with counter sum %d, instruction name: %s",
          address, counter, disassembler->GetInstructionName(*inst));
    } else {
      result.may_affect_control_flow.Increment(counter);
    }
  }

  return result;
}

}  // namespace propeller
