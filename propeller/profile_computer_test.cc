#include "propeller/profile_computer.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "propeller/status_testing_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/binary_content.h"
#include "propeller/branch_aggregation.h"
#include "propeller/branch_aggregator.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_id.h"
#include "propeller/perf_data_provider.h"
#include "propeller/profile.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

class MockBranchAggregator : public BranchAggregator {
 public:
  MOCK_METHOD(absl::StatusOr<absl::flat_hash_set<uint64_t>>,
              GetBranchEndpointAddresses, (), (override));
  MOCK_METHOD(absl::StatusOr<BranchAggregation>, Aggregate,
              (const BinaryAddressMapper &, PropellerStats &), (override));
};

MATCHER_P7(CfgNodeFieldsAre, function_index, bb_index, clone_number, bb_id,
           address, size, freq,
           absl::StrFormat("%s fields {function_index: %d, bb_index: %d, "
                           "clone_number: %d, bb_id: %d, address: 0x%llX, "
                           "size: 0x%llX, frequency: %llu}",
                           negation ? "doesn't have" : "has", function_index,
                           bb_index, clone_number, bb_id, address, size,
                           freq)) {
  return arg.function_index() == function_index && arg.bb_index() == bb_index &&
         arg.clone_number() == clone_number && arg.addr() == address &&
         arg.bb_id() == bb_id && arg.size() == size &&
         arg.CalculateFrequency() == freq;
}

static std::string GetPropellerTestDataFilePath(absl::string_view filename) {
  return absl::StrCat(::testing::SrcDir(),
                      "_main/propeller/testdata/",
                      filename);
}

TEST(ProfileComputerTest, CreateWithBranchAggregator) {
  auto branch_aggregator = std::make_unique<MockBranchAggregator>();
  EXPECT_CALL(*branch_aggregator, GetBranchEndpointAddresses)
      .WillOnce(Return(absl::flat_hash_set<uint64_t>()));
  EXPECT_CALL(*branch_aggregator, Aggregate)
      .WillOnce(Return(BranchAggregation()));

  PropellerOptions options;
  options.set_binary_name(GetPropellerTestDataFilePath("sample.bin"));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<BinaryContent> binary_content,
                       GetBinaryContent(options.binary_name()));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PropellerProfileComputer> profile_computer,
      PropellerProfileComputer::Create(options, binary_content.get(),
                                       std::move(branch_aggregator)));
  EXPECT_THAT(profile_computer->program_cfg(),
              Property("Cfgs", &ProgramCfg::GetCfgs, IsEmpty()));
}

}  // namespace
}  // namespace propeller
