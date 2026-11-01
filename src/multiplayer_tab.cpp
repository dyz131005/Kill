// 联机标签页 —— 陶瓦联机（Terracotta）UI + Win7 兼容性守卫。
#include "main_window.h"
#include "terracotta_client.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QStackedWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool MainWindow::isWindows7OrBelow()
{
#ifdef Q_OS_WIN
    typedef LONG(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(
            reinterpret_cast<void *>(GetProcAddress(hMod, "RtlGetVersion")));
        if (fn) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0) {
                if (info.dwMajorVersion < 6) return true;
                if (info.dwMajorVersion == 6 && info.dwMinorVersion <= 1) return true;
                return false;
            }
        }
    }
#endif
    return false;
}

QWidget *MainWindow::buildMultiplayerTab()
{
    auto *page = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Left nav (预留将来更多联机方式)
    auto *navWidget = new QWidget(page);
    navWidget->setFixedWidth(180);
    navWidget->setObjectName("multiplayerNav");
    navWidget->setStyleSheet(
        "#multiplayerNav { background-color: #f9fafb; border-right: 1px solid #e5e7eb; }");
    auto *navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(16, 20, 16, 20);
    navLayout->setSpacing(6);
    auto *navTitle = new QLabel("联机", navWidget);
    navTitle->setStyleSheet("font-size: 16px; font-weight: 700; margin-bottom: 8px;");
    navLayout->addWidget(navTitle);
    auto *tcBtn = new QPushButton("🌐  陶瓦联机", navWidget);
    tcBtn->setCheckable(true);
    tcBtn->setChecked(true);
    tcBtn->setFixedHeight(44);
    tcBtn->setCursor(Qt::PointingHandCursor);
    tcBtn->setStyleSheet(
        "QPushButton { text-align: left; padding-left: 14px; background: transparent;"
        "  border: none; border-radius: 8px; font-size: 14px; font-weight: 600; color: #2563eb;"
        "  background-color: rgba(37,99,235,0.10); }");
    navLayout->addWidget(tcBtn);
    navLayout->addStretch();

    // Right content: a stacked widget with install page / main page
    auto *right = new QWidget(page);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(24, 20, 24, 20);
    rightLayout->setSpacing(16);

    // Header row: title + refresh
    auto *headerRow = new QHBoxLayout();
    auto *title = new QLabel("陶瓦联机 (Terracotta)", right);
    title->setStyleSheet("font-size: 20px; font-weight: 800; color: #111827;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    auto *refreshBtn = new QPushButton("刷新", right);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setFixedHeight(34);
    refreshBtn->setStyleSheet(
        "QPushButton { background: #ffffff; color: #374151; border: 1px solid #d1d5db;"
        "  border-radius: 8px; padding: 6px 18px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: #f3f4f6; }");
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        appendLog("联机：刷新安装状态");
        refreshMultiplayerInstallState();
    });
    headerRow->addWidget(refreshBtn);
    rightLayout->addLayout(headerRow);

    m_mpStack = new QStackedWidget(right);
    rightLayout->addWidget(m_mpStack, 1);

    // --- Install page ---
    m_mpInstallPage = new QWidget(m_mpStack);
    auto *instLayout = new QVBoxLayout(m_mpInstallPage);
    instLayout->setAlignment(Qt::AlignCenter);
    auto *instCard = new QFrame(m_mpInstallPage);
    instCard->setObjectName("mpInstallCard");
    instCard->setStyleSheet("#mpInstallCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    instCard->setFixedWidth(520);
    auto *instCardLayout = new QVBoxLayout(instCard);
    instCardLayout->setContentsMargins(28, 24, 28, 24);
    instCardLayout->setSpacing(16);
    auto *instTitle = new QLabel("未安装联机核心", instCard);
    instTitle->setStyleSheet("font-size: 18px; font-weight: 800; color: #111827;");
    instCardLayout->addWidget(instTitle);
    auto *instDesc = new QLabel(
        "检测到还没有陶瓦联机核心 (terracotta.exe)。\n是否现在安装？安装后会释放到启动器配置目录下的 terracotta 文件夹。",
        instCard);
    instDesc->setWordWrap(true);
    instDesc->setStyleSheet("font-size: 13px; color: #4b5563;");
    instCardLayout->addWidget(instDesc);
    auto *instBtnRow = new QHBoxLayout();
    instBtnRow->addStretch();
    auto *installYesBtn = new QPushButton("是，安装", instCard);
    installYesBtn->setCursor(Qt::PointingHandCursor);
    installYesBtn->setFixedSize(120, 38);
    installYesBtn->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 8px;"
        "  font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }");
    connect(installYesBtn, &QPushButton::clicked, this, &MainWindow::installTerracottaCore);
    instBtnRow->addWidget(installYesBtn);
    instCardLayout->addLayout(instBtnRow);
    instLayout->addWidget(instCard);
    m_mpStack->addWidget(m_mpInstallPage);

    // --- Main page ---
    m_mpMainPage = new QWidget(m_mpStack);
    auto *mainLayout = new QVBoxLayout(m_mpMainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(16);

    // Status card
    auto *statusCard = new QFrame(m_mpMainPage);
    statusCard->setObjectName("mpStatusCard");
    statusCard->setStyleSheet("#mpStatusCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    auto *statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(22, 18, 22, 18);
    auto *statusTitle = new QLabel("当前状态", statusCard);
    statusTitle->setStyleSheet("font-size: 13px; color: #6b7280;");
    statusLayout->addWidget(statusTitle);
    m_mpStatusLabel = new QLabel("未连接", statusCard);
    m_mpStatusLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #111827;");
    m_mpStatusLabel->setWordWrap(true);
    statusLayout->addWidget(m_mpStatusLabel);
    m_mpRoomCodeLabel = new QLabel(QString(), statusCard);
    m_mpRoomCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_mpRoomCodeLabel->setStyleSheet("font-size: 15px; font-weight: 700; color: #2563eb;");
    statusLayout->addWidget(m_mpRoomCodeLabel);
    m_mpJoinUrlLabel = new QLabel(QString(), statusCard);
    m_mpJoinUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_mpJoinUrlLabel->setStyleSheet("font-size: 14px; color: #059669;");
    statusLayout->addWidget(m_mpJoinUrlLabel);
    mainLayout->addWidget(statusCard);

    // Host card
    auto *hostCard = new QFrame(m_mpMainPage);
    hostCard->setObjectName("mpHostCard");
    hostCard->setStyleSheet("#mpHostCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    auto *hostLayout = new QVBoxLayout(hostCard);
    hostLayout->setContentsMargins(22, 18, 22, 18);
    hostLayout->setSpacing(10);
    auto *hostTitle = new QLabel("创建房间（房主）", hostCard);
    hostTitle->setStyleSheet("font-size: 15px; font-weight: 700; color: #111827;");
    hostLayout->addWidget(hostTitle);
    auto *hostDesc = new QLabel("点击后自动开房，生成房间码发给朋友即可联机。", hostCard);
    hostDesc->setStyleSheet("font-size: 12px; color: #6b7280;");
    hostLayout->addWidget(hostDesc);
    m_mpHostButton = new QPushButton("创建房间", hostCard);
    m_mpHostButton->setCursor(Qt::PointingHandCursor);
    m_mpHostButton->setFixedHeight(38);
    m_mpHostButton->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 8px;"
        "  padding: 6px 22px; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }");
    hostLayout->addWidget(m_mpHostButton, 0, Qt::AlignLeft);
    mainLayout->addWidget(hostCard);

    // Join card
    auto *joinCard = new QFrame(m_mpMainPage);
    joinCard->setObjectName("mpJoinCard");
    joinCard->setStyleSheet("#mpJoinCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    auto *joinLayout = new QVBoxLayout(joinCard);
    joinLayout->setContentsMargins(22, 18, 22, 18);
    joinLayout->setSpacing(10);
    auto *joinTitle = new QLabel("加入房间（房客）", joinCard);
    joinTitle->setStyleSheet("font-size: 15px; font-weight: 700; color: #111827;");
    joinLayout->addWidget(joinTitle);
    auto *joinRow = new QHBoxLayout();
    m_mpRoomInput = new QLineEdit(joinCard);
    m_mpRoomInput->setPlaceholderText("输入房主给你的房间码");
    m_mpRoomInput->setStyleSheet(
        "QLineEdit { padding: 8px 12px; border: 1px solid #d1d5db; border-radius: 8px;"
        "  background: #f9fafb; font-size: 13px; }");
    joinRow->addWidget(m_mpRoomInput, 1);
    m_mpJoinButton = new QPushButton("加入", joinCard);
    m_mpJoinButton->setCursor(Qt::PointingHandCursor);
    m_mpJoinButton->setFixedSize(96, 36);
    m_mpJoinButton->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 8px;"
        "  font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }");
    joinRow->addWidget(m_mpJoinButton);
    joinLayout->addLayout(joinRow);
    mainLayout->addWidget(joinCard);

    mainLayout->addStretch();
    m_mpStack->addWidget(m_mpMainPage);

    rootLayout->addWidget(navWidget);
    rootLayout->addWidget(right, 1);

    // Wire host / join buttons.
    // IMPORTANT: start() is asynchronous (spawns the core and polls for the port).
    // host()/guest() are no-ops until the client is ready, so we must defer the
    // actual action until the ready() signal fires for this specific click.
    connect(m_mpHostButton, &QPushButton::clicked, this, [this]() {
        if (!m_terracotta) return;
        appendLog("联机：创建房间（房主）");
        if (m_mpStatusLabel) m_mpStatusLabel->setText("正在启动联机核心...");
        auto doHost = [this]() {
            m_terracotta->setWaiting();
            m_terracotta->host();
            if (m_mpStatusLabel) m_mpStatusLabel->setText("正在创建房间...");
        };
        if (m_terracotta->isReady()) {
            doHost();
        } else {
            auto *conn = new QMetaObject::Connection();
            *conn = connect(m_terracotta, &TerracottaClient::ready, this, [this, doHost, conn]() {
                disconnect(*conn);
                delete conn;
                doHost();
            });
            auto *failConn = new QMetaObject::Connection();
            *failConn = connect(m_terracotta, &TerracottaClient::failed, this,
                                [failConn](const QString &) {
                disconnect(*failConn);
                delete failConn;
            });
            m_terracotta->start(QStringLiteral("Player"));
        }
    });
    connect(m_mpJoinButton, &QPushButton::clicked, this, [this]() {
        if (!m_terracotta) return;
        const QString room = m_mpRoomInput ? m_mpRoomInput->text().trimmed() : QString();
        if (room.isEmpty()) {
            QMessageBox::warning(this, "联机", "请输入房间码");
            return;
        }
        appendLog(QString("联机：加入房间 %1").arg(room));
        if (m_mpStatusLabel) m_mpStatusLabel->setText("正在启动联机核心...");
        auto doJoin = [this, room]() {
            m_terracotta->guest(room);
            if (m_mpStatusLabel) m_mpStatusLabel->setText("正在连接房主...");
        };
        if (m_terracotta->isReady()) {
            doJoin();
        } else {
            auto *conn = new QMetaObject::Connection();
            *conn = connect(m_terracotta, &TerracottaClient::ready, this, [this, doJoin, conn]() {
                disconnect(*conn);
                delete conn;
                doJoin();
            });
            auto *failConn = new QMetaObject::Connection();
            *failConn = connect(m_terracotta, &TerracottaClient::failed, this,
                                [failConn](const QString &) {
                disconnect(*failConn);
                delete failConn;
            });
            m_terracotta->start(QStringLiteral("Player"));
        }
    });

    // Initial install check happens after the page is shown.
    refreshMultiplayerInstallState();

    return page;
}

QString MainWindow::terracottaDir() const
{
    return m_core.configStore()->configDir() + "/terracotta";
}

void MainWindow::refreshMultiplayerInstallState()
{
    if (!m_mpStack) return;
    const QString exe = terracottaDir() + "/terracotta.exe";
    const bool installed = QFileInfo::exists(exe);
    if (!installed) {
        m_mpStack->setCurrentWidget(m_mpInstallPage);
        return;
    }
    m_mpStack->setCurrentWidget(m_mpMainPage);

    // Lazily create the Terracotta client and wire signals once.
    if (!m_terracotta) {
        m_terracotta = new TerracottaClient(this);
        m_terracotta->setExecutable(exe);
        connect(m_terracotta, &TerracottaClient::stateChanged, this, &MainWindow::onTerracottaState);
        connect(m_terracotta, &TerracottaClient::failed, this, [this](const QString &err) {
            if (m_mpStatusLabel) m_mpStatusLabel->setText(QString("联机核心错误: %1").arg(err));
            appendLog(QString("联机核心错误: %1").arg(err));
        });
        connect(m_terracotta, &TerracottaClient::ready, this, [this]() {
            if (m_mpStatusLabel) m_mpStatusLabel->setText("已就绪，可创建或加入房间");
        });
        connect(m_terracotta, &TerracottaClient::logLine, this, [this](const QString &line) {
            appendLog(line);
        });
        m_terracotta->ensureNodes();
    } else {
        m_terracotta->setExecutable(exe);
    }
}

void MainWindow::installTerracottaCore()
{
    const QString dir = terracottaDir();
    QDir().mkpath(dir);
    const QString dest = dir + "/terracotta.exe";

    QFile res(":/terracotta/terracotta.exe");
    if (!res.exists() || !res.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "联机", "内置联机核心资源缺失，无法安装。");
        return;
    }
    QFile out(dest);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        res.close();
        QMessageBox::warning(this, "联机", QString("无法写入联机核心: %1").arg(dest));
        return;
    }
    const QByteArray data = res.readAll();
    res.close();
    out.write(data);
    out.close();

    appendLog(QString("联机核心已释放到: %1 (%2 字节)").arg(dest).arg(data.size()));
    refreshMultiplayerInstallState();
}

