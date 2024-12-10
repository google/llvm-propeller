#include "propeller/file_helpers.h"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace propeller_file {

absl::Status GetContents(absl::string_view path, std::string *output,
                         bool /*unused_param*/) {
  // Open the file in binary mode if it exists.
  std::ifstream filestream((std::string(path)), std::ios_base::binary);
  if (!filestream) return absl::NotFoundError(path);

  // Buffer the read into a string stream.
  std::ostringstream stringstream;
  stringstream << filestream.rdbuf();
  if (!filestream || !stringstream) {
    return absl::UnknownError(absl::StrCat("Error during read: ", path));
  }

  *output = stringstream.str();
  return absl::OkStatus();
}

}  // namespace propeller_file
