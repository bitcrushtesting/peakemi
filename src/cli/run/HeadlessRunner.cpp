#include <peakemi/cli/HeadlessRunner.h>
#include <peakemi/core/Capabilities.h>
#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/LimitLineIo.h>
#include <peakemi/core/Logging.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/core/Version.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/hal/Discovery.h>
#include <peakemi/hal/DriverRegistry.h>
#include <peakemi/hal/TransportFactory.h>
#include <peakemi/reporting/CsvExporter.h>
#include <peakemi/reporting/JsonExporter.h>
#include <peakemi/reporting/PdfReportRenderer.h>
#include <peakemi/reporting/ReportTemplateIo.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <utility>

namespace peakemi::cli {
namespace {

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] QTextStream& err()
{
    static QTextStream stream{stderr};
    return stream;
}

[[nodiscard]] QTextStream& out()
{
    static QTextStream stream{stdout};
    return stream;
}

[[nodiscard]] QString megahertzText(Hertz frequency)
{
    return QStringLiteral("%1 MHz").arg(toMegahertz(frequency), 0, 'f', 3);
}

[[nodiscard]] QString phaseName(MeasurementEngine::Phase phase)
{
    switch (phase) {
        case MeasurementEngine::Phase::Idle:
            return QStringLiteral("idle");
        case MeasurementEngine::Phase::Configured:
            return QStringLiteral("configured");
        case MeasurementEngine::Phase::Phase1Sweep:
            return QStringLiteral("phase 1 sweep");
        case MeasurementEngine::Phase::PeakAnalysis:
            return QStringLiteral("peak analysis");
        case MeasurementEngine::Phase::Phase2Dwell:
            return QStringLiteral("phase 2 dwell");
        case MeasurementEngine::Phase::Paused:
            return QStringLiteral("paused");
        case MeasurementEngine::Phase::Finished:
            return QStringLiteral("finished");
        case MeasurementEngine::Phase::Failed:
            return QStringLiteral("failed");
        case MeasurementEngine::Phase::Aborted:
            return QStringLiteral("aborted");
    }
    return QStringLiteral("unknown");
}

/// A verdict this run is not allowed to reach.
[[nodiscard]] bool exceeds(Verdict verdict, FailOn failOn)
{
    switch (failOn) {
        case FailOn::Never:
            return false;
        case FailOn::Fail:
            return verdict == Verdict::Fail;
        case FailOn::Marginal:
            return verdict == Verdict::Fail || verdict == Verdict::Marginal;
    }
    return false;
}

[[nodiscard]] QJsonObject toJson(const MeasurementPoint& point)
{
    return QJsonObject{
        {QStringLiteral("frequencyHz"), static_cast<double>(point.frequency.value())},
        {QStringLiteral("amplitude"), point.correctedAmplitude},
        {QStringLiteral("unit"), qs(amplitudeUnitKey(point.unit))},
        {QStringLiteral("limit"),
         std::isfinite(point.limitValue) ? QJsonValue{point.limitValue} : QJsonValue{}},
        {QStringLiteral("marginDb"),
         std::isfinite(point.marginDb) ? QJsonValue{point.marginDb} : QJsonValue{}},
        {QStringLiteral("limitName"), qs(point.limitName)},
        {QStringLiteral("verdict"), qs(verdictKey(point.verdict))}};
}

} // namespace

HeadlessRunner::HeadlessRunner(HeadlessOptions options) : m_options{std::move(options)} {}

HeadlessRunner::~HeadlessRunner() = default;

void HeadlessRunner::setPlotRenderer(PlotRenderer renderer)
{
    m_plotRenderer = std::move(renderer);
}

void HeadlessRunner::requestAbort()
{
    m_abortRequested.store(true);
    const std::scoped_lock lock{m_engineMutex};
    if (m_engine != nullptr) {
        m_engine->requestAbort();
    }
}

void HeadlessRunner::report(const QString& message) const
{
    if (m_options.quiet) {
        return;
    }
    err() << message << '\n';
    err().flush();
}

