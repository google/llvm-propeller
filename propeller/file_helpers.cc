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

#include "propeller/file_helpers.h"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

namespace propeller_file {

absl::StatusOr<std::string> GetContents(llvm::StringRef path) {
  // Open the file in binary mode if it exists.
  std::ifstream filestream(path.str(), std::ios_base::binary);
  if (!filestream) {
    return absl::FailedPreconditionError(
        (llvm::Twine("Failed to open file: ") + path +
         ". State: " + llvm::Twine(filestream.rdstate()))
            .str());
  }

  // Buffer the read into a string stream.
  std::ostringstream stringstream;
  stringstream << filestream.rdbuf();
  if (!filestream || !stringstream) {
    return absl::UnknownError(
        (llvm::Twine("Error during read: ") + path).str());
  }

  return stringstream.str();
}

absl::Status SetContents(llvm::StringRef path, llvm::StringRef contents) {
  std::ofstream filestream(path.str(), std::ios_base::binary);
  if (!filestream) {
    return absl::FailedPreconditionError(
        (llvm::Twine("Failed to open file: ") + path +
         ". State: " + llvm::Twine(filestream.rdstate()))
            .str());
  }

  filestream << contents.str();
  if (filestream.fail() || filestream.bad()) {
    return absl::UnknownError(
        (llvm::Twine("Failed to write to file: ") + path).str());
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> GetContentsIgnoringLines(
    llvm::StringRef path,
    llvm::ArrayRef<llvm::StringRef> ignored_line_prefixes) {
  // Open the file in binary mode if it exists.
  std::ifstream filestream(path.str(), std::ios_base::binary);
  if (!filestream) {
    return absl::FailedPreconditionError(
        (llvm::Twine("Failed to open file: ") + path +
         ". State: " + llvm::Twine(filestream.rdstate()))
            .str());
  }

  std::string contents;
  std::string line;
  while (std::getline(filestream, line)) {
    if (llvm::any_of(ignored_line_prefixes,
                     [&line](llvm::StringRef ignored_prefix) {
                       return llvm::StringRef(line).starts_with(ignored_prefix);
                     })) {
      continue;
    }
    contents += line;
    // If a newline was encountered (eof was not reached), add a newline to the
    // output.
    if (filestream.eof()) return contents;
    contents += '\n';
  }
  return contents;
}

}  // namespace propeller_file
