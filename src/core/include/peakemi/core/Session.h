#pragma once

#include <peakemi/core/MeasurementPoint.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/core/Trace.h>

#include <optional>
#include <string>
#include <vector>

namespace peakemi {

/// Everything about the device under test and the people who measured it.
struct SessionMeta
{
    std::string eutName;
    std::string eutSerial;
    std::string eutOperatingMode;
    std::string testSetup;
    std::string operatorName;
    std::string company;
    std::string notes;
    std::string applicationVersion;
    std::string runId; ///< opaque identifier repeated in every export (FR-DAT-6)
    TimePoint createdAt{};
    TimePoint modifiedAt{};

    friend bool operator==(const SessionMeta&, const SessionMeta&) = default;
};

/// A complete measurement session: configuration, traces and results (FR-DAT-1).
struct Session
{
    SessionMeta meta;
    RunConfiguration config;
    std::vector<Trace> traces;
    std::vector<MeasurementPoint> results;

    [[nodiscard]] bool isEmpty() const { return traces.empty() && results.empty(); }

    /// Worst (smallest) margin over all Phase 2 results.
    [[nodiscard]] std::optional<MeasurementPoint> worstResult() const;

    /// Overall verdict: Fail if any point fails, Marginal if any is marginal.
    [[nodiscard]] Verdict overallVerdict() const;

    /// Fresh session with a generated run id and the current timestamps.
    [[nodiscard]] static Session createNew(std::string applicationVersion);

    friend bool operator==(const Session&, const Session&) = default;
};

} // namespace peakemi
