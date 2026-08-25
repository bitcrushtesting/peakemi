#pragma once

#include <QDockWidget>

class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

namespace peakemi::ui {

/// Application log plus the raw SCPI console (FR-APP-1, FR-COM-6).
///
/// The console is the tool people reach for when writing a new driver or filing
/// a bug report, so its transcript is the same text that goes into the log file.
class LogDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit LogDock(QWidget* parent = nullptr);

    void appendLog(const QString& message);
    void appendConsole(const QString& text);

    [[nodiscard]] QString logText() const;

signals:
    void commandEntered(QString command);

private:
    QPlainTextEdit* m_log{nullptr};
    QPlainTextEdit* m_console{nullptr};
    QLineEdit* m_commandInput{nullptr};
    QTabWidget* m_tabs{nullptr};
};

} // namespace peakemi::ui
