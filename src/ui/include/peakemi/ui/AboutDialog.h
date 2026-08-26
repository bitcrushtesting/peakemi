#pragma once

#include <QDialog>
#include <QString>

namespace peakemi::ui {

/// What PeakEmi is, who made it, what it may be used for and where to get it.
///
/// A dialog of its own rather than QMessageBox::about, because the links have
/// to be clickable: a licence a user cannot open is not much of a licence
/// notice, and a repository nobody can reach is not much of a source offer.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

    /// Where the source lives. Also the source offer the GPL expects to
    /// accompany a distributed binary.
    [[nodiscard]] static QString repositoryUrl();
    [[nodiscard]] static QString licenceUrl();
};

} // namespace peakemi::ui
