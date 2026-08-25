#pragma once

#include <QLoggingCategory>

namespace peakemi {

/// Categorised logging (FR-APP-1). The SCPI transcript is deliberately its own
/// category so it can be enabled for bug reports without drowning the log.
Q_DECLARE_LOGGING_CATEGORY(lcCore)
Q_DECLARE_LOGGING_CATEGORY(lcEngine)
Q_DECLARE_LOGGING_CATEGORY(lcSession)
Q_DECLARE_LOGGING_CATEGORY(lcHal)
Q_DECLARE_LOGGING_CATEGORY(lcTransport)
Q_DECLARE_LOGGING_CATEGORY(lcScpi)
Q_DECLARE_LOGGING_CATEGORY(lcDiscovery)
Q_DECLARE_LOGGING_CATEGORY(lcDriver)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
Q_DECLARE_LOGGING_CATEGORY(lcReport)

/// Install a rotating file sink in addition to the default handler (FR-APP-1).
///
/// Writes `<directory>/peakemi.log`, rotating to `peakemi.log.1`, `.2`, … when
/// the file exceeds @p maximumBytes. Returns the active log file path, or an
/// empty string when the directory could not be created.
QString installRotatingFileLogger(const QString& directory,
                                  qint64 maximumBytes = 5LL * 1024 * 1024,
                                  int keptFiles = 3);

/// Path of the log file currently written, empty when no sink is installed.
[[nodiscard]] QString currentLogFile();

} // namespace peakemi
