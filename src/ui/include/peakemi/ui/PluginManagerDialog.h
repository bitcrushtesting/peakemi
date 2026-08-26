#pragma once

#include <peakemi/python/PluginRegistry.h>

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace peakemi::ui {

/// Lists the discovered Python plugins with their status, origin and last
/// error, and lets the user approve, revoke and rescan without restarting
/// (FR-EXT-6, NFR-EXT-1).
class PluginManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog(python::PluginRegistry& registry, QWidget* parent = nullptr);

private slots:
    void rescan();
    void approveSelected();
    void revokeSelected();
    void openPluginFolder();
    void updateButtons();

private:
    [[nodiscard]] QString selectedPath() const;

    python::PluginRegistry& m_registry;
    QTreeWidget* m_tree{nullptr};
    QPushButton* m_approveButton{nullptr};
    QPushButton* m_revokeButton{nullptr};
    QLabel* m_detail{nullptr};
};

} // namespace peakemi::ui
