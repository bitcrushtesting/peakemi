#pragma once

#include <peakemi/core/CorrectionTable.hpp>
#include <peakemi/core/InstrumentId.hpp>
#include <peakemi/core/Time.hpp>
#include <peakemi/core/Units.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace peakemi {

/// One Phase 2 verified result, stored with full provenance so the number can
/// be defended months later (FR-RUN-4).
struct MeasurementPoint
{
    Hertz frequency{};
    double rawAmplitude{};       ///< as read from the instrument, in `rawUnit`
    double correctedAmplitude{}; ///< after corrections, in `unit`
    double limitValue{};         ///< NaN when no limit covers the frequency
    double marginDb{};
    AmplitudeUnit rawUnit{AmplitudeUnit::dBuV};
    AmplitudeUnit unit{AmplitudeUnit::dBuV};
    Detector detector{Detector::QuasiPeak};
    Hertz rbw{};
    Hertz vbw{};
    std::chrono::milliseconds dwell{0};
    std::vector<AppliedCorrection> corrections;
    std::string limitName;
    Verdict verdict{Verdict::Unknown};
    InstrumentId instrument;
    TimePoint measuredAt{};
    int pass{1}; ///< index of the max-hold pass this value came from (FR-RUN-8)

    friend bool operator==(const MeasurementPoint&, const MeasurementPoint&) = default;
};

} // namespace peakemi