void HeadlessRunner::loadPlugins()
{
    if (!python::PluginRegistry::isSupported()) {
        return;
    }
    m_plugins.rescan();
    m_plugins.publishToDriverRegistry();
    for (const auto& plugin : m_plugins.plugins()) {
        if (plugin.state == python::PluginState::AwaitingApproval) {
            report(QStringLiteral("plugin %1 is not approved on this machine and was not loaded")
                       .arg(plugin.path));
        }
    }
}

Result<RunConfiguration> HeadlessRunner::buildConfiguration()
{
    RunConfiguration config;

    if (!m_options.sessionPath.isEmpty()) {
        auto loaded = SessionSerializer::load(m_options.sessionPath);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        // The file is the baseline: its configuration *and* the equipment it
        // describes, so a pipeline that re-runs a saved session does not have
        // to restate what is already recorded in it.
        config = loaded->config;
        m_meta = loaded->meta;
        report(QStringLiteral("configuration from %1").arg(m_options.sessionPath));
    }

    // A limit given on the command line replaces the file's set rather than
    // adding to it: a run evaluated against a limit nobody asked for is worse
    // than one that refuses to start.
    if (!m_options.limits.isEmpty()) {
        config.limits.clear();
        for (const auto& specification : m_options.limits) {
            if (auto builtIn = builtInLimitLine(specification.toStdString())) {
                config.limits.push_back(*builtIn);
                continue;
            }
            if (!QFileInfo::exists(specification)) {
                return fail(ErrorCode::InvalidConfiguration,
                            QStringLiteral("'%1' is neither a built-in limit line nor a file; "
                                           "--list-limits shows the catalogue")
                                .arg(specification)
                                .toStdString());
            }
            auto line = limit_io::load(specification);
            if (!line) {
                return std::unexpected(line.error());
            }
            config.limits.push_back(*line);
        }
    }
    if (!m_options.corrections.isEmpty()) {
        config.corrections.clear();
        for (const auto& path : m_options.corrections) {
            auto table = correction_io::load(path);
            if (!table) {
                return std::unexpected(table.error());
            }
            config.corrections.push_back(*table);
        }
    }

    if (m_options.start) {
        config.span.start = *m_options.start;
    }
    if (m_options.stop) {
        config.span.stop = *m_options.stop;
    }
    if (m_options.points) {
        config.phase1Points = *m_options.points;
    }
    if (m_options.refLevel) {
        config.refLevel = *m_options.refLevel;
    }
    if (m_options.verificationDetector) {
        config.verificationDetector = *m_options.verificationDetector;
    }
    if (m_options.dwell) {
        config.dwellTime = *m_options.dwell;
    }
    if (m_options.passes) {
        config.passes = *m_options.passes;
    }
    if (m_options.maximumPeaks) {
        config.peaks.maximumCount = *m_options.maximumPeaks;
    }
    if (m_options.marginThresholdDb) {
        config.peaks.marginThresholdDb = *m_options.marginThresholdDb;
    }
    if (!m_options.startCommands.empty()) {
        config.startCommands = m_options.startCommands;
    }
    if (!m_options.stopCommands.empty()) {
        config.stopCommands = m_options.stopCommands;
    }

    if (config.limits.empty()) {
        return fail(ErrorCode::InvalidConfiguration,
                    "the run has no limit line, so nothing could be evaluated");
    }
    if (auto status = config.validate(); !status) {
        return std::unexpected(status.error());
    }
    return config;
}

