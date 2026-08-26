#include "TestSupport.h"

#include <peakemi/python/PluginManifest.h>
#include <peakemi/python/PluginRegistry.h>
#include <peakemi/python/PythonInterpreter.h>
#include <peakemi/python/TrustStore.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace peakemi;
using namespace peakemi::python;

namespace {

/// Write a plugin file into @p directory and return its path.
[[nodiscard]] QString
writePlugin(const QTemporaryDir& directory, const QString& name, const QByteArray& source)
{
    auto path = directory.filePath(name);
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(source);
    return path;
}

[[nodiscard]] QByteArray exampleSource()
{
    QFile file{QStringLiteral(PEAKEMI_EXAMPLE_PLUGIN)};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

[[nodiscard]] PluginManifest validManifest()
{
    PluginManifest manifest;
    manifest.name = "Example sweeper";
    manifest.vendor = "PeakEmi";
    manifest.apiVersion = std::string{PluginApiVersion};
    manifest.models = {"Example.*"};
    return manifest;
}

} // namespace

class PluginTest : public QObject
{
    Q_OBJECT

private slots:
    void apiVersionsMatchOnTheMajor();
    void manifestValidationCatchesMistakes();
    void manifestScoringRanksSpecificity();
    void trustStoreStartsEmpty();
    void trustStoreApprovesAFile();
    void trustStoreNoticesAnEditedPlugin();
    void trustStoreRevokes();
    void trustStorePersists();
    void trustStoreReportsAMissingFile();
    void registryIgnoresUnapprovedPlugins();
    void registryLoadsTheExamplePlugin();
    void registryReportsAPluginThatRaises();
    void registryRejectsAnIncompatibleApiVersion();
    void registryRejectsAFileThatRegistersNothing();
};

void PluginTest::apiVersionsMatchOnTheMajor()
{
    QVERIFY(isApiVersionCompatible(std::string{PluginApiVersion}));
    QVERIFY(isApiVersionCompatible("1.0"));
    // Minor versions only add, so a plugin written for a later minor still runs.
    QVERIFY(isApiVersionCompatible("1.7"));
    QVERIFY(!isApiVersionCompatible("2.0"));
    QVERIFY(!isApiVersionCompatible("0.9"));
    QVERIFY(!isApiVersionCompatible("nonsense"));
    QVERIFY(!isApiVersionCompatible(""));
}

void PluginTest::manifestValidationCatchesMistakes()
{
    QVERIFY(validateManifest(validManifest()).has_value());

    auto unnamed = validManifest();
    unnamed.name.clear();
    QCOMPARE(validateManifest(unnamed).error().code, ErrorCode::InvalidConfiguration);

    auto futureApi = validManifest();
    futureApi.apiVersion = "9.0";
    QCOMPARE(validateManifest(futureApi).error().code, ErrorCode::SchemaVersionUnsupported);

    auto noModels = validManifest();
    noModels.models.clear();
    QVERIFY(!validateManifest(noModels).has_value());

    auto badPattern = validManifest();
    badPattern.models = {"([unclosed"};
    const auto rejected = validateManifest(badPattern);
    QVERIFY(!rejected.has_value());
    QVERIFY(rejected.error().detail.find("invalid model pattern") != std::string::npos);
}

void PluginTest::manifestScoringRanksSpecificity()
{
    const auto manifest = validManifest(); // models: {"Example.*"}

    InstrumentId identity;
    identity.manufacturer = "PeakEmi";
    identity.model = "Example Sweeper";

    // "Example.*" is a family claim however much of the name it covers, so a
    // C++ driver naming this model exactly still outranks the plugin.
    QCOMPARE(scoreManifest(manifest, identity), 70);

    InstrumentId longerModel;
    longerModel.manufacturer = "PeakEmi";
    longerModel.model = "Example Sweeper Mark II";
    QCOMPARE(scoreManifest(manifest, longerModel), 70);

    // A literal pattern naming one model is an exact claim.
    auto exactManifest = manifest;
    exactManifest.models = {"Example Sweeper"};
    QCOMPARE(scoreManifest(exactManifest, identity), 100);
    QCOMPARE(scoreManifest(exactManifest, longerModel), 70); // matches only a prefix

    InstrumentId vendorOnly;
    vendorOnly.manufacturer = "PeakEmi";
    vendorOnly.model = "Something else";
    QCOMPARE(scoreManifest(manifest, vendorOnly), 30);

    InstrumentId foreign;
    foreign.manufacturer = "Keysight";
    foreign.model = "Example Sweeper";
    QCOMPARE(scoreManifest(manifest, foreign), 0);
}

void PluginTest::trustStoreStartsEmpty()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const TrustStore store{directory.filePath(QStringLiteral("trust.json"))};
    QVERIFY(store.entries().empty());

    const auto plugin = writePlugin(directory, QStringLiteral("p.py"), "x = 1\n");
    QCOMPARE(store.state(plugin), TrustState::Unknown);
    QVERIFY(!store.isTrusted(plugin));
}

void PluginTest::trustStoreApprovesAFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrustStore store{directory.filePath(QStringLiteral("trust.json"))};
    const auto plugin = writePlugin(directory, QStringLiteral("p.py"), "x = 1\n");

    const auto approved = store.approve(plugin);
    QVERIFY2(approved.has_value(), test::errorText(approved).constData());
    QCOMPARE(store.state(plugin), TrustState::Trusted);
    QVERIFY(store.isTrusted(plugin));
    QCOMPARE(store.entries().size(), 1U);
    QCOMPARE(store.entries().front().sha256.size(), 64U); // hex SHA-256
}

