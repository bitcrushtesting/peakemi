#include <peakemi/core/Logging.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/Version.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/ui/MainWindow.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QStandardPaths>
#include <QString>

namespace {

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

/// Composition root: everything is wired here and nowhere else.
int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(toQString(peakemi::ProjectName));
    // The full version, because --version is asked precisely when someone
    // needs to know which build they have.
    QCoreApplication::setApplicationVersion(toQString(peakemi::ProjectVersionFull));
    QCoreApplication::setOrganizationName(toQString(peakemi::OrganizationName));
    QCoreApplication::setOrganizationDomain(toQString(peakemi::OrganizationDomain));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "EMI pre-compliance measurement suite"));
    parser.addHelpOption();
    parser.addVersionOption();
    // Not "-v": that short form belongs to --version.
    const QCommandLineOption verboseOption{
        QStringLiteral("verbose"),
        QCoreApplication::translate("main", "Log the full SCPI transcript.")};
    parser.addOption(verboseOption);
    parser.addPositionalArgument(
        QStringLiteral("session"),
        QCoreApplication::translate("main", "Session file to open on start-up."),
        QStringLiteral("[session]"));
    parser.process(application);

    // Logging first, so start-up problems land in the file too (FR-APP-1).
    const auto logDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
    peakemi::installRotatingFileLogger(logDirectory);
    QLoggingCategory::setFilterRules(parser.isSet(verboseOption)
                                         ? QStringLiteral("peakemi.*.debug=true")
                                         : QStringLiteral("peakemi.*.debug=false"));

    // First line of every log: which build wrote the rest of it. A report
    // question months later starts here.
    qCInfo(peakemi::lcCore) << toQString(peakemi::buildIdentification());

    // Value types travel across thread boundaries by queued connection, which
    // needs them registered before the first connect().
    peakemi::registerMetaTypes();
    peakemi::drivers::registerBuiltInDrivers();

    // Compiled in rather than loaded from disk, so the icon is there on every
    // platform and in every packaging of the application. AboutDialog.cpp pulls
    // the resource into the link.
    QApplication::setWindowIcon(QIcon{QStringLiteral(":/peakemi/icon.png")});

    peakemi::ui::MainWindow window;
    window.show();

    const auto positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openSessionFile(positional.first());
    }

    return QApplication::exec();
}