Result<DriverPtr> HeadlessRunner::connectInstrument()
{
    const auto& endpoint = m_options.endpoint;

    if (endpoint.kind == TransportKind::Simulated) {
        auto simulated = std::make_shared<drivers::SimulatedDriver>();
        if (auto status = simulated->open(nullptr); !status) {
            return std::unexpected(status.error());
        }
        if (auto identity = simulated->identify()) {
            m_instrument = *identity;
        }
        return simulated;
    }

    auto transport = hal::makeTransport(endpoint);
    if (!transport) {
        return std::unexpected(transport.error());
    }
    m_transport = *transport;

    auto& registry = hal::DriverRegistry::instance();
    Result<DriverPtr> driver = fail(ErrorCode::NoDriverMatch, "no driver was chosen");
    if (m_options.driverId.empty()) {
        // Ask the endpoint who it is, then let the registry score the answer.
        // The probe uses its own short-lived connection, exactly as the
        // instrument dock does, so the driver still opens a clean one.
        auto identity = hal::identifyEndpoint(endpoint, endpoint.defaultTimeout);
        if (!identity) {
            return fail(identity.error().code,
                        endpoint.displayName() +
                            " did not answer *IDN?: " + identity.error().detail);
        }
        m_instrument = *identity;
        driver = registry.createBestMatch(*identity);
        if (!driver) {
            return fail(driver.error().code,
                        driver.error().detail + "; name one with --driver, and --list-drivers "
                                                "shows what this build has");
        }
    } else {
        driver = registry.create(m_options.driverId);
        if (!driver) {
            return std::unexpected(driver.error());
        }
    }

    if (auto status = (*driver)->open(m_transport); !status) {
        return std::unexpected(status.error());
    }
    if (auto identity = (*driver)->identify()) {
        m_instrument = *identity;
    }
    return *driver;
}

