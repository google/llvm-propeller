#ifndef PROPELLER_FILE_HELPERS_H_
#define PROPELLER_FILE_HELPERS_H_

#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace propeller_file {

// Reads the contents of the file `file_name` into `output`.
absl::StatusOr<std::string> GetContents(absl::string_view path);

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
