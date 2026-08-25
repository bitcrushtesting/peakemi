#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/InstrumentId.h>
#include <peakemi/core/Units.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// SCPI plumbing shared by drivers. Everything here is pure text/byte handling
/// so it can be unit-tested without an instrument (architecture.md 12).
namespace peakemi::scpi {

/// Split a `*IDN?` response ("Siglent,SSA3032X,SSA3XABC,1.2.9.5") into fields.
/// Missing fields stay empty; the raw string is always preserved.
[[nodiscard]] InstrumentId parseIdn(std::string_view response);

/// Parse an IEEE 488.2 definite-length block: `#<n><length><payload>`.
/// Returns the payload only; trailing bytes after the payload are ignored.
[[nodiscard]] Result<std::vector<std::byte>> parseDefiniteLengthBlock(std::string_view response);

/// Number of header bytes of a definite-length block, or 0 if @p data does not
/// start with one. Useful for streaming readers that need the payload length
/// before the whole block has arrived.
struct BlockHeader
{
    std::size_t headerSize{0};
    std::size_t payloadSize{0};
};

[[nodiscard]] Result<BlockHeader> parseBlockHeader(std::string_view data);

/// Comma-separated ASCII trace values, e.g. "-71.2,-70.8,-72.0".
[[nodiscard]] Result<std::vector<double>> parseAsciiTrace(std::string_view response);

/// IEEE 754 little-endian float32/float64 payloads as delivered by REAL,32/64.
[[nodiscard]] std::vector<double> parseReal32(std::span<const std::byte> payload);
[[nodiscard]] std::vector<double> parseReal64(std::span<const std::byte> payload);

/// One entry of the instrument error queue: "-113,\"Undefined header\"".
[[nodiscard]] Result<std::pair<int, std::string>> parseErrorQueueEntry(std::string_view response);

/// Format a frequency for a SCPI command without exponent notation.
[[nodiscard]] std::string formatHertz(Hertz frequency);

/// Format a level with one decimal, locale-independent.
[[nodiscard]] std::string formatDecibel(Decibel value);

[[nodiscard]] std::string trim(std::string_view text);

} // namespace peakemi::scpi
