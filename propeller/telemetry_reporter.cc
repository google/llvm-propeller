#include "propeller/telemetry_reporter.h"

#include <utility>
#include <vector>

#include "propeller/binary_content.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
namespace {
// Returns the global registry of Propeller telemetry reporters.
std::vector<PropellerTelemetryReporter>& GetPropellerTelemetryReporters() {
  static auto* const reporters = new std::vector<PropellerTelemetryReporter>;
  return *reporters;
}
}  // namespace

void RegisterPropellerTelemetryReporter(PropellerTelemetryReporter reporter) {
  GetPropellerTelemetryReporters().push_back(std::move(reporter));
}

void InvokePropellerTelemetryReporters(const BinaryContent& binary_content,
                                       const PropellerStats& propeller_stats) {
  for (const PropellerTelemetryReporter& reporter :
       GetPropellerTelemetryReporters()) {
    reporter(binary_content, propeller_stats);
  }
}

void UnregisterAllPropellerTelemetryReportersForTest() {
  GetPropellerTelemetryReporters().clear();
}
}  // namespace propeller
