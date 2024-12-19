#include "propeller/profile_generator.h"

#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregator.h"
#include "propeller/file_helpers.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/frequencies_branch_aggregator.h"
#include "propeller/lbr_branch_aggregator.h"
#include "propeller/path_profile_aggregator.h"
#include "propeller/perf_branch_frequencies_aggregator.h"
#include "propeller/perf_data_path_profile_aggregator.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perf_lbr_aggregator.h"
#include "propeller/profile.h"
#include "propeller/profile_computer.h"
#include "propeller/profile_writer.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/proto_branch_frequencies_aggregator.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "propeller/status_macros.h"

namespace propeller {
namespace {
using ::propeller_file::GetBinaryProto;

// Determines the type of the provided input profiles, returning an error if
// profile types are heterogeneous. For backwards compatibility reasons, assumes
// that unspecified profile types are PERF_LBR.
absl::StatusOr<ProfileType> GetProfileType(const PropellerOptions &opts) {
  if (opts.input_profiles().empty())
    return absl::InvalidArgumentError("no input profiles provided");

  absl::flat_hash_set<ProfileType> profile_types;
  absl::c_transform(
      opts.input_profiles(), std::inserter(profile_types, profile_types.end()),
      [](const InputProfile &profile) {
        if (profile.type() == ProfileType::PROFILE_TYPE_UNSPECIFIED)
          return ProfileType::PERF_LBR;
        return profile.type();
      });

  if (profile_types.size() > 1) {
    return absl::InvalidArgumentError("heterogeneous profile types");
  }

  return *profile_types.begin();
}

// Creates a perf data provider for the perf files in `opts.input_profiles`.
// Assumes that all input profile types are Perf LBR/SPE or unspecified.
std::unique_ptr<GenericFilePerfDataProvider> CreatePerfDataProvider(
    const PropellerOptions &opts) {
  std::vector<std::string> profile_names;
  absl::c_transform(opts.input_profiles(), std::back_inserter(profile_names),
                    [](const InputProfile &profile) { return profile.name(); });

  return std::make_unique<GenericFilePerfDataProvider>(
      std::move(profile_names));
}

// Fetches and merges the `BranchFrequenciesProto` messages from the provided
// `input_profiles`.
absl::StatusOr<BranchFrequenciesProto> FetchProtoProfile(
    const PropellerOptions &opts) {
  BranchFrequenciesProto proto;
  for (const InputProfile &profile : opts.input_profiles()) {
    ASSIGN_OR_RETURN(BranchFrequenciesProto profile_proto,
                     GetBinaryProto<BranchFrequenciesProto>(profile.name()));
    proto.MergeFrom(profile_proto);
  }
  return proto;
}

// Creates a branch aggregator for the provided profile type given the provided
// perf data provider.
absl::StatusOr<std::unique_ptr<BranchAggregator>> CreateBranchAggregator(
    ProfileType profile_type, const PropellerOptions &opts,
    const BinaryContent &binary_content,
    std::unique_ptr<PerfDataProvider> perf_data_provider) {
  switch (profile_type) {
    case ProfileType::PERF_LBR: {
      return std::make_unique<LbrBranchAggregator>(
          std::make_unique<PerfLbrAggregator>(std::move(perf_data_provider)),
          opts, binary_content);
    }
    case ProfileType::PERF_SPE: {
      return std::make_unique<FrequenciesBranchAggregator>(
          std::make_unique<PerfBranchFrequenciesAggregator>(
              std::move(perf_data_provider)),
          opts, binary_content);
    }
    default: {
      return absl::InvalidArgumentError(
          absl::StrCat("unsupported profile type ", profile_type));
    }
  }
}

// Creates a branch aggregator for the provided profile type.
absl::StatusOr<std::unique_ptr<BranchAggregator>> CreateBranchAggregator(
    ProfileType profile_type, const PropellerOptions &opts,
    const BinaryContent &binary_content) {
  if (profile_type == ProfileType::FREQUENCIES_PROTO) {
    ASSIGN_OR_RETURN(BranchFrequenciesProto proto, FetchProtoProfile(opts));
    return std::make_unique<FrequenciesBranchAggregator>(
        std::make_unique<ProtoBranchFrequenciesAggregator>(
            ProtoBranchFrequenciesAggregator::Create(std::move(proto))),
        opts, binary_content);
  }
  return CreateBranchAggregator(profile_type, opts, binary_content,
                                CreatePerfDataProvider(opts));
}

// Creates a path profile aggregator for the provided profile type.
absl::StatusOr<std::unique_ptr<PathProfileAggregator>>
CreatePathProfileAggregator(ProfileType profile_type,
                            const PropellerOptions &opts) {
  if (!opts.path_profile_options().enable_cloning()) return nullptr;

  if (profile_type != ProfileType::PERF_LBR) {
    return absl::FailedPreconditionError(
        "Cloning is only supported for PERF_LBR profiles");
  }
  return std::make_unique<PerfDataPathProfileAggregator>(
      opts, CreatePerfDataProvider(opts));
}

// Generates propeller profiles for the provided options.
absl::Status GeneratePropellerProfiles(
    const PropellerOptions &opts, std::unique_ptr<BinaryContent> binary_content,
    std::unique_ptr<BranchAggregator> branch_aggregator,
    std::unique_ptr<PathProfileAggregator> path_profile_aggregator) {
  ASSIGN_OR_RETURN(std::unique_ptr<PropellerProfileComputer> profile_computer,
                   PropellerProfileComputer::Create(
                       opts, binary_content.get(), std::move(branch_aggregator),
                       std::move(path_profile_aggregator)));
  ASSIGN_OR_RETURN(PropellerProfile profile,
                   std::move(*std::move(profile_computer)).ComputeProfile());

  PropellerProfileWriter(opts).Write(profile);
  LOG(INFO) << profile.stats.DebugString();

  return absl::OkStatus();
}
}  // namespace

absl::Status GeneratePropellerProfiles(const PropellerOptions &opts) {
  ASSIGN_OR_RETURN(ProfileType profile_type, GetProfileType(opts));
  ASSIGN_OR_RETURN(std::unique_ptr<BinaryContent> binary_content,
                   GetBinaryContent(opts.binary_name()));
  ASSIGN_OR_RETURN(std::unique_ptr<BranchAggregator> branch_aggregator,
                   CreateBranchAggregator(profile_type, opts, *binary_content));
  ASSIGN_OR_RETURN(
      std::unique_ptr<PathProfileAggregator> path_profile_aggregator,
      CreatePathProfileAggregator(profile_type, opts));
  return GeneratePropellerProfiles(opts, std::move(binary_content),
                                   std::move(branch_aggregator),
                                   std::move(path_profile_aggregator));
}

absl::Status GeneratePropellerProfiles(
    const PropellerOptions &opts,
    std::unique_ptr<PerfDataProvider> perf_data_provider,
    ProfileType profile_type) {
  ASSIGN_OR_RETURN(std::unique_ptr<BinaryContent> binary_content,
                   GetBinaryContent(opts.binary_name()));
  ASSIGN_OR_RETURN(std::unique_ptr<BranchAggregator> branch_aggregator,
                   CreateBranchAggregator(profile_type, opts, *binary_content,
                                          std::move(perf_data_provider)));
  // If we only have one perf_data_provider, we can't have both branch and
  // path data.
  return GeneratePropellerProfiles(opts, std::move(binary_content),
                                   std::move(branch_aggregator),
                                   /*path_profile_aggregator=*/nullptr);
}

}  // namespace propeller
