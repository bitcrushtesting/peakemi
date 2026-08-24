#include <peakemi/ui/MainWindow.hpp>

#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTest>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void constructsWithMenusAndCentralWidget();
};

void MainWindowTest::constructsWithMenusAndCentralWidget()
{
    peakemi::ui::MainWindow window;

    QVERIFY(window.centralWidget() != nullptr);
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("centralPlaceholder")) != nullptr);
    QCOMPARE(window.menuBar()->actions().size(), 2);
    QVERIFY(!window.windowTitle().isEmpty());
}

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