int HeadlessRunner::run()
{
    // Queued connections are not used here -- the loop runs on this thread --
    // but the session type still has to be known to the meta-object system for
    // the signals to carry it at all.
    registerMetaTypes();
    drivers::registerBuiltInDrivers();
    loadPlugins();

    QElapsedTimer clock;
    clock.start();

    if (!m_options.logDirectory.isEmpty()) {
        installRotatingFileLogger(m_options.logDirectory);
    }
    // --quiet silences the engine's own commentary too, not just this runner's:
    // a pipeline that asked for quiet wants the summary and the exit code, and
    // nothing else on the way there.
    if (m_options.verbose) {
        QLoggingCategory::setFilterRules(QStringLiteral("peakemi.*.debug=true"));
    } else if (m_options.quiet) {
        QLoggingCategory::setFilterRules(
            QStringLiteral("peakemi.*.debug=false\npeakemi.*.info=false"));
    } else {
        QLoggingCategory::setFilterRules(QStringLiteral("peakemi.*.debug=false"));
    }

    auto config = buildConfiguration();
    if (!config) {
        err() << QStringLiteral("peakemi: %1\n").arg(qs(config.error().message()));
        err().flush();
        return exit_code::UsageError;
    }

    auto driver = connectInstrument();
    if (!driver) {
        err() << QStringLiteral("peakemi: %1\n").arg(qs(driver.error().message()));
        err().flush();
        return exit_code::RunFailed;
    }
    report(QStringLiteral("instrument: %1").arg(qs(m_instrument.displayName())));

    // Refuse a sweep the instrument cannot make, here rather than three minutes
    // into the run: the point of asking for capabilities is to fail early.
    const auto capabilities = (*driver)->capabilities();
    if (!capabilities.range.contains(config->span.start) ||
        !capabilities.range.contains(config->span.stop))
    {
        err() << QStringLiteral("peakemi: the instrument sweeps %1 to %2, the run asks for %3 "
                                "to %4\n")
                     .arg(megahertzText(capabilities.range.start),
                          megahertzText(capabilities.range.stop),
                          megahertzText(config->span.start),
                          megahertzText(config->span.stop));
        err().flush();
        return exit_code::UsageError;
    }
    config->maximumPointsPerSweep =
        std::min(config->maximumPointsPerSweep, capabilities.maximumPoints);
    config->verificationPoints = std::clamp(
        config->verificationPoints, capabilities.minimumPoints, capabilities.maximumPoints);

    m_meta.applicationVersion = std::string{ProjectVersion};
    if (!m_options.runId.empty()) {
        m_meta.runId = m_options.runId;
    }
    if (!m_options.eutName.empty()) {
        m_meta.eutName = m_options.eutName;
    }
    if (!m_options.eutSerial.empty()) {
        m_meta.eutSerial = m_options.eutSerial;
    }
    if (!m_options.operatorName.empty()) {
        m_meta.operatorName = m_options.operatorName;
    }
    if (!m_options.company.empty()) {
        m_meta.company = m_options.company;
    }
    if (!m_options.notes.empty()) {
        m_meta.notes = m_options.notes;
    }

    // Autosave onto the session output: a job killed mid-run still leaves the
    // points it had already verified where the pipeline expects to find them.
    if (!m_options.sessionOutput.isEmpty() || !m_options.outputDirectory.isEmpty()) {
        config->autosave = true;
        config->autosavePath = m_options.sessionOutput.isEmpty()
                                   ? QDir{m_options.outputDirectory}
                                         .filePath(QStringLiteral("session.peakemi.json"))
                                         .toStdString()
                                   : m_options.sessionOutput.toStdString();
        QDir{}.mkpath(QFileInfo{qs(config->autosavePath)}.absolutePath());
    }

    MeasurementEngine engine;
    engine.setDriver(*driver);
    engine.setConfiguration(*config);
    engine.setSessionMeta(m_meta);
    if (m_transport) {
        // Auxiliary equipment shares the instrument's bus; the engine decides
        // when the operator's commands go out, the transport carries them.
        auto transport = m_transport;
        const auto timeout = config->commandTimeout;
        engine.setCommandSender([transport, timeout](const std::string& command) -> Status {
            if (auto status = transport->write(command); !status) {
                return status;
            }
            if (command.find('?') == std::string::npos) {
                return {};
            }
            const CancelToken cancel;
            auto response = transport->read(timeout, cancel);
            if (!response) {
                return std::unexpected(response.error());
            }
            return {};
        });
    }

    QObject::connect(
        &engine, &MeasurementEngine::phaseChanged, &engine, [this](MeasurementEngine::Phase phase) {
            report(QStringLiteral("* %1").arg(phaseName(phase)));
        });
    QObject::connect(
        &engine,
        &MeasurementEngine::peaksFlagged,
        &engine,
        [this](const std::vector<PeakCandidate>& peaks) {
            report(QStringLiteral("  %1 peak(s) flagged for verification").arg(peaks.size()));
        });
    QObject::connect(
        &engine, &MeasurementEngine::pointMeasured, &engine, [this](const MeasurementPoint& point) {
            report(QStringLiteral("  %1  %2 %3  limit %4  margin %5 dB  %6")
                       .arg(megahertzText(point.frequency),
                            QString::number(point.correctedAmplitude, 'f', 1),
                            qs(amplitudeUnitKey(point.unit)),
                            std::isfinite(point.limitValue)
                                ? QString::number(point.limitValue, 'f', 1)
                                : QStringLiteral("-"),
                            std::isfinite(point.marginDb) ? QString::number(point.marginDb, 'f', 1)
                                                          : QStringLiteral("-"),
                            qs(verdictKey(point.verdict)).toUpper()));
        });
    QObject::connect(&engine, &MeasurementEngine::runFailed, &engine, [this](const Error& error) {
        m_error = error;
    });
    QObject::connect(&engine,
                     &MeasurementEngine::runFinished,
                     &engine,
                     [this](const Session& session) { m_session = session; });

    {
        const std::scoped_lock lock{m_engineMutex};
        m_engine = &engine;
    }

    // The engine clears its own abort flag as a run begins -- an abort belongs
    // to the run it interrupts -- so a cancellation that arrived while the
    // instrument was still being opened would be lost if the run were started
    // at all. It is not started: the start commands never go out, and there is
    // nothing to release on the way back.
    if (m_abortRequested.load()) {
        report(QStringLiteral("* cancelled before the first sweep"));
    } else {
        engine.start();
    }

    {
        const std::scoped_lock lock{m_engineMutex};
        m_engine = nullptr;
    }
    (*driver)->close();
    if (m_transport) {
        m_transport->close();
    }
    m_elapsed = std::chrono::milliseconds{clock.elapsed()};

    const auto phase = engine.phase();

    // Anything the run did produce is written out, however it ended: a job that
    // failed in the last band still measured the first three, and a cancelled
    // one still has the points it verified. A run that produced nothing writes
    // nothing -- five empty artefacts only look like a measurement gone wrong.
    if (!m_session.isEmpty()) {
        if (auto status = writeOutputs(); !status) {
            err() << QStringLiteral("peakemi: %1\n").arg(qs(status.error().message()));
            err().flush();
            return exit_code::RunFailed;
        }
    }

    if (phase == MeasurementEngine::Phase::Failed) {
        err() << QStringLiteral("peakemi: the run failed: %1\n").arg(qs(m_error.message()));
        err().flush();
        // The summary is printed on this path too: whatever reads the job's
        // output should not have to special-case a run that ended badly.
        printSummary(exit_code::RunFailed);
        return exit_code::RunFailed;
    }

    const int code = exitCodeFor(phase);
    printSummary(code);
    return code;
}

