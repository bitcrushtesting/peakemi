#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/ITransport.h>

namespace peakemi::hal {

/// Builds the transport a descriptor names (FR-COM-5).
///
/// Every path that turns a persisted or command-line endpoint into a live
/// connection goes through here, so a bus this build left out is reported the
/// same way everywhere -- ErrorCode::NotImplemented naming the CMake option
/// that would have included it -- rather than as a null transport that only
/// fails once someone tries to read from it.
///
/// TransportKind::Simulated has no transport at all: the simulated driver *is*
/// the instrument. It is refused with ErrorCode::InvalidConfiguration, because
/// asking for one is a wiring mistake in the caller, not a missing feature.
[[nodiscard]] Result<TransportPtr> makeTransport(const TransportDescriptor& descriptor);

} // namespace peakemi::hal
