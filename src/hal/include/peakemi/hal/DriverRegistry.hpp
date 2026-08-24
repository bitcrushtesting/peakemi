#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.hpp>
#include <peakemi/core/Error.hpp>
#include <peakemi/core/InstrumentId.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace peakemi::hal {

/// Score a driver claims for an identified instrument. Higher wins; zero means
/// "not mine" (FR-DIS-4).
enum class MatchScore : int
{
    None = 0,
    Vendor = 30,      ///< right manufacturer, unknown model
    ModelFamily = 70, ///< model matched a family pattern
    ExactModel = 100
};

/// Registry of driver factories contributed by the built-in C++ drivers and, in
/// a later milestone, by the Python bridge (architecture.md 4.3).
class DriverRegistry
{
public:
    using Factory = std::function<DriverPtr()>;
    using Matcher = std::function<int(const InstrumentId&)>;

    struct Entry
    {
        DriverInfo info;
        Matcher matcher;
        Factory factory;
    };

    struct Match
    {
        std::string driverId;
        std::string driverName;
        int score{0};
    };

    /// Process-wide registry used by the application; tests build their own.
    [[nodiscard]] static DriverRegistry& instance();

    void registerDriver(Entry entry);
    void clear();

    [[nodiscard]] std::vector<DriverInfo> drivers() const;
    [[nodiscard]] bool contains(const std::string& driverId) const;

    [[nodiscard]] Result<DriverPtr> create(const std::string& driverId) const;

    /// All drivers claiming @p id, best score first.
    [[nodiscard]] std::vector<Match> match(const InstrumentId& id) const;

    /// Unambiguous best match, or an error the UI turns into a manual
    /// driver-selection dialog — never a silent failure (FR-DIS-4).
    [[nodiscard]] Result<DriverPtr> createBestMatch(const InstrumentId& id) const;

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

/// Matcher for the common case: one manufacturer, a list of model patterns.
/// Patterns are matched case-insensitively; a `*` suffix makes it a family
/// pattern scoring lower than an exact model name.
[[nodiscard]] DriverRegistry::Matcher makeMatcher(std::string manufacturer,
                                                  std::vector<std::string> modelPatterns);

} // namespace peakemi::hal
