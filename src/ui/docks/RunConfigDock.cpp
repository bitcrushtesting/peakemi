#include <peakemi/core/LimitCatalogue.hpp>
#include <peakemi/core/LimitLineIo.hpp>
#include <peakemi/ui/RunConfigDock.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace peakemi::ui {
namespace {

void addDetectors(QComboBox* combo)
{
    combo->addItem(QObject::tr("Peak"), static_cast<int>(Detector::Peak));
    combo->addItem(QObject::tr("Quasi-peak"), static_cast<int>(Detector::QuasiPeak));
    combo->addItem(QObject::tr("Average"), static_cast<int>(Detector::Average));
    combo->addItem(QObject::tr("RMS"), static_cast<int>(Detector::Rms));
    combo->addItem(QObject::tr("Sample"), static_cast<int>(Detector::Sample));
}

[[nodiscard]] Detector detectorOf(const QComboBox* combo)
{
    return static_cast<Detector>(combo->currentData().toInt());
}

void selectDetector(QComboBox* combo, Detector detector)
{
    const int index = combo->findData(static_cast<int>(detector));
    if (index >= 0) {
        combo->setCurrentIndex(index);
    }
}

[[nodiscard]] QString describeBandwidth(Hertz bandwidth)
{
    const double value = static_cast<double>(bandwidth.value());
    if (value >= 1e6) {
        return QObject::tr("%1 MHz").arg(value / 1e6, 0, 'g', 4);
    }
    if (value >= 1e3) {
        return QObject::tr("%1 kHz").arg(value / 1e3, 0, 'g', 4);
    }
    return QObject::tr("%1 Hz").arg(value, 0, 'g', 4);
}

} // namespace

