#include <peakemi/cli/CommandLine.h>
#include <peakemi/cli/HeadlessRunner.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/Version.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/hal/DriverRegistry.h>
#include <peakemi/ui/SessionPlot.h>

#include <QApplication>
#include <QString>
#include <QTextStream>
#include <QThread>

#include <atomic>
#include <csignal>

namespace {

using namespace peakemi;

/// Set by the signal handler and by nothing else. A handler may touch a
/// lock-free atomic and little more, so the flag is all it sets: the watcher
/// thread below does the part that takes a mutex.
std::atomic_bool g_cancelled{false};

extern "C" void onTerminationSignal(int /*signal*/)
{
    g_cancelled.store(true);
}

/// Turns the flag into an abort request. A cancelled pipeline job still has to
/// leave the bench safe, and the engine only sends the stop commands if it is
/// asked to stop rather than killed (FR-RUN-9).
class CancellationWatcher : public QThread
{
public:
    explicit CancellationWatcher(cli::HeadlessRunner& runner) : m_runner{runner} {}

    void stop()
    {
        m_stopping.store(true);
        wait();
    }

protected:
    void run() override
    {
        while (!m_stopping.load()) {
            if (g_cancelled.load()) {
                m_runner.requestAbort();
                return;
            }
            msleep(PollIntervalMs);
        }
    }

private:
    static constexpr unsigned long PollIntervalMs = 50;

    cli::HeadlessRunner& m_runner;
    std::atomic_bool m_stopping{false};
};

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] int listDrivers(QTextStream& stream)
{
    drivers::registerBuiltInDrivers();
    for (const auto& driver : hal::DriverRegistry::instance().drivers()) {
        stream << QStringLiteral("%1\t%2\t%3\n")
                      .arg(qs(driver.id), qs(driver.name), qs(driver.origin));
    }
    stream.flush();
    return cli::exit_code::Success;
}

[[nodiscard]] int listLimits(QTextStream& stream)
{
    for (const auto& limit : builtInLimitLines()) {
        stream << QStringLiteral("%1\t%2\n").arg(qs(limit.name), qs(limit.standard));
    }
    stream.flush();
    return cli::exit_code::Success;
}

} // namespace

/// Composition root of the headless runner.
///
/// A console executable of its own rather than a switch inside the GUI binary:
/// on Windows the application is linked as a GUI subsystem program and has no
/// console to write to, which is precisely where a build server needs to read
/// its output.
int main(int argc, char* argv[])
{
    // A QApplication rather than a QCoreApplication: the PDF report embeds the
    // same spectrum the operator sees, and drawing it goes through the widget
    // toolkit. It needs no display -- the offscreen platform plugin is enough --
    // so a build machine with no window system is asked to use it, unless the
    // caller has already chosen a platform.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication application{argc, argv};
    QCoreApplication::setApplicationName(qs(peakemi::ProjectName));
    QCoreApplication::setApplicationVersion(qs(peakemi::ProjectVersionFull));
    QCoreApplication::setOrganizationName(qs(peakemi::OrganizationName));
    QCoreApplication::setOrganizationDomain(qs(peakemi::OrganizationDomain));

    QTextStream output{stdout};
    QTextStream errors{stderr};

    auto parsed = peakemi::cli::parseCommandLine(QCoreApplication::arguments());
    if (!parsed) {
        errors << QStringLiteral("peakemi-cli: %1\n\n").arg(qs(parsed.error().detail));
        errors << peakemi::cli::usageText();
        errors.flush();
        return peakemi::cli::exit_code::UsageError;
    }

    switch (parsed->action) {
        case peakemi::cli::Action::ShowHelp:
            output << parsed->helpText;
            output.flush();
            return peakemi::cli::exit_code::Success;
        case peakemi::cli::Action::ShowVersion:
            output << qs(peakemi::buildIdentification()) << '\n';
            output.flush();
            return peakemi::cli::exit_code::Success;
        case peakemi::cli::Action::ListDrivers:
            return listDrivers(output);
        case peakemi::cli::Action::ListLimits:
            return listLimits(output);
        case peakemi::cli::Action::Run:
            break;
    }

    peakemi::cli::HeadlessRunner runner{parsed->options};
    runner.setPlotRenderer(&peakemi::ui::renderSessionSpectrum);

    std::signal(SIGINT, onTerminationSignal);
    std::signal(SIGTERM, onTerminationSignal);
    CancellationWatcher watcher{runner};
    watcher.start();

    const int code = runner.run();

    watcher.stop();
    return code;
}
