#ifndef PROPELLER_TELEMETRY_REPORTER_H_
#define PROPELLER_TELEMETRY_REPORTER_H_

#include "absl/functional/any_invocable.h"
#include "propeller/binary_content.h"
#include "propeller/propeller_statistics.h"

namespace propeller {
// Signature of a Propeller telemetry reporting function. The alias is a part
// of the public API of this module.
using PropellerTelemetryReporter =
    absl::AnyInvocable<void(const BinaryContent& binary_content,
                            const PropellerStats& propeller_stats) const>;

// Registers `reporter` in the global registry of Propeller telemetry reporting
// functions. Not safe to call concurrently.
void RegisterPropellerTelemetryReporter(PropellerTelemetryReporter reporter);

// Invokes all registered Propeller telemetry reporters. Not safe to call
// concurrently.
void InvokePropellerTelemetryReporters(const BinaryContent& binary_content,
                                       const PropellerStats& propeller_stats);

// Unregisters all Propeller telemetry reporting functions. To be only used in
// tests. Not safe to call concurrently.
void UnregisterAllPropellerTelemetryReportersForTest();
}  // namespace propeller

#endif  // PROPELLER_TELEMETRY_REPORTER_H_
