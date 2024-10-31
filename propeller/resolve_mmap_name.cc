#include "propeller/resolve_mmap_name.h"

#include <string>

namespace propeller {

std::string ResolveMmapName(const PropellerOptions &options) {
  if (options.has_profiled_binary_name()) {
    // If user specified "--profiled_binary_name", we use it.
    return options.profiled_binary_name();
  } else if (!options.ignore_build_id()) {
    // Return empty string so PerfDataReader::SelectPerfInfo auto
    // picks filename based on build-id, if build id is present; otherwise,
    // PerfDataReader::SelectPerfInfo uses options.binary_name to match mmap
    // event file name.
    return "";
  }
  return options.binary_name();
};
}  // namespace propeller