void PluginTest::trustStoreNoticesAnEditedPlugin()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrustStore store{directory.filePath(QStringLiteral("trust.json"))};
    const auto plugin = writePlugin(directory, QStringLiteral("p.py"), "x = 1\n");
    QVERIFY(store.approve(plugin).has_value());

    // Approval is for the contents, not the name: editing the file withdraws it.
    QVERIFY(!writePlugin(directory, QStringLiteral("p.py"), "import os\nos.system('rm -rf /')\n")
                 .isEmpty());
    QCOMPARE(store.state(plugin), TrustState::Changed);
    QVERIFY(!store.isTrusted(plugin));
}

void PluginTest::trustStoreRevokes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrustStore store{directory.filePath(QStringLiteral("trust.json"))};
    const auto plugin = writePlugin(directory, QStringLiteral("p.py"), "x = 1\n");

    QVERIFY(store.approve(plugin).has_value());
    QVERIFY(store.revoke(plugin).has_value());
    QCOMPARE(store.state(plugin), TrustState::Unknown);
    QVERIFY(store.entries().empty());
}

void PluginTest::trustStorePersists()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto storePath = directory.filePath(QStringLiteral("trust.json"));
    const auto plugin = writePlugin(directory, QStringLiteral("p.py"), "x = 1\n");
    {
        TrustStore store{storePath};
        QVERIFY(store.approve(plugin).has_value());
    }

    const TrustStore reopened{storePath};
    QCOMPARE(reopened.entries().size(), 1U);
    QVERIFY(reopened.isTrusted(plugin));
}

void PluginTest::trustStoreReportsAMissingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TrustStore store{directory.filePath(QStringLiteral("trust.json"))};

    const auto missing = directory.filePath(QStringLiteral("not-there.py"));
    QCOMPARE(store.approve(missing).error().code, ErrorCode::IoFailure);
    QCOMPARE(store.state(missing), TrustState::Unknown);
}

void PluginTest::registryIgnoresUnapprovedPlugins()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(
        !writePlugin(directory, QStringLiteral("example_sweeper.py"), exampleSource()).isEmpty());

    PluginRegistry registry;
    registry.setSearchPaths({directory.path()});
    registry.rescan();

    // Nothing is imported before the user approves it, whatever it contains.
    QCOMPARE(registry.plugins().size(), 1U);
    QCOMPARE(registry.plugins().front().state, PluginState::AwaitingApproval);
    QVERIFY(registry.plugins().front().lastError.find("not approved") != std::string::npos);
}

void PluginTest::registryLoadsTheExamplePlugin()
{
    if (!PluginRegistry::isSupported()) {
        QSKIP("this build has no embedded Python");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto plugin =
        writePlugin(directory, QStringLiteral("example_sweeper.py"), exampleSource());
    QVERIFY(!plugin.isEmpty());

    PluginRegistry registry;
    registry.setSearchPaths({directory.path()});
    QVERIFY(registry.trustStore().approve(plugin).has_value());
    registry.rescan();

    QCOMPARE(registry.plugins().size(), 1U);
    const auto& loaded = registry.plugins().front();
    QVERIFY2(loaded.state == PluginState::Loaded, loaded.lastError.c_str());
    QCOMPARE(loaded.manifest.name, std::string{"Example sweeper"});
    QCOMPARE(loaded.manifest.vendor, std::string{"PeakEmi"});
    QCOMPARE(loaded.manifest.apiVersion, std::string{PluginApiVersion});
    QVERIFY(!loaded.manifest.models.empty());
}

void PluginTest::registryReportsAPluginThatRaises()
{
    if (!PluginRegistry::isSupported()) {
        QSKIP("this build has no embedded Python");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto plugin = writePlugin(
        directory, QStringLiteral("broken.py"), "raise RuntimeError('this plugin is broken')\n");

    PluginRegistry registry;
    registry.setSearchPaths({directory.path()});
    QVERIFY(registry.trustStore().approve(plugin).has_value());
    registry.rescan();

    // A plugin that raises is recorded, not fatal (FR-EXT-4).
    QCOMPARE(registry.plugins().size(), 1U);
    QCOMPARE(registry.plugins().front().state, PluginState::Failed);
    QVERIFY(registry.plugins().front().lastError.find("this plugin is broken") !=
            std::string::npos);
}

void PluginTest::registryRejectsAnIncompatibleApiVersion()
{
    if (!PluginRegistry::isSupported()) {
        QSKIP("this build has no embedded Python");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto plugin = writePlugin(directory,
                                    QStringLiteral("future.py"),
                                    "import peakemi_plugin as api\n"
                                    "@api.register_driver({'name': 'From the future',\n"
                                    "                      'api_version': '99.0',\n"
                                    "                      'models': ['X.*']})\n"
                                    "class Future:\n"
                                    "    pass\n");

    PluginRegistry registry;
    registry.setSearchPaths({directory.path()});
    QVERIFY(registry.trustStore().approve(plugin).has_value());
    registry.rescan();

    QCOMPARE(registry.plugins().front().state, PluginState::Rejected);
    QVERIFY(registry.plugins().front().lastError.find("99.0") != std::string::npos);
}

void PluginTest::registryRejectsAFileThatRegistersNothing()
{
    if (!PluginRegistry::isSupported()) {
        QSKIP("this build has no embedded Python");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto plugin = writePlugin(directory, QStringLiteral("quiet.py"), "VALUE = 42\n");

    PluginRegistry registry;
    registry.setSearchPaths({directory.path()});
    QVERIFY(registry.trustStore().approve(plugin).has_value());
    registry.rescan();

    QCOMPARE(registry.plugins().front().state, PluginState::Rejected);
    QVERIFY(registry.plugins().front().lastError.find("registered no driver") != std::string::npos);
}

QTEST_MAIN(PluginTest)
#include "PluginTest.moc"
