#pragma once

#include <peakemi/core/ITransport.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace peakemi::test {

/// A fake transport that replays a recorded SCPI transcript.
///
/// Component tests drive a real driver against this instead of an instrument
/// (architecture.md 12): every command is recorded for assertions, and queries
/// are answered from a scripted table.
class ScriptedTransport final : public ITransport
{
public:
    explicit ScriptedTransport(std::map<std::string, std::string> responses = {})
        : m_responses{std::move(responses)}
    {}

    void setResponse(std::string command, std::string response)
    {
        m_responses[std::move(command)] = std::move(response);
    }

    /// Make the next @p count operations fail, to exercise the retry paths.
    void failNextWrites(int count, ErrorCode code = ErrorCode::TransportFailure)
    {
        m_failingWrites = count;
        m_failureCode = code;
    }

    [[nodiscard]] const std::vector<std::string>& commands() const { return m_commands; }

    [[nodiscard]] bool sawCommandStartingWith(std::string_view prefix) const
    {
        return std::any_of(m_commands.begin(), m_commands.end(), [prefix](const std::string& sent) {
            return sent.rfind(prefix, 0) == 0;
        });
    }

    /// Position of the first command with @p prefix in the transcript, or -1.
    /// Lets a test assert the order of two settings, which matters when one is
    /// interpreted in terms of the other.
    [[nodiscard]] int indexOfCommandStartingWith(std::string_view prefix) const
    {
        const auto found =
            std::find_if(m_commands.begin(), m_commands.end(), [prefix](const std::string& sent) {
                return sent.rfind(prefix, 0) == 0;
            });
        return found == m_commands.end() ? -1 : static_cast<int>(found - m_commands.begin());
    }

    [[nodiscard]] Status open() override
    {
        m_open = true;
        return {};
    }

    [[nodiscard]] bool isOpen() const override { return m_open; }

    void close() override { m_open = false; }

    [[nodiscard]] Status write(std::string_view command) override
    {
        if (!m_open) {
            return fail(ErrorCode::NotConnected, "scripted transport is closed");
        }
        if (m_failingWrites > 0) {
            --m_failingWrites;
            return fail(m_failureCode, "scripted failure");
        }
        m_commands.emplace_back(command);
        m_pending.assign(command);
        return {};
    }

    [[nodiscard]] Result<std::string> read(std::chrono::milliseconds /*timeout*/,
                                           const CancelToken& cancel) override
    {
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "cancelled");
        }
        const auto found = m_responses.find(m_pending);
        if (found == m_responses.end()) {
            return fail(ErrorCode::Timeout, "no scripted response for '" + m_pending + "'");
        }
        // One reply per query, as an instrument does: a second read with no
        // query in between finds nothing. Drivers that drain the line until a
        // read fails would otherwise never see it run dry.
        m_pending.clear();
        return found->second;
    }

    [[nodiscard]] Result<std::vector<std::byte>>
    readBinaryBlock(std::chrono::milliseconds /*timeout*/, const CancelToken& /*cancel*/) override
    {
        return fail(ErrorCode::NotImplemented, "scripted transport has no binary payloads");
    }

    void clear() override { m_pending.clear(); }

    [[nodiscard]] TransportDescriptor descriptor() const override
    {
        return TransportDescriptor{.kind = TransportKind::Tcp,
                                   .address = "scripted",
                                   .port = 5025,
                                   .baudRate = 0,
                                   .terminator = "\n",
                                   .defaultTimeout = std::chrono::milliseconds{1000}};
    }

private:
    std::map<std::string, std::string> m_responses;
    std::vector<std::string> m_commands;
    std::string m_pending;
    bool m_open{false};
    int m_failingWrites{0};
    ErrorCode m_failureCode{ErrorCode::TransportFailure};
};

} // namespace peakemi::test
