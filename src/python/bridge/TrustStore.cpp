#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/Logging.h>
#include <peakemi/python/TrustStore.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace peakemi::python {
namespace {

using Json = nlohmann::json;

constexpr const char* SchemaName = "peakemi.plugin-trust";
constexpr int SchemaVersion = 1;

/// Compare by absolute path: the same plugin reached through a relative path
/// and an absolute one is one plugin, and must not be trusted twice.
[[nodiscard]] QString canonical(const QString& path)
{
    const QFileInfo info{path};
    const auto resolved = info.canonicalFilePath();
    return resolved.isEmpty() ? info.absoluteFilePath() : resolved;
}

} // namespace

TrustStore::TrustStore() : TrustStore{defaultPath()} {}

TrustStore::TrustStore(QString path) : m_path{std::move(path)}
{
    if (auto status = load(); !status && status.error().code != ErrorCode::IoFailure) {
        qCWarning(lcDriver) << "ignoring the plugin trust store:"
                            << QString::fromStdString(status.error().message());
    }
}

QString TrustStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/plugin-trust.json");
}

Result<std::string> TrustStore::hashFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::IoFailure,
                    path.toStdString() + ": " + file.errorString().toStdString());
    }
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&file)) {
        return fail(ErrorCode::IoFailure, path.toStdString() + ": cannot read the plugin");
    }
    return hash.result().toHex().toStdString();
}

const TrustEntry* TrustStore::find(const QString& path) const
{
    const auto resolved = canonical(path);
    const auto found =
        std::find_if(m_entries.begin(), m_entries.end(), [&](const TrustEntry& entry) {
            return entry.path == resolved;
        });
    return found == m_entries.end() ? nullptr : &*found;
}

TrustState TrustStore::state(const QString& path) const
{
    const auto* entry = find(path);
    if (entry == nullptr) {
        return TrustState::Unknown;
    }
    const auto current = hashFile(path);
    if (!current) {
        return TrustState::Unknown;
    }
    // An approved file that has been edited is not the file that was approved.
    return *current == entry->sha256 ? TrustState::Trusted : TrustState::Changed;
}

bool TrustStore::isTrusted(const QString& path) const
{
    return state(path) == TrustState::Trusted;
}

Status TrustStore::approve(const QString& path)
{
    auto hash = hashFile(path);
    if (!hash) {
        return std::unexpected(hash.error());
    }

    const auto resolved = canonical(path);
    std::erase_if(m_entries, [&](const TrustEntry& entry) { return entry.path == resolved; });
    m_entries.push_back(TrustEntry{
        .path = resolved, .sha256 = *hash, .approvedAt = std::chrono::system_clock::now()});
    qCInfo(lcDriver) << "plugin approved:" << resolved;
    return save();
}

Status TrustStore::revoke(const QString& path)
{
    const auto resolved = canonical(path);
    std::erase_if(m_entries, [&](const TrustEntry& entry) { return entry.path == resolved; });
    return save();
}

Status TrustStore::load()
{
    auto content = readFile(m_path);
    if (!content) {
        return std::unexpected(content.error());
    }

    Json document = Json::parse(*content, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail(ErrorCode::ParseFailure, "the plugin trust store is not valid JSON");
    }
    if (document.value("schema", std::string{}) != SchemaName) {
        return fail(ErrorCode::ParseFailure, "not a PeakEmi plugin trust store");
    }

    m_entries.clear();
    if (!document.contains("plugins") || !document.at("plugins").is_array()) {
        return {};
    }
    for (const auto& entry : document.at("plugins")) {
        TrustEntry trusted;
        trusted.path = QString::fromStdString(entry.value("path", std::string{}));
        trusted.sha256 = entry.value("sha256", std::string{});
        if (trusted.path.isEmpty() || trusted.sha256.empty()) {
            continue;
        }
        trusted.approvedAt =
            fromIso8601(entry.value("approved_at", std::string{})).value_or(TimePoint{});
        m_entries.push_back(std::move(trusted));
    }
    return {};
}

Status TrustStore::save() const
{
    Json plugins = Json::array();
    for (const auto& entry : m_entries) {
        plugins.push_back(Json{{"path", entry.path.toStdString()},
                               {"sha256", entry.sha256},
                               {"approved_at", toIso8601(entry.approvedAt)}});
    }
    const Json document{
        {"schema", SchemaName}, {"schema_version", SchemaVersion}, {"plugins", std::move(plugins)}};
    return writeFileAtomically(m_path, document.dump(2));
}

} // namespace peakemi::python