void MainWindow::onTerracottaState(const QJsonObject &state)
{
    if (!m_mpStatusLabel) return;
    const QString s = state.value("state").toString();
    QString text;
    if (s == "waiting") text = "等待中...";
    else if (s == "host-scanning") text = "正在扫描节点...";
    else if (s == "host-starting") text = "正在创建房间...";
    else if (s == "host-ok") text = "房间已创建，等待朋友加入";
    else if (s == "guest-connecting") text = "正在连接房主...";
    else if (s == "guest-starting") text = "正在建立联机通道...";
    else if (s == "guest-ok") text = "已加入房间";
    else if (s == "exception") {
        // Terracotta exception.type: 0 PING_HOST_FAIL, 1 PING_HOST_RST,
        // 2 GUEST_ET_CRASH, 3 HOST_ET_CRASH, 4 PING_SERVER_RST,
        // 5 SCAFFOLDING_INVALID_RESPONSE
        const int t = state.value("type").toInt(-1);
        static const char *names[] = {
            "无法连接房主 (PING_HOST_FAIL)",
            "房主连接被重置 (PING_HOST_RST)",
            "房客组网崩溃 (GUEST_ET_CRASH)",
            "房主组网崩溃 (HOST_ET_CRASH)",
            "中继服务器连接被重置 (PING_SERVER_RST)",
            "联机协议响应无效 (SCAFFOLDING_INVALID_RESPONSE)"
        };
        text = (t >= 0 && t < 6) ? QString::fromUtf8(names[t])
                                 : QStringLiteral("联机出错 (type=%1)").arg(t);
        appendLog(QString("[联机] 异常: %1, 原始: %2")
                      .arg(text, QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact))));
    }
    else text = s.isEmpty() ? QStringLiteral("未知状态") : s;
    m_mpStatusLabel->setText(text);
    // 若长时间停在 waiting，提示可能未运行游戏进程。
    if (s == "waiting") {
        m_mpStatusLabel->setText("等待中...（请先启动一次 Minecraft 或在游戏中开房；若长时间无进展，请确认游戏进程已运行）");
    }
    if (m_mpRoomCodeLabel) {
        const QString room = state.value("room").toString();
        m_mpRoomCodeLabel->setText(room.isEmpty() ? QString() : QStringLiteral("房间码: %1").arg(room));
    }
    if (m_mpJoinUrlLabel) {
        const QString url = state.value("url").toString();
        m_mpJoinUrlLabel->setText(url.isEmpty() ? QString() : QStringLiteral("服务器地址: %1").arg(url));
    }
}

