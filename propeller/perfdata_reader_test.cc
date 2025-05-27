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

#include "propeller/perfdata_reader.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "propeller/binary_address_branch.h"
#include "propeller/binary_content.h"
#include "propeller/branch_frequencies.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/lbr_aggregation.h"
#include "propeller/perf_data_provider.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::Key;
using ::testing::Optional;
using ::testing::SizeIs;

TEST(PerfDataReaderTest, IsKernel) {
  BinaryContent binary_content;

  EXPECT_FALSE(PerfDataReader(PerfDataProvider::BufferHandle{},
                              /*binary_mmaps=*/{{0, {}}},
                              /*binary_content=*/nullptr)
                   .IsKernelMode());

  EXPECT_TRUE(
      PerfDataReader(PerfDataProvider::BufferHandle{},
                     /*binary_mmaps=*/{{PerfDataReader::kKernelPid, {}}},
                     /*binary_content=*/nullptr)
          .IsKernelMode());
}
}  // namespace
}  // namespace propeller
