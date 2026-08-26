#include <peakemi/core/Version.h>

#include <QTest>

#include <string>
#include <string_view>

class VersionTest : public QObject
{
    Q_OBJECT

private slots:
    void versionComesFromTheGitTag();
    void fullVersionIdentifiesTheSource();
    void buildIdentificationContainsNameAndVersion();
};

void VersionTest::versionComesFromTheGitTag()
{
    QVERIFY(!peakemi::ProjectVersion.empty());

    // The numeric part of the label and the components agree, because both
    // come from the same tag. A version assembled from two sources is the
    // thing this arrangement exists to rule out.
    const auto numeric = std::to_string(peakemi::ProjectVersionMajor) + '.' +
                         std::to_string(peakemi::ProjectVersionMinor) + '.' +
                         std::to_string(peakemi::ProjectVersionPatch);
    QVERIFY2(peakemi::ProjectVersion.starts_with(numeric),
             (std::string{"version '"} + std::string{peakemi::ProjectVersion} +
              "' does not start with '" + numeric + '\'')
                 .c_str());
}

void VersionTest::fullVersionIdentifiesTheSource()
{
    QVERIFY(peakemi::ProjectVersionFull.starts_with(peakemi::ProjectVersion));
    QVERIFY(!peakemi::ProjectCommit.empty());
    // Whatever the commit is, the full version names it.
    QVERIFY(peakemi::ProjectVersionFull.find(peakemi::ProjectCommit) != std::string_view::npos);
    QCOMPARE(peakemi::ProjectVersionFull.find(".dirty") != std::string_view::npos,
             peakemi::ProjectBuildIsDirty);
}

void VersionTest::buildIdentificationContainsNameAndVersion()
{
    const auto identification = peakemi::buildIdentification();
    QVERIFY(identification.find(peakemi::ProjectName) != std::string_view::npos);
    QVERIFY(identification.find(peakemi::ProjectVersionFull) != std::string_view::npos);
}

QTEST_APPLESS_MAIN(VersionTest)
#include "VersionTest.moc"
