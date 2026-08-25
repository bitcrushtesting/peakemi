#include <peakemi/core/Logging.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>

#include <algorithm>

namespace peakemi {

Q_LOGGING_CATEGORY(lcCore, "peakemi.core")
Q_LOGGING_CATEGORY(lcEngine, "peakemi.engine")
Q_LOGGING_CATEGORY(lcSession, "peakemi.session")
Q_LOGGING_CATEGORY(lcHal, "peakemi.hal")
Q_LOGGING_CATEGORY(lcTransport, "peakemi.hal.transport")
Q_LOGGING_CATEGORY(lcScpi, "peakemi.hal.scpi")
Q_LOGGING_CATEGORY(lcDiscovery, "peakemi.hal.discovery")
Q_LOGGING_CATEGORY(lcDriver, "peakemi.driver")
Q_LOGGING_CATEGORY(lcUi, "peakemi.ui")
Q_LOGGING_CATEGORY(lcReport, "peakemi.reporting")

namespace {

QtMessageHandler g_previousHandler = nullptr;
QMutex g_logMutex;
QString g_logPath;
qint64 g_maximumBytes = 5LL * 1024 * 1024;
int g_keptFiles = 3;

[[nodiscard]] const char* levelName(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg:
            return "debug";
        case QtInfoMsg:
            return "info";
        case QtWarningMsg:
            return "warning";
        case QtCriticalMsg:
            return "critical";
        case QtFatalMsg:
            return "fatal";
    }
    return "info";
}

void rotateIfNeeded()
{
    QFile current{g_logPath};
    if (!current.exists() || current.size() < g_maximumBytes) {
        return;
    }
    QFile::remove(g_logPath + QStringLiteral(".%1").arg(g_keptFiles));
    for (int index = g_keptFiles - 1; index >= 1; --index) {
        QFile::rename(g_logPath + QStringLiteral(".%1").arg(index),
                      g_logPath + QStringLiteral(".%1").arg(index + 1));
    }
    QFile::rename(g_logPath, g_logPath + QStringLiteral(".1"));
}

void fileMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, message);
    }

    const QMutexLocker locker{&g_logMutex};
    if (g_logPath.isEmpty()) {
        return;
    }
    rotateIfNeeded();

    QFile file{g_logPath};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream{&file};
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ' ' << levelName(type)
           << ' '
           << (context.category != nullptr ? QString::fromUtf8(context.category)
                                           : QStringLiteral("default"))
           << " - " << message << '\n';
}

} // namespace

QString installRotatingFileLogger(const QString& directory, qint64 maximumBytes, int keptFiles)
{
    if (!QDir{}.mkpath(directory)) {
        return {};
    }

    const QMutexLocker locker{&g_logMutex};
    g_logPath = directory + QStringLiteral("/peakemi.log");
    g_maximumBytes = maximumBytes;
    g_keptFiles = std::max(1, keptFiles);
    if (g_previousHandler == nullptr) {
        g_previousHandler = qInstallMessageHandler(&fileMessageHandler);
    }
    return g_logPath;
}

QString currentLogFile()
{
    const QMutexLocker locker{&g_logMutex};
    return g_logPath;
}

} // namespace peakemi
