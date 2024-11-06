#ifndef PROPELLER_RESOLVE_MMAP_NAME_H_
#define PROPELLER_RESOLVE_MMAP_NAME_H_
#include <string>

#include "propeller/propeller_options.pb.h"

namespace propeller {
// Returns the name of the profiled binary, which will be used to identify
// relevant Perf MMAP events. Returns the name of the binary, or it may return
// an empty string, which indicates that the build ID should be used instead.
std::string ResolveMmapName(const PropellerOptions &options);
}  // namespace propeller
#endif  // PROPELLER_RESOLVE_MMAP_NAME_H_
