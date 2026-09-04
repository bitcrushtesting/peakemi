#pragma once

#include <peakemi/cli/CommandLine.h>
#include <peakemi/core/IReportRenderer.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/Session.h>
#include <peakemi/python/PluginRegistry.h>

#include <QImage>
#include <QSize>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

namespace peakemi::cli {

/// One measurement from command line to exit code, with no GUI in the way.
///
/// The run happens on the calling thread: there is no operator to keep
/// responsive, so the engine's loop *is* the program. The only concurrency is
/// the abort watcher, which exists so that a cancelled pipeline job still gets
/// its stop commands sent (FR-RUN-9).
class HeadlessRunner
{
public:
    /// Renders the spectrum image the PDF report embeds. Optional: drawing it
    /// needs the widget toolkit, which the runner deliberately does not link,
    /// so the executable injects it and a build without one still reports.
    using PlotRenderer = std::function<QImage(const Session&, QSize)>;

    explicit HeadlessRunner(HeadlessOptions options);
    ~HeadlessRunner();

    HeadlessRunner(const HeadlessRunner&) = delete;
    HeadlessRunner& operator=(const HeadlessRunner&) = delete;
    HeadlessRunner(HeadlessRunner&&) = delete;
    HeadlessRunner& operator=(HeadlessRunner&&) = delete;

    void setPlotRenderer(PlotRenderer renderer);

    /// Configures, connects, measures, writes the outputs and prints the
    /// summary. Blocks until the run has ended. @return an `exit_code` value.
    [[nodiscard]] int run();

    /// Asks the run to stop at its next checkpoint. Thread-safe, and safe to
    /// call before `run()` or after it has returned.
    void requestAbort();

    /// The session the last `run()` produced, complete for a finished run and
    /// partial for an aborted one.
    [[nodiscard]] const Session& session() const { return m_session; }

private:
    /// Contributes the drivers of already-approved Python plugins.
    ///
    /// Approval stays an interactive act (NFR-EXT-1): a plugin this machine has
    /// not approved is reported and left unimported, never trusted just because
    /// nobody is watching.
    void loadPlugins();

    [[nodiscard]] Result<RunConfiguration> buildConfiguration();
    [[nodiscard]] Result<DriverPtr> connectInstrument();
    [[nodiscard]] Status writeOutputs();
    [[nodiscard]] int exitCodeFor(MeasurementEngine::Phase phase) const;

    void report(const QString& message) const;
    void printSummary(int code) const;

    HeadlessOptions m_options;
    PlotRenderer m_plotRenderer;
    python::PluginRegistry m_plugins;

    Session m_session;
    SessionMeta m_meta;
    Error m_error; ///< what runFailed reported, empty when the run completed
    InstrumentId m_instrument;
    TransportPtr m_transport;
    QStringList m_written;

    /// Guards the engine pointer against the aborting thread, which must never
    /// reach an engine the run has already destroyed.
    mutable std::mutex m_engineMutex;
    MeasurementEngine* m_engine{nullptr};
    std::atomic_bool m_abortRequested{false};

    std::chrono::milliseconds m_elapsed{0};
};

} // namespace peakemi::cli
