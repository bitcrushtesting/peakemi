#include <peakemi/python/PluginManifest.h>

#include <QRegularExpression>
#include <QString>

#include <charconv>

namespace peakemi::python {
namespace {

/// Major version of a "major.minor" string, or nullopt when malformed.
[[nodiscard]] std::optional<int> majorOf(std::string_view version)
{
    const auto dot = version.find('.');
    const auto majorText = dot == std::string_view::npos ? version : version.substr(0, dot);
    if (majorText.empty()) {
        return std::nullopt;
    }
    int major = 0;
    const auto [end, error] =
        std::from_chars(majorText.data(), majorText.data() + majorText.size(), major);
    if (error != std::errc{} || end != majorText.data() + majorText.size()) {
        return std::nullopt;
    }
    return major;
}

} // namespace

bool isApiVersionCompatible(std::string_view declared)
{
    const auto declaredMajor = majorOf(declared);
    const auto supportedMajor = majorOf(PluginApiVersion);
    if (!declaredMajor || !supportedMajor) {
        return false;
    }
    return *declaredMajor == *supportedMajor;
}

Status validateManifest(const PluginManifest& manifest)
{
    if (manifest.name.empty()) {
        return fail(ErrorCode::InvalidConfiguration, "the plugin manifest has no name");
    }
    if (manifest.apiVersion.empty()) {
        return fail(ErrorCode::InvalidConfiguration,
                    "plugin '" + manifest.name + "' declares no plugin API version");
    }
    if (!isApiVersionCompatible(manifest.apiVersion)) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "plugin '" + manifest.name + "' targets plugin API " + manifest.apiVersion +
                        ", this build speaks " + std::string{PluginApiVersion});
    }
    if (manifest.models.empty()) {
        return fail(ErrorCode::InvalidConfiguration,
                    "plugin '" + manifest.name + "' matches no instrument models");
    }
    for (const auto& pattern : manifest.models) {
        const QRegularExpression expression{QString::fromStdString(pattern)};
        if (!expression.isValid()) {
            return fail(ErrorCode::InvalidConfiguration,
                        "plugin '" + manifest.name + "' has an invalid model pattern '" + pattern +
                            "': " + expression.errorString().toStdString());
        }
    }
    return {};
}

int scoreManifest(const PluginManifest& manifest, const InstrumentId& identity)
{
    if (!manifest.vendor.empty()) {
        const auto vendor = QString::fromStdString(manifest.vendor);
        const auto reported = QString::fromStdString(identity.manufacturer);
        if (!reported.contains(vendor, Qt::CaseInsensitive)) {
            return 0;
        }
    }

    const auto model = QString::fromStdString(identity.model);
    int best = manifest.vendor.empty() ? 0 : 30; // vendor-only match
    for (const auto& pattern : manifest.models) {
        const auto expressionText = QString::fromStdString(pattern);
        const QRegularExpression expression{expressionText,
                                            QRegularExpression::CaseInsensitiveOption};
        if (!expression.isValid()) {
            continue;
        }
        const auto match = expression.match(model);
        if (!match.hasMatch()) {
            continue;
        }

        // Same rule as the C++ registry: naming one model exactly outranks
        // claiming a family. A pattern with metacharacters is a family claim
        // however much of the name it happens to cover, so a driver written
        // for this instrument still wins over a plugin matching "Example.*".
        const bool literal =
            !expressionText.contains(QRegularExpression{QStringLiteral(R"([.*+?\[\]{}()|^$\\])")});
        const bool whole = match.capturedStart() == 0 && match.capturedLength() == model.length();
        best = std::max(best, (literal && whole) ? 100 : 70);
    }
    return best;
}

} // namespace peakemi::python
