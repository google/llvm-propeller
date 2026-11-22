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

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "llvm/Support/MemoryBuffer.h"
#include "propeller/binary_content.h"
#include "propeller/perf_data_provider.h"
#include "propeller/perfdata_reader.h"

// FuzzTest must be included after LLVM headers if they conflict with system
// headers.
#include "testing/fuzzing/fuzztest.h"

namespace propeller {
namespace {

void SelectMMapsDoesNotCrash(std::string perf_data_content,
                             std::vector<std::string> match_mmap_names,
                             std::string binary_file_name,
                             std::string binary_build_id) {
  PerfDataProvider::BufferHandle perf_data = {
      .description = "fuzzed_perf_data",
      .buffer = llvm::MemoryBuffer::getMemBufferCopy(perf_data_content)};

  std::vector<absl::string_view> match_mmap_names_view;
  for (const auto& name : match_mmap_names) {
    match_mmap_names_view.push_back(name);
  }

  BinaryContent binary_content;
  binary_content.file_name = binary_file_name;
  binary_content.build_id = binary_build_id;

  SelectMMaps(perf_data, match_mmap_names_view, binary_content).IgnoreError();
}

FUZZ_TEST(PerfdataReaderFuzzTest, SelectMMapsDoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>(),
                 fuzztest::Arbitrary<std::vector<std::string>>(),
                 fuzztest::Arbitrary<std::string>(),
                 fuzztest::Arbitrary<std::string>());

}  // namespace
}  // namespace propeller
