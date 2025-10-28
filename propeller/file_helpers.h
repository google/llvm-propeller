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

#ifndef PROPELLER_FILE_HELPERS_H_
#define PROPELLER_FILE_HELPERS_H_

#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace propeller_file {

// Reads the contents of the file `path` and returns it as a string.
absl::StatusOr<std::string> GetContents(absl::string_view path);

// Reads the contents of the file `path` and returns it as a string, ignoring
// lines starting with '#'.
absl::StatusOr<std::string> GetContentsIgnoringCommentLines(
    absl::string_view path);

// Reads a binary proto from the given path. The proto type is inferred from the
// template parameter, which must be a proto message type.
template <typename T>
absl::StatusOr<T> GetBinaryProto(absl::string_view path) {
  std::ifstream filestream((std::string(path)));
  if (!filestream) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to open file: ", path, ". State: ", filestream.rdstate()));
  }

  T proto;
  if (!proto.ParseFromIstream(&filestream)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to parse proto from ", path));
  }
  return proto;
}
}  // namespace propeller_file

#endif  // PROPELLER_FILE_HELPERS_H_
