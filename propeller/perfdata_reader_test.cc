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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/Twine.h"
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

TEST(PerfDataReaderTest, KernelRuntimeAddressToBinaryAddress) {
  BinaryContent binary_content;
  // Simulating ARM64 kernel layout where segment[0] starts at _text
  // (0xffffffc080000000) and .text section starts at _stext
  // (0xffffffc080010000), resulting in an offset of 0x10000 between
  // _stext and _text.
  binary_content.segments = {
      {.offset = 0x10000, .vaddr = 0xffffffc080000000, .memsz = 0x1000000}};
  binary_content.kernel_stext_offset = 0x10000;

  // Case 1: Linux perf records [kernel.kallsyms]_text anchored to _text.
  // Runtime address: randomized _text base (0xffffffd984e00000) + offset
  // (0x12340).
  BinaryMMaps text_mmaps;
  text_mmaps[PerfDataReader::kKernelPid] = {
      MMapEntry(PerfDataReader::kKernelPid, /*addr=*/0xffffffd984e00000,
                /*size=*/0x1000000, /*pgoff=*/0, "[kernel.kallsyms]_text")};
  PerfDataReader reader_text(PerfDataProvider::BufferHandle{},
                             std::move(text_mmaps), &binary_content);
  EXPECT_EQ(reader_text.RuntimeAddressToBinaryAddress(
                PerfDataReader::kKernelPid, 0xffffffd984e12340),
            0xffffffc080012340);

  // Case 2: Android simpleperf records [kernel.kallsyms]_stext anchored to
  // _stext. Runtime address: randomized _stext base (0xffffffd984e10000) +
  // offset (0x2340).
  BinaryMMaps stext_mmaps;
  stext_mmaps[PerfDataReader::kKernelPid] = {
      MMapEntry(PerfDataReader::kKernelPid, /*addr=*/0xffffffd984e10000,
                /*size=*/0x1000000, /*pgoff=*/0, "[kernel.kallsyms]_stext")};
  PerfDataReader reader_stext(PerfDataProvider::BufferHandle{},
                              std::move(stext_mmaps), &binary_content);
  EXPECT_EQ(reader_stext.RuntimeAddressToBinaryAddress(
                PerfDataReader::kKernelPid, 0xffffffd984e12340),
            0xffffffc080012340);
}
}  // namespace
}  // namespace propeller
