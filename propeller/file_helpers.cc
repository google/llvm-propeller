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

#include "propeller/file_helpers.h"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace propeller_file {

absl::StatusOr<std::string> GetContents(absl::string_view path) {
  // Open the file in binary mode if it exists.
  std::ifstream filestream((std::string(path)), std::ios_base::binary);
  if (!filestream) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to open file: ", path, ". State: ", filestream.rdstate()));
  }

  // Buffer the read into a string stream.
  std::ostringstream stringstream;
  stringstream << filestream.rdbuf();
  if (!filestream || !stringstream) {
    return absl::UnknownError(absl::StrCat("Error during read: ", path));
  }

  return stringstream.str();
}

absl::StatusOr<std::string> GetContentsIgnoringCommentLines(
    absl::string_view path) {
  // Open the file in binary mode if it exists.
  std::ifstream filestream((std::string(path)), std::ios_base::binary);
  if (!filestream) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to open file: ", path, ". State: ", filestream.rdstate()));
  }

  std::string contents;
  std::string line;
  while (std::getline(filestream, line)) {
    if (line.starts_with("#")) continue;
    absl::StrAppend(&contents, line);
    // If a newline was encountered (eof was not reached), add a newline to the
    // output.
    if (filestream.eof()) return contents;
    absl::StrAppend(&contents, "\n");
  }
  return contents;
}

}  // namespace propeller_file
