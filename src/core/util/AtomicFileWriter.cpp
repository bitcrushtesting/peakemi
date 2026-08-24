#include <peakemi/core/AtomicFileWriter.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace peakemi {

Status writeFileAtomically(const QString& path, std::string_view content)
{
    const QFileInfo info{path};
    if (!info.absoluteDir().exists() && !info.absoluteDir().mkpath(QStringLiteral("."))) {
        return fail(ErrorCode::IoFailure,
                    "cannot create directory " + info.absolutePath().toStdString());
    }

    // QSaveFile is exactly the write-temp-then-rename dance, including fsync.
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(ErrorCode::IoFailure,
                    path.toStdString() + ": " + file.errorString().toStdString());
    }
    const auto written =
        file.write(content.data(), static_cast<qint64>(content.size()));
    if (written != static_cast<qint64>(content.size())) {
        file.cancelWriting();
        return fail(ErrorCode::IoFailure, path.toStdString() + ": short write");
    }
    if (!file.commit()) {
        return fail(ErrorCode::IoFailure,
                    path.toStdString() + ": " + file.errorString().toStdString());
    }
    return {};
}

Result<std::string> readFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::IoFailure,
                    path.toStdString() + ": " + file.errorString().toStdString());
    }
    const QByteArray data = file.readAll();
    return std::string{data.constData(), static_cast<std::size_t>(data.size())};
}

} // namespace peakemi
