#include <peakemi/core/Logging.h>
#include <peakemi/hal/DriverRegistry.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace peakemi::hal {
namespace {

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

/// `pattern` may end in '*', which matches any suffix.
[[nodiscard]] bool matchesPattern(std::string_view model, std::string_view pattern)
{
    if (pattern.empty()) {
        return false;
    }
    if (pattern.back() == '*') {
        const auto prefix = pattern.substr(0, pattern.size() - 1);
        return model.size() >= prefix.size() && model.compare(0, prefix.size(), prefix) == 0;
    }
    return model == pattern;
}

} // namespace

DriverRegistry& DriverRegistry::instance()
{
    static DriverRegistry registry;
    return registry;
}

void DriverRegistry::registerDriver(Entry entry)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto existing =
        std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& stored) {
            return stored.info.id == entry.info.id;
        });
    if (existing != m_entries.end()) {
        *existing = std::move(entry);
        return;
    }
    qCDebug(lcDriver) << "registered driver" << QString::fromStdString(entry.info.id);
    m_entries.push_back(std::move(entry));
}

void DriverRegistry::clear()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_entries.clear();
}

std::vector<DriverInfo> DriverRegistry::drivers() const
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::vector<DriverInfo> infos;
    infos.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        infos.push_back(entry.info);
    }
    return infos;
}

bool DriverRegistry::contains(const std::string& driverId) const
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
        return entry.info.id == driverId;
    });
}

Result<DriverPtr> DriverRegistry::create(const std::string& driverId) const
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto found = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
        return entry.info.id == driverId;
    });
    if (found == m_entries.end() || !found->factory) {
        return fail(ErrorCode::NoDriverMatch, "no driver registered as '" + driverId + "'");
    }
    return found->factory();
}

std::vector<DriverRegistry::Match> DriverRegistry::match(const InstrumentId& id) const
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::vector<Match> matches;
    for (const auto& entry : m_entries) {
        const int score = entry.matcher ? entry.matcher(id) : 0;
        if (score > 0) {
            matches.push_back(
                Match{.driverId = entry.info.id, .driverName = entry.info.name, .score = score});
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return a.score > b.score;
    });
    return matches;
}

Result<DriverPtr> DriverRegistry::createBestMatch(const InstrumentId& id) const
{
    const auto matches = match(id);
    if (matches.empty()) {
        return fail(ErrorCode::NoDriverMatch,
                    "no driver claims '" + id.raw + "'; select one manually");
    }
    if (matches.size() > 1 && matches[0].score == matches[1].score) {
        return fail(ErrorCode::NoDriverMatch,
                    "drivers '" + matches[0].driverId + "' and '" + matches[1].driverId +
                        "' claim '" + id.raw + "' equally; select one manually");
    }
    return create(matches.front().driverId);
}

DriverRegistry::Matcher makeMatcher(std::string_view manufacturer,
                                    std::vector<std::string> modelPatterns)
{
    return [vendor = lowered(manufacturer),
            patterns = std::move(modelPatterns)](const InstrumentId& id) {
        if (lowered(id.manufacturer).find(vendor) == std::string::npos) {
            return static_cast<int>(MatchScore::None);
        }
        const auto model = lowered(id.model);
        int best = static_cast<int>(MatchScore::Vendor);
        for (const auto& pattern : patterns) {
            const auto lowerPattern = lowered(pattern);
            if (!matchesPattern(model, lowerPattern)) {
                continue;
            }
            const int score = lowerPattern.back() == '*' ? static_cast<int>(MatchScore::ModelFamily)
                                                         : static_cast<int>(MatchScore::ExactModel);
            best = std::max(best, score);
        }
        return best;
    };
}

} // namespace peakemi::hal