int HeadlessRunner::exitCodeFor(MeasurementEngine::Phase phase) const
{
    // The cancellation wins over whatever the engine got to before it landed:
    // a job the pipeline stopped must never exit claiming the equipment passed.
    if (m_abortRequested.load() || phase == MeasurementEngine::Phase::Aborted) {
        return exit_code::Aborted;
    }
    if (phase != MeasurementEngine::Phase::Finished) {
        return exit_code::RunFailed;
    }
    return exceeds(m_session.overallVerdict(), m_options.failOn) ? exit_code::LimitsExceeded
                                                                 : exit_code::Success;
}

Status HeadlessRunner::writeOutputs()
{
    // --output-dir fills in every artefact the caller did not name explicitly,
    // under fixed names: a pipeline archives the directory without having to
    // know the generated run id.
    QString session = m_options.sessionOutput;
    QString resultsCsv = m_options.resultsCsvOutput;
    QString resultsJson = m_options.resultsJsonOutput;
    QString traceCsv = m_options.traceCsvOutput;
    QString reportPdf = m_options.reportPdfOutput;
    if (!m_options.outputDirectory.isEmpty()) {
        const QDir directory{m_options.outputDirectory};
        if (!QDir{}.mkpath(directory.absolutePath())) {
            return fail(ErrorCode::IoFailure,
                        "could not create " + directory.absolutePath().toStdString());
        }
        const auto fill = [&directory](QString& path, const QString& name) {
            if (path.isEmpty()) {
                path = directory.filePath(name);
            }
        };
        fill(session, QStringLiteral("session.peakemi.json"));
        fill(resultsCsv, QStringLiteral("results.csv"));
        fill(resultsJson, QStringLiteral("results.json"));
        fill(traceCsv, QStringLiteral("trace.csv"));
        fill(reportPdf, QStringLiteral("report.pdf"));
    }

    const auto note = [this](const QString& path) { m_written.append(path); };

    if (!session.isEmpty()) {
        if (auto status = SessionSerializer::save(m_session, session); !status) {
            return status;
        }
        note(session);
    }
    if (!resultsCsv.isEmpty()) {
        if (auto status = reporting::csv::writeResults(m_session, resultsCsv); !status) {
            return status;
        }
        note(resultsCsv);
    }
    if (!resultsJson.isEmpty()) {
        if (auto status = reporting::json_export::writeResults(m_session, resultsJson); !status) {
            return status;
        }
        note(resultsJson);
    }
    if (!traceCsv.isEmpty() && !m_session.traces.empty()) {
        if (auto status = reporting::csv::writeTrace(m_session, m_session.traces.back(), traceCsv);
            !status)
        {
            return status;
        }
        note(traceCsv);
    }
    if (!reportPdf.isEmpty()) {
        ReportTemplate reportTemplate;
        if (m_options.reportTemplatePath.isEmpty()) {
            reportTemplate = reporting::report_template::loadDefault();
        } else {
            auto loaded = reporting::report_template::load(m_options.reportTemplatePath);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            reportTemplate = *loaded;
        }
        if (!m_session.meta.company.empty()) {
            reportTemplate.companyName = m_session.meta.company;
        }

        reporting::PdfReportRenderer renderer;
        if (m_plotRenderer) {
            renderer.setPlotImage(m_plotRenderer(m_session, QSize{1600, 900}));
        } else {
            // Nothing to draw the spectrum with, so the report says so by
            // omission rather than leaving an empty frame in the layout.
            reportTemplate.includeTracePlot = false;
        }
        if (auto status = renderer.render(m_session, reportTemplate, reportPdf); !status) {
            return status;
        }
        note(reportPdf);
    }
    return {};
}

