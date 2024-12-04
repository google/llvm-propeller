#ifndef PROPELLER_PERF_DATA_PATH_READER_H_
#define PROPELLER_PERF_DATA_PATH_READER_H_

#include "absl/functional/function_ref.h"
#include "absl/types/span.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/perfdata_reader.h"

namespace propeller {

// Reads and returns the LBR paths of a perfdata profile.
class PerfDataPathReader {
 public:
  // Does not take ownership of `perf_data_reader` and `address_mapper` which
  // should point to valid objects which will outlive the constructed
  // `PerfDataPathReader`.
  PerfDataPathReader(const PerfDataReader *perf_data_reader,
                     const BinaryAddressMapper *address_mapper)
      : perf_data_reader_(perf_data_reader), address_mapper_(address_mapper) {}

  PerfDataPathReader(const PerfDataPathReader &) = delete;
  PerfDataPathReader &operator=(const PerfDataPathReader &) = delete;
  PerfDataPathReader(PerfDataPathReader &&) = default;
  PerfDataPathReader &operator=(PerfDataPathReader &&) = default;

  // Reads intra-function paths from every LBR sample event and calls
  // `handle_paths_callback` on the set of paths captured from each sample.
  void ReadPathsAndApplyCallBack(
      absl::FunctionRef<void(absl::Span<const BbHandleBranchPath>)>
          handle_paths_callback);

 private:
  const PerfDataReader *perf_data_reader_;
  const BinaryAddressMapper *address_mapper_;
};
}  // namespace propeller
#endif  // PROPELLER_PERF_DATA_PATH_READER_H_
