#include <peakemi/core/Version.hpp>

#include <QTest>

class VersionTest : public QObject
{
    Q_OBJECT

private slots:
    void versionIsNotEmpty();
    void buildIdentificationContainsNameAndVersion();
};

void VersionTest::versionIsNotEmpty()
{
    QVERIFY(!peakemi::ProjectVersion.empty());
    QCOMPARE(peakemi::ProjectVersionMajor, 0);
}

void VersionTest::buildIdentificationContainsNameAndVersion()
{
    const auto identification = peakemi::buildIdentification();
    QVERIFY(identification.find(peakemi::ProjectName) != std::string_view::npos);
    QVERIFY(identification.find(peakemi::ProjectVersion) != std::string_view::npos);
}

QTEST_APPLESS_MAIN(VersionTest)
#include "VersionTest.moc"
