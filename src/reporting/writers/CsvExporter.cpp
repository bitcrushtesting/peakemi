#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/CisprBands.h>
#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/Time.h>
#include <peakemi/reporting/CsvExporter.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>

namespace peakemi::reporting::csv {
namespace {

[[nodiscard]] std::string quote(std::string_view text)
{
    std::string quoted{'"'};
    for (const char character : text) {
        if (character == '"') {
            quoted += '"';
        }
        quoted += character;
    }
    quoted += '"';
    return quoted;
}

/// Wrap the disclaimer into `#` comment lines so it survives every spreadsheet.
void writeDisclaimer(std::ostringstream& stream)
{
    constexpr std::size_t LineWidth = 90;
    std::string_view remaining = ComplianceDisclaimer;
    while (!remaining.empty()) {
        std::size_t length = std::min(LineWidth, remaining.size());
        if (length < remaining.size()) {
            const auto space = remaining.rfind(' ', length);
            if (space != std::string_view::npos && space > 0) {
                length = space;
            }
        }
        stream << "# " << remaining.substr(0, length) << '\n';
        remaining.remove_prefix(length);
        while (!remaining.empty() && remaining.front() == ' ') {
            remaining.remove_prefix(1);
        }
    }
}

void writeHeader(std::ostringstream& stream, const Session& session, const InstrumentId& instrument)
{
    stream << "# PeakEmi measurement export\n"
           << "# application_version: " << session.meta.applicationVersion << '\n'
           << "# run_id: " << session.meta.runId << '\n'
           << "# exported_at: " << toIso8601(std::chrono::system_clock::now()) << '\n'
           << "# eut: " << session.meta.eutName << '\n'
           << "# eut_serial: " << session.meta.eutSerial << '\n'
           << "# operating_mode: " << session.meta.eutOperatingMode << '\n'
           << "# operator: " << session.meta.operatorName << '\n'
           << "# instrument: " << instrument.displayName() << '\n'
           << "# instrument_firmware: " << instrument.firmware << '\n';
    for (const auto& correction : session.config.corrections) {
        stream << "# correction: " << correction.name << " (" << correctionKindKey(correction.kind)
               << ", " << (correction.enabled ? "applied" : "disabled") << ", "
               << correction.points.size() << " points)\n";
    }
    for (const auto& limit : session.config.limits) {
        stream << "# limit: " << limit.name << " [" << limit.standard << "]\n";
    }
    writeDisclaimer(stream);
}

[[nodiscard]] std::string number(double value, int precision = 2)
{
    if (!std::isfinite(value)) {
        return {};
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

} // namespace

std::string traceToCsv(const Session& session, const Trace& trace)
{
    std::ostringstream stream;
    writeHeader(stream, session, trace.source);
    stream << "# trace: " << trace.label << '\n'
           << "# detector: " << detectorKey(trace.detector) << '\n'
           << "# unit: " << amplitudeUnitKey(trace.unit) << '\n'
           << "# corrections_applied: " << (trace.corrected ? "yes" : "no") << '\n'
           << "# rbw_hz: " << trace.params.rbw.value() << '\n'
           << "# vbw_hz: " << trace.params.vbw.value() << '\n'
           << "# acquired_at: " << toIso8601(trace.acquiredAt) << '\n'
           << "frequency_hz,amplitude_" << amplitudeUnitKey(trace.unit) << '\n';

    for (int i = 0; i < trace.size(); ++i) {
        stream << trace.axis.frequencyAt(i).value() << ','
               << number(trace.amplitudes[static_cast<std::size_t>(i)]) << '\n';
    }
    return stream.str();
}

std::string resultsToCsv(const Session& session)
{
    std::ostringstream stream;
    const InstrumentId instrument =
        session.results.empty() ? InstrumentId{} : session.results.front().instrument;
    writeHeader(stream, session, instrument);
    stream << "frequency_hz,frequency_mhz,raw_amplitude,corrected_amplitude,unit,limit,margin_db,"
              "verdict,detector,rbw_hz,vbw_hz,dwell_ms,cispr_band,corrections,limit_name,"
              "measured_at\n";

    for (const auto& point : session.results) {
        std::ostringstream corrections;
        for (std::size_t i = 0; i < point.corrections.size(); ++i) {
            if (i > 0) {
                corrections << "; ";
            }
            corrections << point.corrections[i].name << '='
                        << number(point.corrections[i].contributionDb) << " dB";
        }
        stream << point.frequency.value() << ',' << number(toMegahertz(point.frequency), 6) << ','
               << number(point.rawAmplitude) << ',' << number(point.correctedAmplitude) << ','
               << amplitudeUnitKey(point.unit) << ',' << number(point.limitValue) << ','
               << number(point.marginDb) << ',' << verdictKey(point.verdict) << ','
               << detectorKey(point.detector) << ',' << point.rbw.value() << ','
               << point.vbw.value() << ',' << point.dwell.count() << ','
               << cisprBandKey(cisprBandFor(point.frequency).band) << ','
               << quote(corrections.str()) << ',' << quote(point.limitName) << ','
               << toIso8601(point.measuredAt) << '\n';
    }
    return stream.str();
}

Status writeTrace(const Session& session, const Trace& trace, const QString& path)
{
    return writeFileAtomically(path, traceToCsv(session, trace));
}

Status writeResults(const Session& session, const QString& path)
{
    return writeFileAtomically(path, resultsToCsv(session));
}

} // namespace peakemi::reporting::csv
