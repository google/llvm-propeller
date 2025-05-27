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

#include "propeller/addr2cu.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "propeller/status_testing_macros.h"

namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::propeller::Addr2Cu;
using ::propeller::CreateDWARFContext;
using ::testing::HasSubstr;

uint64_t GetSymbolAddress(const std::string &symmap, absl::string_view symbol) {
  std::ifstream fin(symmap.c_str());
  int64_t addr;
  std::string sym_type;
  std::string sym_name;
  while (fin >> std::dec >> addr >> sym_type >> sym_name)
    if (sym_name == symbol) return addr;
  return -1;
}

struct BinaryData {
  std::unique_ptr<llvm::MemoryBuffer> mem_buf;
  std::unique_ptr<llvm::object::ObjectFile> object_file;
};

// Primes `BinaryData` for test cases.
absl::StatusOr<BinaryData> SetupBinaryData(absl::string_view binary) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> mem_buf =
      llvm::MemoryBuffer::getFile(binary);
  if (!mem_buf) {
    return absl::FailedPreconditionError(absl::StrCat(
        "failed creating MemoryBuffer: %s", mem_buf.getError().message()));
  }

  llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> object_file =
      llvm::object::ObjectFile::createELFObjectFile(**mem_buf);
  if (!object_file) {
    return absl::FailedPreconditionError(
        absl::StrFormat("failed creating ELFObjectFile: %s",
                        llvm::toString(object_file.takeError())));
  }

  return BinaryData{.mem_buf = std::move(*mem_buf),
                    .object_file = std::move(*object_file)};
}

TEST(Addr2CuTest, ComdatFunc) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "test_comdat.bin");
  const std::string symmap = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "test_comdat.symmap");

  ASSERT_OK_AND_ASSIGN(BinaryData binary_data, SetupBinaryData(binary));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<llvm::DWARFContext> context,
                       CreateDWARFContext(*binary_data.object_file));

  EXPECT_THAT(Addr2Cu(*context).GetCompileUnitFileNameForCodeAddress(
                  GetSymbolAddress(symmap, /*symbol=*/"_ZN3Foo7do_workEv")),
              IsOkAndHolds("propeller/testdata/test_comdat_1.cc"));
}

TEST(Addr2CuTest, ComdatFuncHasNoDwp) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "test_comdat_with_dwp.bin");

  ASSERT_OK_AND_ASSIGN(BinaryData binary_data, SetupBinaryData(binary));

  EXPECT_THAT(CreateDWARFContext(*binary_data.object_file),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("without a corresponding dwp file")));
}

TEST(Addr2CuTest, ComdatFuncHasDwp) {
  const std::string binary = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "test_comdat_with_dwp.bin");
  const std::string symmap = absl::StrCat(::testing::SrcDir(),
                                          "_main/propeller/testdata/"
                                          "test_comdat_with_dwp.symmap");
  const std::string dwp = absl::StrCat(::testing::SrcDir(),
                                       "_main/propeller/testdata/"
                                       "test_comdat_with_dwp.dwp");

  ASSERT_OK_AND_ASSIGN(BinaryData binary_data, SetupBinaryData(binary));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<llvm::DWARFContext> context,
                       CreateDWARFContext(*binary_data.object_file, dwp));

  EXPECT_THAT(Addr2Cu(*context).GetCompileUnitFileNameForCodeAddress(
                  GetSymbolAddress(symmap, "_ZN3Foo7do_workEv")),
              IsOkAndHolds("propeller/testdata/test_comdat_1.cc"));
}
}  // namespace
