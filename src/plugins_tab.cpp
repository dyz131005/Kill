// 插件管理页 —— MainWindow 的插件标签页实现
#include "main_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QDir>
#include <QUrl>

QWidget *MainWindow::buildPluginsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *title = new QLabel("插件管理", page);
    title->setStyleSheet("font-size: 18px; font-weight: 600;");
    layout->addWidget(title);

    auto *hint = new QLabel("把插件 DLL 放到 exe 同目录的 plugins/ 下，重启生效。危险权限需授权。", page);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #6b7280;");
    layout->addWidget(hint);

    auto *btnRow = new QHBoxLayout();
    auto *refreshBtn = new QPushButton("刷新", page);
    auto *openDirBtn = new QPushButton("打开插件目录", page);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(openDirBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    m_pluginsList = new QListWidget(page);
    layout->addWidget(m_pluginsList, 1);

    auto *opRow = new QHBoxLayout();
    auto *authBtn = new QPushButton("授权", page);
    auto *revokeBtn = new QPushButton("取消授权", page);
    opRow->addWidget(authBtn);
    opRow->addWidget(revokeBtn);
    opRow->addStretch();
    layout->addLayout(opRow);

    connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshPluginsTab(); });
    connect(openDirBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QCoreApplication::applicationDirPath() + "/plugins";
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(authBtn, &QPushButton::clicked, this, [this]() {
        if (!m_pluginManager || !m_pluginsList) return;
        auto *item = m_pluginsList->currentItem();
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        m_pluginManager->setAuthorized(id, true);
        appendLog(QString("已授权插件: %1（重启后生效）").arg(id));
        refreshPluginsTab();
    });
    connect(revokeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_pluginManager || !m_pluginsList) return;
        auto *item = m_pluginsList->currentItem();
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        m_pluginManager->setAuthorized(id, false);
        appendLog(QString("已取消授权插件: %1（重启后生效）").arg(id));
        refreshPluginsTab();
    });

    refreshPluginsTab();
    return page;
}

void MainWindow::refreshPluginsTab()
{
    if (!m_pluginsList) return;
    m_pluginsList->clear();
    if (!m_pluginManager) return;
    const auto entries = m_pluginManager->entries();
    if (entries.isEmpty()) {
        auto *item = new QListWidgetItem("（未发现插件）", m_pluginsList);
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    const quint64 confirmMask = KILL_PERM_WRITE_FILE | KILL_PERM_DELETE_FILE |
                                KILL_PERM_RUN_PROCESS | KILL_PERM_NETWORK | KILL_PERM_WRITE_CONFIG;
    for (const PluginEntry &e : entries) {
        QString status;
        if (e.forbidden) status = "[禁止] 权限被拒绝";
        else if (!e.loadError.isEmpty()) status = QString("[警告] %1").arg(e.loadError);
        else if (e.loaded) status = "[已加载]";
        else if ((e.permissions & confirmMask) && !e.authorized) status = "[待授权]";
        else status = "[已启用]";
        QString text = e.name + "  v" + e.version;
        text += QChar(10);
        text += "作者: " + (e.author.isEmpty() ? QString("-") : e.author) + "   " + status;
        text += QChar(10);
        text += "权限: " + PluginManager::permissionSummary(e.permissions);
        auto *item = new QListWidgetItem(text, m_pluginsList);
        item->setData(Qt::UserRole, e.id);
    }
}