void MainWindow::applyWin7MultiplayerGuard()
{
    if (!m_multiplayerTab || !m_mainTabWidget) return;
    if (m_multiplayerTabIndex < 0) return;
    if (!isWindows7OrBelow()) return;
    if (m_core.configStore()->config().forceEnableMultiplayerOnWin7) return;

    auto *overlay = new QWidget(m_multiplayerTab);
    overlay->setObjectName("win7MultiplayerOverlay");
    overlay->setStyleSheet("#win7MultiplayerOverlay { background-color: rgba(15,23,42,0.55); }");
    overlay->setGeometry(m_multiplayerTab->rect());

    auto *ovLayout = new QVBoxLayout(overlay);
    ovLayout->setAlignment(Qt::AlignCenter);
    auto *panel = new QFrame(overlay);
    panel->setObjectName("win7MpPanel");
    panel->setStyleSheet("#win7MpPanel { background: #ffffff; border-radius: 16px; border: 1px solid #e5e7eb; }");
    panel->setFixedWidth(520);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(28, 24, 28, 22);
    panelLayout->setSpacing(16);
    auto *title = new QLabel("联机可能不兼容当前系统", panel);
    title->setStyleSheet("font-size: 18px; font-weight: 800; color: #dc2626;");
    panelLayout->addWidget(title);
    auto *desc = new QLabel(
        "检测到当前系统为 Windows 7 或更低版本。陶瓦联机在较新的系统上体验更佳，"
        "在旧系统上可能存在兼容性问题。\n\n是否仍要继续使用联机功能？", panel);
    desc->setWordWrap(true);
    desc->setStyleSheet("font-size: 13px; color: #374151;");
    panelLayout->addWidget(desc);
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *noBtn = new QPushButton("否", panel);
    noBtn->setCursor(Qt::PointingHandCursor);
    noBtn->setFixedSize(96, 36);
    noBtn->setStyleSheet("QPushButton { background: #f3f4f6; color: #374151; border: 1px solid #d1d5db; border-radius: 8px; font-size: 14px; font-weight: 600; } QPushButton:hover { background: #e5e7eb; }");
    auto *yesBtn = new QPushButton("是", panel);
    yesBtn->setCursor(Qt::PointingHandCursor);
    yesBtn->setFixedSize(96, 36);
    yesBtn->setStyleSheet("QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 8px; font-size: 14px; font-weight: 700; } QPushButton:hover { background: #1d4ed8; }");
    btnRow->addWidget(noBtn);
    btnRow->addWidget(yesBtn);
    panelLayout->addLayout(btnRow);
    ovLayout->addWidget(panel);
    overlay->hide();

    connect(m_mainTabWidget, &QTabWidget::currentChanged, this, [this, overlay](int index) {
        if (index == m_multiplayerTabIndex) {
            overlay->setGeometry(m_multiplayerTab->rect());
            overlay->raise();
            overlay->show();
        } else {
            overlay->hide();
        }
    });
    connect(yesBtn, &QPushButton::clicked, this, [overlay]() { overlay->hide(); });
    connect(noBtn, &QPushButton::clicked, this, [this, overlay]() {
        overlay->hide();
        m_multiplayerDisabled = true;
        if (m_mainTabWidget) {
            if (m_multiplayerTabIndex >= 0 && m_multiplayerTabIndex < m_mainTabWidget->count())
                m_mainTabWidget->setTabEnabled(m_multiplayerTabIndex, false);
            m_mainTabWidget->setCurrentIndex(0);
        }
        appendLog("联机：用户选择在 Win7 上跳过联机功能（本会话已禁用联机标签页）。");
    });
}
