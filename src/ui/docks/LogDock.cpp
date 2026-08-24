#include <peakemi/ui/LogDock.hpp>

#include <QDateTime>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace peakemi::ui {

LogDock::LogDock(QWidget* parent) : QDockWidget{tr("Log and console"), parent}
{
    setObjectName(QStringLiteral("logDock"));

    m_tabs = new QTabWidget{this};

    m_log = new QPlainTextEdit{m_tabs};
    m_log->setObjectName(QStringLiteral("applicationLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_tabs->addTab(m_log, tr("Application log"));

    auto* consolePage = new QWidget{m_tabs};
    auto* consoleLayout = new QVBoxLayout{consolePage};
    m_console = new QPlainTextEdit{consolePage};
    m_console->setObjectName(QStringLiteral("scpiConsole"));
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(5000);
    consoleLayout->addWidget(m_console, 1);

    m_commandInput = new QLineEdit{consolePage};
    m_commandInput->setObjectName(QStringLiteral("scpiCommandInput"));
    m_commandInput->setPlaceholderText(tr("SCPI command, e.g. *IDN?"));
    m_commandInput->setClearButtonEnabled(true);
    consoleLayout->addWidget(m_commandInput);
    m_tabs->addTab(consolePage, tr("SCPI console"));

    setWidget(m_tabs);

    connect(m_commandInput, &QLineEdit::returnPressed, this, [this] {
        const auto command = m_commandInput->text().trimmed();
        if (command.isEmpty()) {
            return;
        }
        appendConsole(QStringLiteral("> %1").arg(command));
        m_commandInput->clear();
        emit commandEntered(command);
    });
}

void LogDock::appendLog(const QString& message)
{
    const auto stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, message));
}

void LogDock::appendConsole(const QString& text)
{
    m_console->appendPlainText(text);
}

QString LogDock::logText() const
{
    return m_log->toPlainText();
}

} // namespace peakemi::ui
