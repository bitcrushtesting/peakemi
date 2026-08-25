#pragma once

#include <atomic>
#include <memory>

namespace peakemi {

/// Cheap, copyable cancellation handle passed engine -> driver -> transport.
///
/// Every operation that can block polls it (FR-HAL-5). Copies share one flag, so
/// a token held by the GUI thread cancels work running on an acquisition thread.
class CancelToken
{
public:
    CancelToken() : m_flag{std::make_shared<std::atomic_bool>(false)} {}

    void cancel() const noexcept { m_flag->store(true, std::memory_order_relaxed); }

    void reset() const noexcept { m_flag->store(false, std::memory_order_relaxed); }

    [[nodiscard]] bool isCancelled() const noexcept
    {
        return m_flag->load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic_bool> m_flag;
};

} // namespace peakemi
