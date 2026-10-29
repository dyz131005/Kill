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
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

// A QListWidget that accepts dropped files and reports their local paths.
class PluginDropList : public QListWidget
{
    Q_OBJECT

public:
    explicit PluginDropList(QWidget *parent = nullptr) : QListWidget(parent)
    {
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        setSelectionMode(QAbstractItemView::SingleSelection);
    }

signals:
    void filesDropped(const QStringList &files);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        } else {
            QListWidget::dragEnterEvent(e);
        }
    }

    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        } else {
            QListWidget::dragMoveEvent(e);
        }
    }

    void dropEvent(QDropEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            QStringList files;
            for (const QUrl &url : e->mimeData()->urls()) {
                if (url.isLocalFile()) files.append(url.toLocalFile());
            }
            e->acceptProposedAction();
            if (!files.isEmpty()) emit filesDropped(files);
            return;
        }
        QListWidget::dropEvent(e);
    }
};

QWidget *MainWindow::buildPluginsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *title = new QLabel("插件管理", page);
    title->setStyleSheet("font-size: 18px; font-weight: 600;");
    layout->addWidget(title);

    auto *hint = new QLabel("把插件 DLL 拖到下方列表，或点击「安装插件」选择文件即可安装。危险权限需授权，重启后生效。", page);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #6b7280;");
    layout->addWidget(hint);

    auto *btnRow = new QHBoxLayout();
    auto *installBtn = new QPushButton("安装插件", page);
    auto *refreshBtn = new QPushButton("刷新", page);
    auto *openDirBtn = new QPushButton("打开插件目录", page);
    btnRow->addWidget(installBtn);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(openDirBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto *dropList = new PluginDropList(page);
    m_pluginsList = dropList;
    layout->addWidget(m_pluginsList, 1);
    connect(dropList, &PluginDropList::filesDropped, this, [this](const QStringList &files) {
        for (const QString &f : files) {
            if (f.endsWith(".dll", Qt::CaseInsensitive)) installPluginDll(f);
        }
        refreshPluginsTab();
    });

    auto *opRow = new QHBoxLayout();
    auto *authBtn = new QPushButton("授权", page);
    auto *revokeBtn = new QPushButton("取消授权", page);
    opRow->addWidget(authBtn);
    opRow->addWidget(revokeBtn);
    opRow->addStretch();
    layout->addLayout(opRow);

    connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshPluginsTab(); });
    connect(openDirBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = m_core.configStore()->configDir() + "/plugins";
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    // Install plugin(s) from a file picker.
    connect(installBtn, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(this, "选择插件 DLL", QString(), "插件 (*.dll)");
        if (files.isEmpty()) return;
        for (const QString &f : files) installPluginDll(f);
        refreshPluginsTab();
    });

    // Drag & drop is handled by PluginDropList itself.
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

void MainWindow::installPluginDll(const QString &sourcePath)
{
    if (sourcePath.isEmpty()) return;
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists() || !srcInfo.isFile()) {
        QMessageBox::warning(this, "安装插件", QString("插件文件不存在: %1").arg(sourcePath));
        return;
    }

    const QString pluginsDir = m_core.configStore()->configDir() + "/plugins";
    QDir().mkpath(pluginsDir);
    const QString destPath = pluginsDir + "/" + srcInfo.fileName();

    // Overwrite prompt if a same-named plugin already exists.
    if (QFileInfo::exists(destPath)) {
        const auto ret = QMessageBox::question(this, "覆盖插件",
            QString("插件目录中已存在同名文件：\n%1\n\n是否覆盖？").arg(srcInfo.fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            appendLog(QString("取消安装插件（已存在同名）: %1").arg(srcInfo.fileName()));
            return;
        }
        QFile::remove(destPath);
    }

    if (!QFile::copy(sourcePath, destPath)) {
        QMessageBox::warning(this, "安装插件", QString("安装插件失败，无法复制到: %1").arg(destPath));
        return;
    }

    appendLog(QString("已安装插件: %1 -> %2（重启后生效）").arg(srcInfo.fileName(), pluginsDir));
    // Rescan so the list updates immediately.
    if (m_pluginManager) {
        m_pluginManager->scan(pluginsDir);
        const auto entries = m_pluginManager->entries();
        for (const PluginEntry &e : entries) {
            if (e.forbidden) continue;
            const bool needsConfirm = (e.permissions & (KILL_PERM_WRITE_FILE | KILL_PERM_DELETE_FILE | KILL_PERM_RUN_PROCESS | KILL_PERM_NETWORK | KILL_PERM_WRITE_CONFIG)) != 0;
            if (needsConfirm && !m_pluginManager->isAuthorized(e.id)) {
                const auto ret = QMessageBox::warning(this, "插件授权请求",
                    QString("新插件「%1」申请危险权限：%2是否授权？").arg(e.name, PluginManager::permissionSummary(e.permissions)),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (ret == QMessageBox::Yes) m_pluginManager->setAuthorized(e.id, true);
            }
        }
    }
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

#include "plugins_tab.moc"
