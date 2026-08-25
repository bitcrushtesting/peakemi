#pragma once

#include <peakemi/core/Capabilities.h>
#include <peakemi/core/ITransport.h>
#include <peakemi/core/InstrumentId.h>
#include <peakemi/core/Trace.h>

#include <memory>
#include <string>
#include <vector>

namespace peakemi {

/// Static description of a driver, shown in the UI and used for matching.
struct DriverInfo
{
    std::string id;   ///< stable identifier, e.g. "siglent.ssa3000x"
    std::string name; ///< human readable
    std::string vendor;
    std::string version;
    std::string origin; ///< "built-in" or a plugin path
    std::vector<TransportKind> supportedTransports;
};

/// One error reported by the instrument's error queue.
struct InstrumentError
{
    int code{0};
    std::string message;
};

/// The single port every instrument is reached through (FR-HAL-1/2).
///
/// Contract:
/// * every call returns a value, never throws across this boundary (ADR-2);
/// * long operations poll the cancellation token and honour the timeout the
///   caller configured (FR-HAL-5);
/// * a driver tolerates being destroyed at any time, including mid-sweep
///   (FR-HAL-6) — the destructor aborts and closes;
/// * no SCPI string appears outside an implementation of this interface
///   (FR-HAL-4).
class AbstractAnalyzerDriver
{
public:
    AbstractAnalyzerDriver() = default;
    virtual ~AbstractAnalyzerDriver() = default;

    AbstractAnalyzerDriver(const AbstractAnalyzerDriver&) = delete;
    AbstractAnalyzerDriver& operator=(const AbstractAnalyzerDriver&) = delete;
    AbstractAnalyzerDriver(AbstractAnalyzerDriver&&) = delete;
    AbstractAnalyzerDriver& operator=(AbstractAnalyzerDriver&&) = delete;

    [[nodiscard]] virtual DriverInfo info() const = 0;
    [[nodiscard]] virtual Capabilities capabilities() const = 0;

    [[nodiscard]] virtual Status open(TransportPtr transport) = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual Result<InstrumentId> identify() = 0;
    [[nodiscard]] virtual Status configureSweep(const SweepParams& params) = 0;
    [[nodiscard]] virtual Status armAndTrigger(const CancelToken& cancel) = 0;
    [[nodiscard]] virtual Result<Trace> fetchTrace(const CancelToken& cancel) = 0;

    /// Stop the running acquisition. Callable from another thread.
    virtual void abort() = 0;

    [[nodiscard]] virtual std::vector<InstrumentError> lastErrors() = 0;

    /// Set the timeout for subsequent operations.
    virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
};

using DriverPtr = std::shared_ptr<AbstractAnalyzerDriver>;

} // namespace peakemi