void HeadlessRunner::printSummary(int code) const
{
    const auto verdict = m_session.overallVerdict();
    const auto worst = m_session.worstResult();
    const double seconds = static_cast<double>(m_elapsed.count()) / 1e3;

    if (m_options.summary == SummaryFormat::Json) {
        QJsonObject summary{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("runId"), qs(m_session.meta.runId)},
            {QStringLiteral("application"), qs(buildIdentification())},
            {QStringLiteral("instrument"), qs(m_instrument.displayName())},
            {QStringLiteral("eut"), qs(m_session.meta.eutName)},
            {QStringLiteral("startHz"), static_cast<double>(m_session.config.span.start.value())},
            {QStringLiteral("stopHz"), static_cast<double>(m_session.config.span.stop.value())},
            {QStringLiteral("verifiedPoints"), static_cast<int>(m_session.results.size())},
            {QStringLiteral("verdict"), qs(verdictKey(verdict))},
            {QStringLiteral("exitCode"), code},
            {QStringLiteral("durationSeconds"), seconds},
            {QStringLiteral("error"),
             m_error.code == ErrorCode::None ? QJsonValue{} : QJsonValue{qs(m_error.message())}},
            {QStringLiteral("disclaimer"), qs(ComplianceDisclaimer)}};

        QJsonArray limits;
        for (const auto& limit : m_session.config.limits) {
            limits.append(qs(limit.name));
        }
        summary.insert(QStringLiteral("limits"), limits);

        QJsonArray points;
        for (const auto& point : m_session.results) {
            points.append(toJson(point));
        }
        summary.insert(QStringLiteral("results"), points);
        summary.insert(QStringLiteral("worst"), worst ? toJson(*worst) : QJsonValue{});

        QJsonArray files;
        for (const auto& path : m_written) {
            files.append(path);
        }
        summary.insert(QStringLiteral("files"), files);

        out() << QString::fromUtf8(QJsonDocument{summary}.toJson(QJsonDocument::Indented));
        out().flush();
        return;
    }

    out() << QStringLiteral("run id          : %1\n").arg(qs(m_session.meta.runId));
    out() << QStringLiteral("instrument      : %1\n").arg(qs(m_instrument.displayName()));
    out() << QStringLiteral("span            : %1 to %2\n")
                 .arg(megahertzText(m_session.config.span.start),
                      megahertzText(m_session.config.span.stop));
    for (const auto& limit : m_session.config.limits) {
        out() << QStringLiteral("limit           : %1\n").arg(qs(limit.name));
    }
    out() << QStringLiteral("verified points : %1\n").arg(m_session.results.size());
    out() << QStringLiteral("verdict         : %1\n").arg(qs(verdictKey(verdict)).toUpper());
    if (worst) {
        out() << QStringLiteral("worst margin    : %1 dB at %2 against %3\n")
                     .arg(QString::number(worst->marginDb, 'f', 1),
                          megahertzText(worst->frequency),
                          qs(worst->limitName));
    }
    for (const auto& path : m_written) {
        out() << QStringLiteral("written         : %1\n").arg(path);
    }
    out() << QStringLiteral("duration        : %1 s\n").arg(seconds, 0, 'f', 1);
    out() << QStringLiteral("%1\n").arg(qs(ComplianceDisclaimer));
    out().flush();
}

} // namespace peakemi::cli
