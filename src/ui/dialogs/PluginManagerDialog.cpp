#include <peakemi/core/Logging.h>
#include <peakemi/ui/PluginManagerDialog.h>

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace peakemi::ui {
namespace {

constexpr int PathRole = Qt::UserRole + 1;

[[nodiscard]] QString stateText(python::PluginState state)
{
    switch (state) {
        case python::PluginState::Loaded:           return QObject::tr("Loaded");
        case python::PluginState::AwaitingApproval: return QObject::tr("Awaiting approval");
        case python::PluginState::Rejected:         return QObject::tr("Rejected");
        case python::PluginState::Failed:           return QObject::tr("Error");
    }
    return {};
}

[[nodiscard]] QColor stateColour(python::PluginState state)
{
    switch (state) {
        case python::PluginState::Loaded:           return QColor{0xE6, 0xF4, 0xEA};
        case python::PluginState::AwaitingApproval: return QColor{0xFE, 0xF7, 0xE0};
        case python::PluginState::Rejected:
        case python::PluginState::Failed:           return QColor{0xFC, 0xE8, 0xE6};
    }
    return {};
}

} // namespace

PluginManagerDialog::PluginManagerDialog(python::PluginRegistry& registry, QWidget* parent)
    : QDialog{parent}
    , m_registry{registry}
{
    setWindowTitle(tr("Driver plugins"));
    setObjectName(QStringLiteral("pluginManagerDialog"));
    resize(760, 520);

    auto* layout = new QVBoxLayout{this};

    auto* warning = new QLabel{tr("Python plugins run with your full user privileges. PeakEmi "
                                  "imports a plugin only after you have approved that exact file, "
                                  "and editing an approved file withdraws the approval."),
                               this};
    warning->setWordWrap(true);
    layout->addWidget(warning);

    if (!python::PluginRegistry::isSupported()) {
        auto* unsupported = new QLabel{tr("This build was configured without embedded Python, so "
                                          "plugins cannot be loaded. Rebuild with "
                                          "-DPEAKEMI_WITH_PYTHON=ON to use them."),
                                       this};
        unsupported->setWordWrap(true);
        layout->addWidget(unsupported);
    }

    m_tree = new QTreeWidget{this};
    m_tree->setObjectName(QStringLiteral("pluginList"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("Plugin"), tr("Status"), tr("Vendor"), tr("File")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    m_detail = new QLabel{this};
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setMinimumHeight(72);
    layout->addWidget(m_detail);

    auto* actions = new QHBoxLayout;
    m_approveButton = new QPushButton{tr("Approve and load"), this};
    m_revokeButton = new QPushButton{tr("Revoke approval"), this};
    auto* rescanButton = new QPushButton{tr("Rescan"), this};
    auto* folderButton = new QPushButton{tr("Open plugin folder"), this};
    actions->addWidget(m_approveButton);
    actions->addWidget(m_revokeButton);
    actions->addWidget(rescanButton);
    actions->addWidget(folderButton);
    actions->addStretch();
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close, this};
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(m_approveButton, &QPushButton::clicked, this, &PluginManagerDialog::approveSelected);
    connect(m_revokeButton, &QPushButton::clicked, this, &PluginManagerDialog::revokeSelected);
    connect(rescanButton, &QPushButton::clicked, this, &PluginManagerDialog::rescan);
    connect(folderButton, &QPushButton::clicked, this, &PluginManagerDialog::openPluginFolder);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &PluginManagerDialog::updateButtons);

    rescan();
}

void PluginManagerDialog::rescan()
{
    m_registry.rescan();
    m_registry.publishToDriverRegistry();

    m_tree->clear();
    for (const auto& plugin : m_registry.plugins()) {
        auto* item = new QTreeWidgetItem{m_tree,
                                         {plugin.displayName(),
                                          stateText(plugin.state),
                                          QString::fromStdString(plugin.manifest.vendor),
                                          QFileInfo{plugin.path}.fileName()}};
        item->setData(0, PathRole, plugin.path);
        item->setToolTip(3, plugin.path);
        const QColor colour = stateColour(plugin.state);
        if (colour.isValid()) {
            for (int column = 0; column < m_tree->columnCount(); ++column) {
                item->setBackground(column, colour);
            }
        }
    }
    if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }
    updateButtons();
}

QString PluginManagerDialog::selectedPath() const
{
    const auto* item = m_tree->currentItem();
    return item == nullptr ? QString{} : item->data(0, PathRole).toString();
}

void PluginManagerDialog::updateButtons()
{
    const auto path = selectedPath();
    const auto* selected = [this, &path]() -> const python::DiscoveredPlugin* {
        for (const auto& plugin : m_registry.plugins()) {
            if (plugin.path == path) {
                return &plugin;
            }
        }
        return nullptr;
    }();

    m_approveButton->setEnabled(selected != nullptr
                                && selected->state != python::PluginState::Loaded);
    m_revokeButton->setEnabled(selected != nullptr
                               && m_registry.trustStore().state(path)
                                      != python::TrustState::Unknown);

    if (selected == nullptr) {
        m_detail->clear();
        return;
    }

    QString detail = selected->manifest.description.empty()
                         ? QString{}
                         : QString::fromStdString(selected->manifest.description) + QLatin1Char('\n');
    if (!selected->manifest.apiVersion.empty()) {
        detail += tr("Plugin API %1").arg(QString::fromStdString(selected->manifest.apiVersion));
    }
    if (!selected->manifest.licence.empty()) {
        detail += tr("  ·  %1").arg(QString::fromStdString(selected->manifest.licence));
    }
    if (!selected->lastError.empty()) {
        detail += QLatin1Char('\n') + QString::fromStdString(selected->lastError);
    }
    m_detail->setText(detail);
}

void PluginManagerDialog::approveSelected()
{
    const auto path = selectedPath();
    if (path.isEmpty()) {
        return;
    }

    const auto answer =
        QMessageBox::question(this,
                              tr("Approve this plugin?"),
                              tr("%1 will be imported and run with your full user privileges "
                                 "every time PeakEmi starts.\n\nOnly approve plugins you trust.")
                                  .arg(QFileInfo{path}.fileName()),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (auto status = m_registry.approveAndLoad(path); !status) {
        QMessageBox::warning(this,
                             tr("Cannot approve the plugin"),
                             QString::fromStdString(status.error().message()));
    }
    rescan();
}

void PluginManagerDialog::revokeSelected()
{
    const auto path = selectedPath();
    if (path.isEmpty()) {
        return;
    }
    if (auto status = m_registry.trustStore().revoke(path); !status) {
        QMessageBox::warning(this,
                             tr("Cannot revoke the approval"),
                             QString::fromStdString(status.error().message()));
        return;
    }
    // The plugin stays loaded until the next start; say so rather than pretend.
    QMessageBox::information(this,
                             tr("Approval revoked"),
                             tr("%1 will not be imported again. A plugin already loaded in this "
                                "session stays loaded until PeakEmi restarts.")
                                 .arg(QFileInfo{path}.fileName()));
    rescan();
}

void PluginManagerDialog::openPluginFolder()
{
    const auto folder = python::PluginRegistry::userPluginDirectory();
    QDir{}.mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

} // namespace peakemi::ui
