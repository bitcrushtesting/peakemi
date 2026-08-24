#pragma once

#include <peakemi/core/CorrectionTable.hpp>
#include <peakemi/core/LimitLine.hpp>
#include <peakemi/core/PeakDetector.hpp>
#include <peakemi/core/SweepParams.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace peakemi {

/// Everything needed to reproduce a run (FR-RUN-6). Stored inside the session,
/// so re-running a saved configuration needs no manual reconfiguration.
struct RunConfiguration
{
    // --- Phase 1: rapid peak profile ---------------------------------------
    FrequencyRange span{megahertz(30), gigahertz(1.0)};
    int phase1Points{1001};
    Detector phase1Detector{Detector::Peak};
    Hertz phase1Rbw{0}; ///< 0 = mandated RBW of the band being swept
    Hertz phase1Vbw{0};
    Decibel refLevel{80.0};
    Decibel attenuation{10.0};
    bool automaticAttenuation{true};
    bool preamp{false};
    /// Maximum points the instrument delivers per sweep; wider spans are split
    /// into that many segments and stitched (architecture.md 5.2).
    int maximumPointsPerSweep{1001};

    // --- Peak selection -----------------------------------------------------
    PeakDetectionSettings peaks{};

    // --- Phase 2: dwell verification ---------------------------------------
    Detector verificationDetector{Detector::QuasiPeak};
    std::chrono::milliseconds dwellTime{1000};
    /// Span centred on the peak for the dwell; 0 selects zero span.
    Hertz verificationSpan{kilohertz(200)};
    int verificationPoints{101};
    /// Override the CISPR-mandated RBW; 0 keeps the mandated value.
    Hertz verificationRbw{0};

    // --- Run policy ---------------------------------------------------------
    int passes{1};      ///< max-hold passes over the whole loop (FR-RUN-8)
    int maxRetries{2};  ///< bounded retry on instrument errors (FR-RUN-7)
    std::chrono::milliseconds operationTimeout{15000};
    double marginalThresholdDb{6.0};
    bool autosave{true};
    std::string autosavePath;

    // --- Evaluation inputs --------------------------------------------------
    std::vector<LimitLine> limits;
    std::vector<CorrectionTable> corrections;

    /// Sweep parameters for the Phase 1 scan over @p segment.
    [[nodiscard]] SweepParams phase1SweepParams(FrequencyRange segment) const;

    /// Sweep parameters for the dwell on @p frequency, with the CISPR band's
    /// mandated RBW unless `verificationRbw` overrides it.
    [[nodiscard]] SweepParams phase2SweepParams(Hertz frequency) const;

    /// Split `span` into segments no wider than the instrument's point budget
    /// can resolve at the required RBW.
    [[nodiscard]] std::vector<FrequencyRange> planSegments() const;

    [[nodiscard]] Status validate() const;

    friend bool operator==(const RunConfiguration&, const RunConfiguration&) = default;
};

} // namespace peakemi
