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

#include "propeller/file_perf_data_provider.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "llvm/Support/MemoryBuffer.h"
#include "propeller/perf_data_provider.h"
#include "propeller/status_macros.h"

namespace propeller {

// Uses `FileReader::ReadFile` to read the content of the next file into a
// `BufferHandle`.
absl::StatusOr<std::optional<PerfDataProvider::BufferHandle>>
FilePerfDataProvider::GetNext() {
  if (index_ >= file_names_.size()) return std::nullopt;

  ASSIGN_OR_RETURN(std::unique_ptr<llvm::MemoryBuffer> perf_file_content,
                   file_reader_->ReadFile(file_names_[index_]));

  std::string description = absl::StrFormat(
      "[%d/%d] %s", index_ + 1, file_names_.size(), file_names_[index_]);
  ++index_;
  return BufferHandle{.description = std::move(description),
                      .buffer = std::move(perf_file_content)};
}

}  // namespace propeller
