#include <peakemi/core/Error.hpp>

#include <array>

namespace peakemi {
namespace {

constexpr std::array<std::string_view, 14> ErrorKeys{
    "none",
    "cancelled",
    "timeout",
    "not-connected",
    "transport-failure",
    "protocol-violation",
    "instrument-error",
    "unsupported-setting",
    "invalid-configuration",
    "io-failure",
    "parse-failure",
    "schema-version-unsupported",
    "no-driver-match",
    "not-implemented",
};

} // namespace

std::string_view errorCodeKey(ErrorCode code)
{
    const auto index = static_cast<std::size_t>(code);
    return index < ErrorKeys.size() ? ErrorKeys[index] : ErrorKeys[0];
}

std::string Error::message() const
{
    std::string text{errorCodeKey(code)};
    if (!detail.empty()) {
        text += ": ";
        text += detail;
    }
    return text;
}

} // namespace peakemi
