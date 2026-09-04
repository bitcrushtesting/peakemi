#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/ITransport.h>
#include <peakemi/core/Units.h>

#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace peakemi::cli {

/// Exit codes of the headless runner.
///
/// A pipeline scripts against these numbers, so they are part of the published
/// interface: they are documented in the README and may not be renumbered
/// without a major version. The split between 1 and 3 is the one that matters —
/// "the equipment under test emitted too much" is a result, "the instrument
/// stopped answering" is a broken job, and a pipeline treats them differently.
namespace exit_code {

inline constexpr int Success = 0;        ///< the run finished within the limits
inline constexpr int LimitsExceeded = 1; ///< finished, but worse than --fail-on allows
inline constexpr int UsageError = 2;     ///< the command line or an input file was wrong
inline constexpr int RunFailed = 3;      ///< instrument, transport or output failure
inline constexpr int Aborted = 4;        ///< SIGINT/SIGTERM: the job was cancelled

} // namespace exit_code

/// Worst verdict a run may reach and still exit successfully.
enum class FailOn : std::uint8_t
{
    Never,   ///< the exit code reports only whether the run completed
    Fail,    ///< the default: an exceeded limit fails the job
    Marginal ///< anything inside the marginal band fails the job too
};

/// Shape of the summary written to stdout when the run ends.
enum class SummaryFormat : std::uint8_t
{
    Text,
    Json
};

/// Everything a headless run needs, as parsed from the command line.
///
/// The overrides are optional rather than defaulted: a session file supplies
/// the baseline and the command line amends it, so an option nobody passed must
/// be distinguishable from one that happens to match the default.
struct HeadlessOptions
{
    // --- where the instrument is -------------------------------------------
    TransportDescriptor endpoint{.kind = TransportKind::Simulated};
    std::string driverId; ///< empty: choose by scoring the *IDN? reply

    // --- what to measure ----------------------------------------------------
    QString sessionPath;     ///< configuration baseline; empty for the defaults
    QStringList limits;      ///< catalogue name or path to a CSV/JSON limit line
    QStringList corrections; ///< paths to CSV/JSON correction tables
    std::optional<Hertz> start;
    std::optional<Hertz> stop;
    std::optional<int> points;
    std::optional<Decibel> refLevel;
    std::optional<Detector> verificationDetector;
    std::optional<std::chrono::milliseconds> dwell;
    std::optional<int> passes;
    std::optional<int> maximumPeaks;
    std::optional<double> marginThresholdDb;
    std::vector<std::string> startCommands;
    std::vector<std::string> stopCommands;

    // --- who measured, and what ---------------------------------------------
    std::string eutName;
    std::string eutSerial;
    std::string operatorName;
    std::string company;
    std::string notes;
    std::string runId; ///< empty: generated, and printed in the summary

    // --- what to write ------------------------------------------------------
    /// Writes session, results.csv, results.json and report.pdf under fixed
    /// names, so a pipeline can archive a directory without knowing the run id.
    QString outputDirectory;
    QString sessionOutput;
    QString resultsCsvOutput;
    QString resultsJsonOutput;
    QString traceCsvOutput;
    QString reportPdfOutput;
    QString reportTemplatePath;

    // --- how it behaves -----------------------------------------------------
    FailOn failOn{FailOn::Fail};
    SummaryFormat summary{SummaryFormat::Text};
    bool quiet{false};    ///< progress off; the summary and errors still print
    bool verbose{false};  ///< the full SCPI transcript
    QString logDirectory; ///< empty: stderr only, which is what CI keeps anyway
};

/// What the parsed command line asks for.
enum class Action : std::uint8_t
{
    Run,
    ShowHelp,
    ShowVersion,
    ListDrivers,
    ListLimits
};

struct CommandLine
{
    Action action{Action::Run};
    HeadlessOptions options;
    /// Filled for every action, so a usage error can print it alongside.
    QString helpText;
};

/// Parses @p arguments, argv[0] included.
///
/// Pure: it neither prints nor exits, which is what makes the whole surface
/// testable and lets the caller decide where usage errors go.
[[nodiscard]] Result<CommandLine> parseCommandLine(const QStringList& arguments);

/// The same usage text `--help` prints, available without a successful parse so
/// a refused command line can be shown what it should have looked like.
[[nodiscard]] QString usageText();

/// "30M", "150kHz", "1e9", "1000000" -> hertz. A bare number is hertz.
[[nodiscard]] Result<Hertz> parseFrequency(const QString& text);

/// "1s", "500ms", "2min" -> milliseconds. A bare number is milliseconds.
[[nodiscard]] Result<std::chrono::milliseconds> parseDuration(const QString& text);

/// An endpoint as one string, so the bus and its address stay together:
///
///     sim
///     tcp:192.168.1.20            tcp:192.168.1.20:5555
///     vxi11:10.0.0.5
///     serial:/dev/ttyUSB0         serial:COM3:9600
///     usbtmc:USB0::0xF4EC::0x1300::SSA3XABC::INSTR
///     visa:TCPIP0::10.0.0.5::inst0::INSTR
[[nodiscard]] Result<TransportDescriptor> parseEndpoint(const QString& text);

} // namespace peakemi::cli
