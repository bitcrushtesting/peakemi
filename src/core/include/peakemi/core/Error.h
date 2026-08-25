#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace peakemi {

/// Error taxonomy of the libraries. Core and HAL never produce user-visible
/// prose: they return a stable code plus untranslated diagnostic detail, and the
/// UI maps the code to a translated message (FR-APP-3).
enum class ErrorCode : std::uint8_t
{
    None,
    Cancelled,
    Timeout,
    NotConnected,
    TransportFailure,
    ProtocolViolation,
    InstrumentError,
    UnsupportedSetting,
    InvalidConfiguration,
    IoFailure,
    ParseFailure,
    SchemaVersionUnsupported,
    NoDriverMatch,
    NotImplemented
};

[[nodiscard]] std::string_view errorCodeKey(ErrorCode code);

/// Error value carried by std::expected across every port boundary (ADR-2).
struct Error
{
    ErrorCode code{ErrorCode::None};
    std::string detail; ///< Diagnostic context: instrument reply, path, value.

    [[nodiscard]] std::string message() const;
};

template<class T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string detail = {})
{
    return std::unexpected(Error{code, std::move(detail)});
}

} // namespace peakemi
