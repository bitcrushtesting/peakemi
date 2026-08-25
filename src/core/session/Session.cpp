#include <peakemi/core/Session.h>

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace peakemi {

std::optional<MeasurementPoint> Session::worstResult() const
{
    const MeasurementPoint* worst = nullptr;
    for (const auto& point : results) {
        if (!std::isfinite(point.marginDb)) {
            continue;
        }
        if (worst == nullptr || point.marginDb < worst->marginDb) {
            worst = &point;
        }
    }
    return worst != nullptr ? std::optional<MeasurementPoint>{*worst} : std::nullopt;
}

Verdict Session::overallVerdict() const
{
    Verdict overall = Verdict::Unknown;
    for (const auto& point : results) {
        switch (point.verdict) {
            case Verdict::Fail:
                return Verdict::Fail;
            case Verdict::Marginal:
                overall = Verdict::Marginal;
                break;
            case Verdict::Pass:
                if (overall == Verdict::Unknown) {
                    overall = Verdict::Pass;
                }
                break;
            case Verdict::Unknown:
                break;
        }
    }
    return overall;
}

Session Session::createNew(std::string applicationVersion)
{
    Session session;
    session.meta.applicationVersion = std::move(applicationVersion);
    session.meta.runId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    session.meta.createdAt = std::chrono::system_clock::now();
    session.meta.modifiedAt = session.meta.createdAt;
    return session;
}

} // namespace peakemi
