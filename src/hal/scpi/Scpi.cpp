#include <peakemi/hal/Scpi.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <sstream>

namespace peakemi::scpi {
namespace {

[[nodiscard]] bool isDigit(char character)
{
    return character >= '0' && character <= '9';
}

template<class Float>
[[nodiscard]] std::vector<double> parseReal(std::span<const std::byte> payload)
{
    std::vector<double> values;
    const std::size_t count = payload.size() / sizeof(Float);
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Float value{};
        std::memcpy(&value, payload.data() + i * sizeof(Float), sizeof(Float));
        values.push_back(static_cast<double>(value));
    }
    return values;
}

} // namespace

std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

InstrumentId parseIdn(std::string_view response)
{
    InstrumentId id;
    id.raw = trim(response);

    std::vector<std::string> fields;
    std::string current;
    for (const char character : id.raw) {
        if (character == ',') {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(trim(current));

    if (!fields.empty()) {
        id.manufacturer = fields[0];
    }
    if (fields.size() > 1) {
        id.model = fields[1];
    }
    if (fields.size() > 2) {
        id.serial = fields[2];
    }
    if (fields.size() > 3) {
        id.firmware = fields[3];
    }
    return id;
}

Result<BlockHeader> parseBlockHeader(std::string_view data)
{
    if (data.size() < 2 || data.front() != '#') {
        return fail(ErrorCode::ProtocolViolation, "response is not a definite-length block");
    }
    if (!isDigit(data[1])) {
        return fail(ErrorCode::ProtocolViolation, "block header digit count is not a digit");
    }
    const auto digits = static_cast<std::size_t>(data[1] - '0');
    if (digits == 0) {
        return fail(ErrorCode::ProtocolViolation, "indefinite-length blocks are not supported");
    }
    if (data.size() < 2 + digits) {
        return fail(ErrorCode::ProtocolViolation, "block header is truncated");
    }

    std::size_t payloadSize = 0;
    const auto lengthField = data.substr(2, digits);
    const auto [end, error] =
        std::from_chars(lengthField.data(), lengthField.data() + lengthField.size(), payloadSize);
    if (error != std::errc{} || end != lengthField.data() + lengthField.size()) {
        return fail(ErrorCode::ProtocolViolation,
                    "block length field '" + std::string{lengthField} + "' is not numeric");
    }
    return BlockHeader{.headerSize = 2 + digits, .payloadSize = payloadSize};
}

Result<std::vector<std::byte>> parseDefiniteLengthBlock(std::string_view response)
{
    auto header = parseBlockHeader(response);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (response.size() < header->headerSize + header->payloadSize) {
        return fail(ErrorCode::ProtocolViolation,
                    "block payload is truncated: expected " + std::to_string(header->payloadSize) +
                        " bytes, got " + std::to_string(response.size() - header->headerSize));
    }

    std::vector<std::byte> payload(header->payloadSize);
    std::memcpy(payload.data(), response.data() + header->headerSize, header->payloadSize);
    return payload;
}

Result<std::vector<double>> parseAsciiTrace(std::string_view response)
{
    std::vector<double> values;
    std::string current;
    const auto flush = [&]() -> bool {
        const auto text = trim(current);
        current.clear();
        if (text.empty()) {
            return true;
        }
        try {
            std::size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed == 0) {
                return false;
            }
            values.push_back(value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };

    for (const char character : response) {
        if (character == ',' || character == '\n' || character == '\r') {
            if (!flush()) {
                return fail(ErrorCode::ParseFailure, "trace value is not numeric");
            }
        } else {
            current.push_back(character);
        }
    }
    if (!flush()) {
        return fail(ErrorCode::ParseFailure, "trace value is not numeric");
    }
    if (values.empty()) {
        return fail(ErrorCode::ParseFailure, "trace response contained no values");
    }
    return values;
}

std::vector<double> parseReal32(std::span<const std::byte> payload)
{
    return parseReal<float>(payload);
}

std::vector<double> parseReal64(std::span<const std::byte> payload)
{
    return parseReal<double>(payload);
}

Result<std::pair<int, std::string>> parseErrorQueueEntry(std::string_view response)
{
    const auto text = trim(response);
    if (text.empty()) {
        return fail(ErrorCode::ParseFailure, "empty error queue response");
    }
    const auto comma = text.find(',');
    const auto codeText = trim(comma == std::string::npos ? text : text.substr(0, comma));

    int code = 0;
    const auto [end, error] =
        std::from_chars(codeText.data(), codeText.data() + codeText.size(), code);
    if (error != std::errc{} || end != codeText.data() + codeText.size()) {
        return fail(ErrorCode::ParseFailure, "error queue code '" + codeText + "' is not numeric");
    }

    std::string message;
    if (comma != std::string::npos) {
        message = trim(text.substr(comma + 1));
        if (message.size() >= 2 && message.front() == '"' && message.back() == '"') {
            message = message.substr(1, message.size() - 2);
        }
    }
    return std::pair<int, std::string>{code, message};
}

std::string formatHertz(Hertz frequency)
{
    return std::to_string(frequency.value());
}

std::string formatDecibel(Decibel value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.precision(2);
    stream << std::fixed << value.value();
    return stream.str();
}

} // namespace peakemi::scpi
