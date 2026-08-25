#pragma once

#include <peakemi/core/Capabilities.hpp>
#include <peakemi/core/RunConfiguration.hpp>

#include <QDockWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;

namespace peakemi::ui {

/// Editor for the run configuration (architecture.md 7): span, bandwidths,
/// detectors, thresholds, limit sets, corrections and dwell.
class RunConfigDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit RunConfigDock(QWidget* parent = nullptr);

    [[nodiscard]] RunConfiguration configuration() const;
    void setConfiguration(const RunConfiguration& config);

    /// Narrows the editable ranges to what the connected instrument supports,
    /// so an impossible configuration cannot be entered in the first place
    /// (FR-HAL-3).
    void applyCapabilities(const Capabilities& capabilities);

    void setEditingEnabled(bool enabled);

signals:
    void configurationChanged();

private slots:
    void importLimitLine();
    void importCorrectionTable();
    void removeSelectedCorrection();

private:
    void buildWidgets();
    void populateLimitCatalogue();
    [[nodiscard]] std::vector<LimitLine> selectedLimits() const;

    QDoubleSpinBox* m_startFrequency{nullptr};
    QDoubleSpinBox* m_stopFrequency{nullptr};
    QSpinBox* m_points{nullptr};
    QComboBox* m_phase1Detector{nullptr};
    QComboBox* m_resolutionBandwidth{nullptr};
    QDoubleSpinBox* m_refLevel{nullptr};
    QCheckBox* m_preamp{nullptr};

    QDoubleSpinBox* m_peakThreshold{nullptr};
    QDoubleSpinBox* m_prominence{nullptr};
    QDoubleSpinBox* m_minimumSpacing{nullptr};
    QSpinBox* m_maximumPeaks{nullptr};

    QComboBox* m_verificationDetector{nullptr};
    QSpinBox* m_dwellTime{nullptr};
    QDoubleSpinBox* m_verificationSpan{nullptr};
    QSpinBox* m_passes{nullptr};
    QDoubleSpinBox* m_marginalThreshold{nullptr};

    QListWidget* m_limits{nullptr};
    QListWidget* m_corrections{nullptr};
    std::vector<LimitLine> m_availableLimits;
    std::vector<CorrectionTable> m_correctionTables;
};

} // namespace peakemi::ui
