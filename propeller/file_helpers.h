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

#ifndef PROPELLER_FILE_HELPERS_H_
#define PROPELLER_FILE_HELPERS_H_

#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

namespace propeller_file {

// Reads the contents of the file `path` and returns it as a string.
absl::StatusOr<std::string> GetContents(llvm::StringRef path);

// Writes the given `contents` to the file `path`, overwriting any existing
// file.
absl::Status SetContents(llvm::StringRef path, llvm::StringRef contents);

// Reads the contents of the file `path` and returns it as a string, ignoring
// lines starting with any of the given prefixes. This is useful for ignoring
// comments in the file.
absl::StatusOr<std::string> GetContentsIgnoringLines(
    llvm::StringRef path,
    llvm::ArrayRef<llvm::StringRef> ignored_line_prefixes);

// Reads a binary proto from the given path. The proto type is inferred from the
// template parameter, which must be a proto message type.
template <typename T>
absl::StatusOr<T> GetBinaryProto(llvm::StringRef path) {
  std::ifstream filestream(path.str());
  if (!filestream) {
    return absl::FailedPreconditionError(
        (llvm::Twine("Failed to open file: ") + path +
         ". State: " + llvm::Twine(filestream.rdstate()))
            .str());
  }

  T proto;
  if (!proto.ParseFromIstream(&filestream)) {
    return absl::FailedPreconditionError(
        (llvm::Twine("Failed to parse proto from ") + path).str());
  }
  return proto;
}
}  // namespace propeller_file

#endif  // PROPELLER_FILE_HELPERS_H_
