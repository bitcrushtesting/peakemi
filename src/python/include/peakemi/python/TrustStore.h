#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/Time.h>

#include <QString>

#include <string>
#include <vector>

namespace peakemi::python {

/// Whether a plugin file may be imported.
enum class TrustState : std::uint8_t
{
    /// Never approved. It will not be imported.
    Unknown,
    /// Approved, and the file still hashes to what was approved.
    Trusted,
    /// Approved earlier, but the file has changed since — treated as untrusted
    /// until the user approves the new contents.
    Changed
};

struct TrustEntry
{
    QString path;
    std::string sha256;
    TimePoint approvedAt{};

    friend bool operator==(const TrustEntry&, const TrustEntry&) = default;
};

/// Records which plugin files the user has explicitly approved (NFR-EXT-1).
///
/// Python plugins run with the user's full privileges, so importing one is a
/// decision the user has to make knowingly, per file and per content. Editing
/// an approved file revokes that approval: the hash no longer matches.
class TrustStore
{
public:
    /// Uses the store in the application's config directory.
    TrustStore();
    /// Uses a specific file, which is what the tests do.
    explicit TrustStore(QString path);

    [[nodiscard]] static QString defaultPath();

    /// SHA-256 of the file's contents, as lower-case hex.
    [[nodiscard]] static Result<std::string> hashFile(const QString& path);

    [[nodiscard]] TrustState state(const QString& path) const;
    [[nodiscard]] bool isTrusted(const QString& path) const;

    /// Record the file's current contents as approved.
    [[nodiscard]] Status approve(const QString& path);
    /// Withdraw approval; the plugin will not be imported again until approved.
    [[nodiscard]] Status revoke(const QString& path);

    [[nodiscard]] const std::vector<TrustEntry>& entries() const { return m_entries; }

    [[nodiscard]] Status load();
    [[nodiscard]] Status save() const;

private:
    [[nodiscard]] const TrustEntry* find(const QString& path) const;

    QString m_path;
    std::vector<TrustEntry> m_entries;
};

} // namespace peakemi::python