RunConfigDock::RunConfigDock(QWidget* parent) : QDockWidget{tr("Run configuration"), parent}
{
    setObjectName(QStringLiteral("runConfigDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    buildWidgets();
    populateLimitCatalogue();
    setConfiguration(RunConfiguration{});
}

void RunConfigDock::buildWidgets()
{
    auto* content = new QWidget{this};
    auto* outer = new QVBoxLayout{content};

    // --- Phase 1 -----------------------------------------------------------
    auto* scanGroup = new QGroupBox{tr("Phase 1 — peak profile scan"), content};
    auto* scanForm = new QFormLayout{scanGroup};

    m_startFrequency = new QDoubleSpinBox{scanGroup};
    m_startFrequency->setRange(0.000009, 40000.0);
    m_startFrequency->setDecimals(6);
    m_startFrequency->setSuffix(tr(" MHz"));
    m_startFrequency->setToolTip(tr("Start of the scanned span"));
    scanForm->addRow(tr("Start frequency"), m_startFrequency);

    m_stopFrequency = new QDoubleSpinBox{scanGroup};
    m_stopFrequency->setRange(0.00001, 40000.0);
    m_stopFrequency->setDecimals(6);
    m_stopFrequency->setSuffix(tr(" MHz"));
    scanForm->addRow(tr("Stop frequency"), m_stopFrequency);

    m_points = new QSpinBox{scanGroup};
    m_points->setRange(101, 40001);
    m_points->setSingleStep(100);
    scanForm->addRow(tr("Trace points"), m_points);

    m_phase1Detector = new QComboBox{scanGroup};
    addDetectors(m_phase1Detector);
    scanForm->addRow(tr("Detector"), m_phase1Detector);

    m_resolutionBandwidth = new QComboBox{scanGroup};
    m_resolutionBandwidth->addItem(tr("CISPR band default"), QVariant::fromValue<qint64>(0));
    scanForm->addRow(tr("Resolution bandwidth"), m_resolutionBandwidth);

    m_refLevel = new QDoubleSpinBox{scanGroup};
    m_refLevel->setRange(-100.0, 130.0);
    m_refLevel->setSuffix(tr(" dB"));
    scanForm->addRow(tr("Reference level"), m_refLevel);

    m_preamp = new QCheckBox{tr("Enable pre-amplifier"), scanGroup};
    scanForm->addRow(QString{}, m_preamp);
    outer->addWidget(scanGroup);

    // --- Peak selection -----------------------------------------------------
    auto* peakGroup = new QGroupBox{tr("Peak selection"), content};
    auto* peakForm = new QFormLayout{peakGroup};

    m_peakThreshold = new QDoubleSpinBox{peakGroup};
    m_peakThreshold->setRange(0.0, 60.0);
    m_peakThreshold->setSuffix(tr(" dB"));
    m_peakThreshold->setToolTip(tr("Flag peaks whose margin to the limit is smaller than this"));
    peakForm->addRow(tr("Flag within"), m_peakThreshold);

    m_prominence = new QDoubleSpinBox{peakGroup};
    m_prominence->setRange(0.0, 40.0);
    m_prominence->setSuffix(tr(" dB"));
    peakForm->addRow(tr("Minimum prominence"), m_prominence);

    m_minimumSpacing = new QDoubleSpinBox{peakGroup};
    m_minimumSpacing->setRange(0.0, 1000.0);
    m_minimumSpacing->setDecimals(3);
    m_minimumSpacing->setSuffix(tr(" MHz"));
    peakForm->addRow(tr("Minimum spacing"), m_minimumSpacing);

    m_maximumPeaks = new QSpinBox{peakGroup};
    m_maximumPeaks->setRange(1, 500);
    peakForm->addRow(tr("Maximum peaks"), m_maximumPeaks);
    outer->addWidget(peakGroup);

    // --- Phase 2 -----------------------------------------------------------
    auto* dwellGroup = new QGroupBox{tr("Phase 2 — dwell verification"), content};
    auto* dwellForm = new QFormLayout{dwellGroup};

    m_verificationDetector = new QComboBox{dwellGroup};
    addDetectors(m_verificationDetector);
    dwellForm->addRow(tr("Detector"), m_verificationDetector);

    m_dwellTime = new QSpinBox{dwellGroup};
    m_dwellTime->setRange(10, 60000);
    m_dwellTime->setSingleStep(100);
    m_dwellTime->setSuffix(tr(" ms"));
    dwellForm->addRow(tr("Dwell time"), m_dwellTime);

    m_verificationSpan = new QDoubleSpinBox{dwellGroup};
    m_verificationSpan->setRange(0.0, 10000.0);
    m_verificationSpan->setDecimals(1);
    m_verificationSpan->setSuffix(tr(" kHz"));
    m_verificationSpan->setToolTip(tr("Span centred on the peak; 0 selects zero span"));
    dwellForm->addRow(tr("Verification span"), m_verificationSpan);

    m_passes = new QSpinBox{dwellGroup};
    m_passes->setRange(1, 20);
    m_passes->setToolTip(tr("Repeat the whole loop and keep the worst case per point"));
    dwellForm->addRow(tr("Passes"), m_passes);

    m_marginalThreshold = new QDoubleSpinBox{dwellGroup};
    m_marginalThreshold->setRange(0.0, 30.0);
    m_marginalThreshold->setSuffix(tr(" dB"));
    dwellForm->addRow(tr("Marginal below"), m_marginalThreshold);
    outer->addWidget(dwellGroup);

    // --- Limits and corrections --------------------------------------------
    auto* limitGroup = new QGroupBox{tr("Limit lines"), content};
    auto* limitLayout = new QVBoxLayout{limitGroup};
    m_limits = new QListWidget{limitGroup};
    m_limits->setSelectionMode(QAbstractItemView::NoSelection);
    m_limits->setToolTip(tr("Several limits may be active at once; the worst margin wins"));
    limitLayout->addWidget(m_limits);
    auto* importLimit = new QPushButton{tr("Import limit line…"), limitGroup};
    connect(importLimit, &QPushButton::clicked, this, &RunConfigDock::importLimitLine);
    limitLayout->addWidget(importLimit);
    outer->addWidget(limitGroup);

    auto* correctionGroup = new QGroupBox{tr("Correction tables"), content};
    auto* correctionLayout = new QVBoxLayout{correctionGroup};
    m_corrections = new QListWidget{correctionGroup};
    m_corrections->setToolTip(
        tr("Antenna factors, cable losses and amplifier gains applied before limit evaluation"));
    correctionLayout->addWidget(m_corrections);
    auto* correctionButtons = new QHBoxLayout;
    auto* importCorrection = new QPushButton{tr("Import…"), correctionGroup};
    connect(importCorrection, &QPushButton::clicked, this, &RunConfigDock::importCorrectionTable);
    auto* removeCorrection = new QPushButton{tr("Remove"), correctionGroup};
    connect(removeCorrection, &QPushButton::clicked, this, &RunConfigDock::removeSelectedCorrection);
    correctionButtons->addWidget(importCorrection);
    correctionButtons->addWidget(removeCorrection);
    correctionLayout->addLayout(correctionButtons);
    outer->addWidget(correctionGroup);

    outer->addStretch();

    auto* scroll = new QScrollArea{this};
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    setWidget(scroll);

    const auto notify = [this] { emit configurationChanged(); };
    connect(m_startFrequency, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_stopFrequency, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_points, &QSpinBox::valueChanged, this, notify);
    connect(m_phase1Detector, &QComboBox::currentIndexChanged, this, notify);
    connect(m_resolutionBandwidth, &QComboBox::currentIndexChanged, this, notify);
    connect(m_refLevel, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_preamp, &QCheckBox::toggled, this, notify);
    connect(m_peakThreshold, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_prominence, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_minimumSpacing, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_maximumPeaks, &QSpinBox::valueChanged, this, notify);
    connect(m_verificationDetector, &QComboBox::currentIndexChanged, this, notify);
    connect(m_dwellTime, &QSpinBox::valueChanged, this, notify);
    connect(m_verificationSpan, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_passes, &QSpinBox::valueChanged, this, notify);
    connect(m_marginalThreshold, &QDoubleSpinBox::valueChanged, this, notify);
    connect(m_limits, &QListWidget::itemChanged, this, notify);
    connect(m_corrections, &QListWidget::itemChanged, this, notify);
}

void RunConfigDock::populateLimitCatalogue()
{
    m_availableLimits.assign(builtInLimitLines().begin(), builtInLimitLines().end());
    m_limits->clear();
    for (const auto& limit : m_availableLimits) {
        auto* item = new QListWidgetItem{QString::fromStdString(limit.name), m_limits};
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setToolTip(QString::fromStdString(limit.standard + "\n" + limit.note));
    }
}

std::vector<LimitLine> RunConfigDock::selectedLimits() const
{
    std::vector<LimitLine> limits;
    for (int row = 0; row < m_limits->count(); ++row) {
        if (m_limits->item(row)->checkState() == Qt::Checked
            && row < static_cast<int>(m_availableLimits.size())) {
            limits.push_back(m_availableLimits[static_cast<std::size_t>(row)]);
        }
    }
    return limits;
}

RunConfiguration RunConfigDock::configuration() const
{
    RunConfiguration config;
    config.span = FrequencyRange{megahertz(m_startFrequency->value()),
                                 megahertz(m_stopFrequency->value())};
    config.phase1Points = m_points->value();
    config.phase1Detector = detectorOf(m_phase1Detector);
    config.phase1Rbw = hertz(m_resolutionBandwidth->currentData().toLongLong());
    config.refLevel = decibel(m_refLevel->value());
    config.preamp = m_preamp->isChecked();
    config.maximumPointsPerSweep = m_points->value();

    config.peaks.marginThresholdDb = m_peakThreshold->value();
    config.peaks.prominenceDb = m_prominence->value();
    config.peaks.minimumSpacing = megahertz(m_minimumSpacing->value());
    config.peaks.maximumCount = m_maximumPeaks->value();

    config.verificationDetector = detectorOf(m_verificationDetector);
    config.dwellTime = std::chrono::milliseconds{m_dwellTime->value()};
    config.verificationSpan = kilohertz(m_verificationSpan->value());
    config.passes = m_passes->value();
    config.marginalThresholdDb = m_marginalThreshold->value();

    config.limits = selectedLimits();
    config.corrections = m_correctionTables;
    for (int row = 0; row < m_corrections->count()
                      && row < static_cast<int>(config.corrections.size());
         ++row) {
        config.corrections[static_cast<std::size_t>(row)].enabled =
            m_corrections->item(row)->checkState() == Qt::Checked;
    }
    return config;
}

void RunConfigDock::setConfiguration(const RunConfiguration& config)
{
    const QSignalBlocker blocker{this};
    m_startFrequency->setValue(toMegahertz(config.span.start));
    m_stopFrequency->setValue(toMegahertz(config.span.stop));
    m_points->setValue(config.phase1Points);
    selectDetector(m_phase1Detector, config.phase1Detector);
    const int rbwIndex =
        m_resolutionBandwidth->findData(QVariant::fromValue<qint64>(config.phase1Rbw.value()));
    m_resolutionBandwidth->setCurrentIndex(rbwIndex >= 0 ? rbwIndex : 0);
    m_refLevel->setValue(config.refLevel.value());
    m_preamp->setChecked(config.preamp);

    m_peakThreshold->setValue(config.peaks.marginThresholdDb);
    m_prominence->setValue(config.peaks.prominenceDb);
    m_minimumSpacing->setValue(toMegahertz(config.peaks.minimumSpacing));
    m_maximumPeaks->setValue(config.peaks.maximumCount);

    selectDetector(m_verificationDetector, config.verificationDetector);
    m_dwellTime->setValue(static_cast<int>(config.dwellTime.count()));
    m_verificationSpan->setValue(static_cast<double>(config.verificationSpan.value()) / 1000.0);
    m_passes->setValue(config.passes);
    m_marginalThreshold->setValue(config.marginalThresholdDb);

    if (!config.limits.empty()) {
        for (const auto& limit : config.limits) {
            const auto existing = std::find_if(m_availableLimits.begin(), m_availableLimits.end(),
                                               [&](const LimitLine& stored) {
                                                   return stored.name == limit.name;
                                               });
            if (existing == m_availableLimits.end()) {
                m_availableLimits.push_back(limit);
                auto* item = new QListWidgetItem{QString::fromStdString(limit.name), m_limits};
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
            } else {
                const auto row = std::distance(m_availableLimits.begin(), existing);
                m_limits->item(static_cast<int>(row))->setCheckState(Qt::Checked);
            }
        }
    }

    m_correctionTables = config.corrections;
    m_corrections->clear();
    for (const auto& correction : m_correctionTables) {
        auto* item = new QListWidgetItem{QString::fromStdString(correction.name), m_corrections};
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(correction.enabled ? Qt::Checked : Qt::Unchecked);
    }
}

void RunConfigDock::applyCapabilities(const Capabilities& capabilities)
{
    const QSignalBlocker blocker{this};
    m_startFrequency->setRange(toMegahertz(capabilities.range.start),
                               toMegahertz(capabilities.range.stop));
    m_stopFrequency->setRange(toMegahertz(capabilities.range.start),
                              toMegahertz(capabilities.range.stop));
    m_points->setRange(capabilities.minimumPoints, capabilities.maximumPoints);
    m_refLevel->setRange(capabilities.minimumRefLevel.value(),
                         capabilities.maximumRefLevel.value());
    m_preamp->setEnabled(capabilities.preamp);

    const qint64 currentRbw = m_resolutionBandwidth->currentData().toLongLong();
    m_resolutionBandwidth->clear();
    m_resolutionBandwidth->addItem(tr("CISPR band default"), QVariant::fromValue<qint64>(0));
    for (const auto& bandwidth : capabilities.resolutionBandwidths) {
        m_resolutionBandwidth->addItem(describeBandwidth(bandwidth),
                                       QVariant::fromValue<qint64>(bandwidth.value()));
    }
    const int index = m_resolutionBandwidth->findData(QVariant::fromValue(currentRbw));
    m_resolutionBandwidth->setCurrentIndex(index >= 0 ? index : 0);

    for (int i = 0; i < m_phase1Detector->count(); ++i) {
        const auto detector = static_cast<Detector>(m_phase1Detector->itemData(i).toInt());
        const bool supported = capabilities.supports(detector);
        auto* model = qobject_cast<QStandardItemModel*>(m_phase1Detector->model());
        if (model != nullptr) {
            model->item(i)->setEnabled(supported);
        }
    }
}

void RunConfigDock::setEditingEnabled(bool enabled)
{
    if (auto* content = widget()) {
        content->setEnabled(enabled);
    }
}

void RunConfigDock::importLimitLine()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("Import limit line"),
        {},
        tr("Limit files (*.csv *.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    auto limit = limit_io::load(path);
    if (!limit) {
        QMessageBox::warning(this,
                             tr("Import failed"),
                             tr("The limit line could not be imported:\n%1")
                                 .arg(QString::fromStdString(limit.error().message())));
        return;
    }
    if (limit->name.empty()) {
        limit->name = QFileInfo{path}.completeBaseName().toStdString();
    }

    m_availableLimits.push_back(*limit);
    auto* item = new QListWidgetItem{QString::fromStdString(limit->name), m_limits};
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    emit configurationChanged();
}

void RunConfigDock::importCorrectionTable()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("Import correction table"),
        {},
        tr("Correction files (*.csv *.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    auto table = correction_io::load(path);
    if (!table) {
        QMessageBox::warning(this,
                             tr("Import failed"),
                             tr("The correction table could not be imported:\n%1")
                                 .arg(QString::fromStdString(table.error().message())));
        return;
    }
    if (table->name.empty()) {
        table->name = QFileInfo{path}.completeBaseName().toStdString();
    }

    m_correctionTables.push_back(*table);
    auto* item = new QListWidgetItem{QString::fromStdString(table->name), m_corrections};
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    emit configurationChanged();
}

void RunConfigDock::removeSelectedCorrection()
{
    const int row = m_corrections->currentRow();
    if (row < 0 || row >= static_cast<int>(m_correctionTables.size())) {
        return;
    }
    m_correctionTables.erase(m_correctionTables.begin() + row);
    delete m_corrections->takeItem(row);
    emit configurationChanged();
}

} // namespace peakemi::ui
