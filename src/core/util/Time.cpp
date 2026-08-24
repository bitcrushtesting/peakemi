#include <peakemi/core/Time.hpp>

#include <QDateTime>
#include <QTimeZone>
#include <QString>

namespace peakemi {

std::string toIso8601(TimePoint timePoint)
{
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            timePoint.time_since_epoch())
                            .count();
    const auto dateTime = QDateTime::fromMSecsSinceEpoch(millis, QTimeZone::UTC);
    return dateTime.toString(Qt::ISODateWithMs).toStdString();
}

std::optional<TimePoint> fromIso8601(std::string_view text)
{
    const auto dateTime =
        QDateTime::fromString(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())),
                              Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        return std::nullopt;
    }
    const auto millis = dateTime.toUTC().toMSecsSinceEpoch();
    return TimePoint{std::chrono::milliseconds{millis}};
}

} // namespace peakemi
