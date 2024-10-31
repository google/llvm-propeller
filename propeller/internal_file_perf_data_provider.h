#ifndef PROPELLER_INTERNAL_FILE_PERF_DATA_PROVIDER_H_
#define PROPELLER_INTERNAL_FILE_PERF_DATA_PROVIDER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "file/base/helpers.h"
#include "file/base/options.h"
#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "llvm/Support/MemoryBuffer.h"
#include "propeller/file_perf_data_provider.h"
#include "propeller/status_macros.h"

namespace propeller {

// Google-internal file reader using the Google3 file API.
class InternalFileReader : public FileReader {
 public:
  InternalFileReader() = default;
  InternalFileReader(const InternalFileReader&) = delete;
  InternalFileReader(InternalFileReader&&) = default;
  InternalFileReader& operator=(const InternalFileReader&) = delete;
  InternalFileReader& operator=(InternalFileReader&&) = default;

  absl::StatusOr<std::unique_ptr<llvm::MemoryBuffer>> ReadFile(
      absl::string_view file_name) override {
    ASSIGN_OR_RETURN(std::string perf_file_content,
                     file::GetContents(file_name, file::Defaults()),
                     _ << "When reading file " << file_name);
    return llvm::MemoryBuffer::getMemBufferCopy(perf_file_content,
                                                /*BufferName=*/"");
  }
};

class InternalFilePerfDataProvider : public FilePerfDataProvider {
 public:
  explicit InternalFilePerfDataProvider(std::vector<std::string> file_names)
      : FilePerfDataProvider(std::make_unique<InternalFileReader>(),
                             std::move(file_names)) {}
  InternalFilePerfDataProvider(const InternalFilePerfDataProvider&) = delete;
  InternalFilePerfDataProvider(InternalFilePerfDataProvider&&) = default;
  InternalFilePerfDataProvider& operator=(const InternalFilePerfDataProvider&) =
      delete;
  InternalFilePerfDataProvider& operator=(InternalFilePerfDataProvider&&) =
      default;
};

}  // namespace propeller

#endif  // PROPELLER_INTERNAL_FILE_PERF_DATA_PROVIDER_H_
