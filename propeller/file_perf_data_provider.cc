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
