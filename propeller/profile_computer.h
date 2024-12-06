#ifndef PROPELLER_PROFILE_COMPUTER_H_
#define PROPELLER_PROFILE_COMPUTER_H_

#include <memory>
#include <optional>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregator.h"
#include "propeller/path_node.h"
#include "propeller/path_profile_aggregator.h"
#include "propeller/perf_data_provider.h"
#include "propeller/profile.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

// Computes the `PropellerProfile` by reading the binary and profile.
// Example:
//    absl::StatusOr<std::unique_ptr<PropellerProfileComputer>>
//        profile_computer = Create(options);
//    absl::StatusOr<PropellerProfile>
//        profile = profile_computer->ComputeProfile();
class PropellerProfileComputer {
 public:
  // Creates a PropellerProfileComputer from a set of options. Requires that all
  // input profiles are of type PERF_LBR or PROFILE_TYPE_UNSPECIFIED.
  static absl::StatusOr<std::unique_ptr<PropellerProfileComputer>> Create(
      const PropellerOptions &options,
      ABSL_ATTRIBUTE_LIFETIME_BOUND absl::Nonnull<const BinaryContent *>
          binary_content);

  // Creates a PropellerProfileComputer from a set of options and a perf data
  // provider. Requires that all input profiles are of type PERF_LBR or
  // PROFILE_TYPE_UNSPECIFIED.
  static absl::StatusOr<std::unique_ptr<PropellerProfileComputer>> Create(
      const PropellerOptions &options,
      ABSL_ATTRIBUTE_LIFETIME_BOUND absl::Nonnull<const BinaryContent *>
          binary_content,
      std::unique_ptr<PerfDataProvider> perf_data_provider);

  // Creates a PropellerProfileComputer from an arbitrary branch aggregator and
  // binary content. If no binary content is provided, uses the binary specified
  // in `options`. The profiles specified in `options` are disregarded.
  static absl::StatusOr<std::unique_ptr<PropellerProfileComputer>> Create(
      const PropellerOptions &options,
      ABSL_ATTRIBUTE_LIFETIME_BOUND absl::Nonnull<const BinaryContent *>
          binary_content,
      std::unique_ptr<BranchAggregator> branch_aggregator,
      std::unique_ptr<PathProfileAggregator> path_profile_aggregator = nullptr);

  // Returns the propeller profile.
  absl::StatusOr<PropellerProfile> ComputeProfile() &&;

  const BinaryAddressMapper &binary_address_mapper() const {
    CHECK_NE(binary_address_mapper_, nullptr)
        << "Address mapper is not initialized.";
    return *binary_address_mapper_;
  }

  const ProgramCfg &program_cfg() const {
    CHECK_NE(program_cfg_, nullptr) << "Program CFG is not initialized.";
    return *program_cfg_;
  }

  const PropellerStats &stats() const { return stats_; }

 private:
  PropellerProfileComputer(
      const PropellerOptions &options,
      ABSL_ATTRIBUTE_LIFETIME_BOUND absl::Nonnull<const BinaryContent *>
          binary_content,
      std::unique_ptr<BranchAggregator> branch_aggregator,
      std::unique_ptr<PathProfileAggregator> path_profile_aggregator)
      : options_(options),
        branch_aggregator_(std::move(branch_aggregator)),
        path_profile_aggregator_(std::move(path_profile_aggregator)),
        binary_content_(binary_content) {}

  PropellerProfileComputer(const PropellerProfileComputer &) = delete;
  PropellerProfileComputer &operator=(const PropellerProfileComputer &) =
      delete;
  PropellerProfileComputer(PropellerProfileComputer &&) noexcept = delete;
  PropellerProfileComputer &operator=(PropellerProfileComputer &&) noexcept =
      delete;

  // Initializes the program profile (program cfg and program path profile).
  absl::Status InitializeProgramProfile();

  PropellerOptions options_;
  std::unique_ptr<BranchAggregator> branch_aggregator_;
  absl::Nullable<std::unique_ptr<PathProfileAggregator>>
      path_profile_aggregator_;
  absl::Nonnull<const BinaryContent *> binary_content_;
  PropellerStats stats_;
  std::unique_ptr<BinaryAddressMapper> binary_address_mapper_;
  std::unique_ptr<ProgramCfg> program_cfg_;
  std::optional<ProgramPathProfile> program_path_profile_;
};

}  // namespace propeller
#endif  // PROPELLER_PROFILE_COMPUTER_H_
