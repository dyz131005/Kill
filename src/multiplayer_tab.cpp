// 联机标签页 —— 陶瓦联机（Terracotta）UI + Win7 兼容性守卫。
#include "main_window.h"
#include "terracotta_client.h"
#include "hongshi_client.h"

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
#include <QGuiApplication>
#include <QClipboard>

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
    auto *hsBtn = new QPushButton("🧱  红石联机", navWidget);
    hsBtn->setCheckable(true);
    hsBtn->setFixedHeight(44);
    hsBtn->setCursor(Qt::PointingHandCursor);
    hsBtn->setStyleSheet(
        "QPushButton { text-align: left; padding-left: 14px; background: transparent;"
        "  border: none; border-radius: 8px; font-size: 14px; font-weight: 600; color: #374151; }"
        "QPushButton:hover { background-color: #f3f4f6; }"
        "QPushButton:checked { color: #2563eb; background-color: rgba(37,99,235,0.10); }");
    navLayout->addWidget(hsBtn);
    navLayout->addStretch();

    // Right content: a stacked widget with install page / main page
    auto *right = new QWidget(page);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Outer stack: page 0 = Terracotta, page 1 = Hongshi.
    auto *outerStack = new QStackedWidget(right);
    m_mpOuterStack = outerStack;
    rightLayout->addWidget(outerStack, 1);

    auto *tcPage = new QWidget(outerStack);
    auto *tcPageLayout = new QVBoxLayout(tcPage);
    tcPageLayout->setContentsMargins(24, 20, 24, 20);
    tcPageLayout->setSpacing(16);

    // Header row: title + refresh
    auto *headerRow = new QHBoxLayout();
    auto *title = new QLabel("陶瓦联机 (Terracotta)", tcPage);
    title->setStyleSheet("font-size: 20px; font-weight: 800; color: #111827;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    auto *refreshBtn = new QPushButton("刷新", tcPage);
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
    tcPageLayout->addLayout(headerRow);
    m_mpStack = new QStackedWidget(tcPage);
    tcPageLayout->addWidget(m_mpStack, 1);

    outerStack->addWidget(tcPage);
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

    // ===== Hongshi (红石联机) page =====
    QWidget *hsPage = buildHongshiPage(outerStack);
    outerStack->addWidget(hsPage);

    // Nav switching between the two multiplayer backends.
    auto applyNavStyle = [](QPushButton *btn, bool active) {
        btn->setStyleSheet(
            QStringLiteral("QPushButton { text-align: left; padding-left: 14px; background: transparent;"
                           "  border: none; border-radius: 8px; font-size: 14px; font-weight: 600;"
                           "  color: %1; background-color: %2; }")
                .arg(active ? QStringLiteral("#2563eb") : QStringLiteral("#374151"),
                     active ? QStringLiteral("rgba(37,99,235,0.10)") : QStringLiteral("transparent")));
    };
    connect(tcBtn, &QPushButton::clicked, this, [this, outerStack, tcBtn, hsBtn, applyNavStyle]() {
        animateButton(tcBtn);
        animateStackSwitch(outerStack, 0);
        tcBtn->setChecked(true);
        hsBtn->setChecked(false);
        applyNavStyle(tcBtn, true);
        applyNavStyle(hsBtn, false);
    });
    connect(hsBtn, &QPushButton::clicked, this, [this, outerStack, tcBtn, hsBtn, applyNavStyle]() {
        animateStackSwitch(outerStack, 1);
        animateButton(hsBtn);
        tcBtn->setChecked(false);
        hsBtn->setChecked(true);
        applyNavStyle(tcBtn, false);
        applyNavStyle(hsBtn, true);
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

    // The core depends on the MSVC 2015+ runtime. Ship VCRUNTIME140.dll next to
    // terracotta.exe so it also works on machines without the VC++ redistributable.
    const QList<QPair<QString, QString>> files = {
        { QStringLiteral(":/terracotta/terracotta.exe"), QStringLiteral("terracotta.exe") },
        { QStringLiteral(":/terracotta/VCRUNTIME140.DLL"), QStringLiteral("VCRUNTIME140.dll") },
    };

    for (const auto &entry : files) {
        QFile res(entry.first);
        if (!res.exists() || !res.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "联机",
                QString("内置联机资源缺失: %1，无法安装。").arg(entry.first));
            return;
        }
        const QString dest = dir + "/" + entry.second;
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            res.close();
            QMessageBox::warning(this, "联机", QString("无法写入联机文件: %1").arg(dest));
            return;
        }
        const QByteArray data = res.readAll();
        res.close();
        out.write(data);
        out.close();
        appendLog(QString("联机文件已释放到: %1 (%2 字节)").arg(dest).arg(data.size()));
    }

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

// ============================================================================
// 红石联机 (Hongshi) page
// ============================================================================

QString MainWindow::hongshiDir() const
{
    return HongshiClient::defaultDir(m_core.configStore()->configDir());
}

QWidget *MainWindow::buildHongshiPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto *headerRow = new QHBoxLayout();
    auto *title = new QLabel("红石联机 (Hongshi)", page);
    title->setStyleSheet("font-size: 20px; font-weight: 800; color: #111827;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    layout->addLayout(headerRow);

    m_hsStack = new QStackedWidget(page);
    layout->addWidget(m_hsStack, 1);

    // --- Install page ---
    m_hsInstallPage = new QWidget(m_hsStack);
    auto *instLayout = new QVBoxLayout(m_hsInstallPage);
    instLayout->setAlignment(Qt::AlignCenter);
    auto *instCard = new QFrame(m_hsInstallPage);
    instCard->setObjectName("hsInstallCard");
    instCard->setStyleSheet("#hsInstallCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    instCard->setFixedWidth(560);
    auto *cardLayout = new QVBoxLayout(instCard);
    cardLayout->setContentsMargins(28, 24, 28, 24);
    cardLayout->setSpacing(16);
    auto *instTitle = new QLabel("未安装红石联机内核", instCard);
    instTitle->setStyleSheet("font-size: 18px; font-weight: 800; color: #111827;");
    cardLayout->addWidget(instTitle);
    auto *instDesc = new QLabel(
        "红石联机内核 (hongshic) 需要从官方服务器下载，约 1 MB。\n"
        "下载后会保存到启动器配置目录下的 hongshi 文件夹。",
        instCard);
    instDesc->setWordWrap(true);
    instDesc->setStyleSheet("font-size: 13px; color: #4b5563;");
    cardLayout->addWidget(instDesc);

    auto *progressLabel = new QLabel(QString(), instCard);
    progressLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
    cardLayout->addWidget(progressLabel);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *installBtn = new QPushButton("下载并安装", instCard);
    installBtn->setCursor(Qt::PointingHandCursor);
    installBtn->setFixedSize(140, 38);
    installBtn->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 8px;"
        "  font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:disabled { background: #9ca3af; }");
    btnRow->addWidget(installBtn);
    cardLayout->addLayout(btnRow);
    instLayout->addWidget(instCard);
    m_hsStack->addWidget(m_hsInstallPage);

    connect(installBtn, &QPushButton::clicked, this, [this, installBtn, progressLabel]() {
        installBtn->setEnabled(false);
        progressLabel->setText("正在下载...");
        if (!m_hongshi) {
            m_hongshi = new HongshiClient(this);
            connect(m_hongshi, &HongshiClient::logLine, this, [this](const QString &line) {
                appendLog(line);
            });
        }
        auto *dlConn = new QMetaObject::Connection();
        *dlConn = connect(m_hongshi, &HongshiClient::downloadProgress, this,
                          [progressLabel](qint64 rec, qint64 total) {
            if (total > 0) {
                progressLabel->setText(QString("正在下载... %1 / %2 KB")
                    .arg(rec / 1024).arg(total / 1024));
            } else {
                progressLabel->setText(QString("正在下载... %1 KB").arg(rec / 1024));
            }
        });
        auto *finConn = new QMetaObject::Connection();
        *finConn = connect(m_hongshi, &HongshiClient::downloadFinished, this,
                           [this, installBtn, progressLabel, dlConn, finConn](const QString &) {
            disconnect(*dlConn); delete dlConn;
            disconnect(*finConn); delete finConn;
            installBtn->setEnabled(true);
            progressLabel->setText(QString());
            refreshHongshiInstallState();
        });
        auto *failConn = new QMetaObject::Connection();
        *failConn = connect(m_hongshi, &HongshiClient::failed, this,
                            [installBtn, progressLabel, dlConn, failConn](const QString &err) {
            disconnect(*dlConn); delete dlConn;
            disconnect(*failConn); delete failConn;
            installBtn->setEnabled(true);
            progressLabel->setText(QString("下载失败: %1").arg(err));
        });
        m_hongshi->ensureKernel(hongshiDir());
    });

    // --- Main page ---
    m_hsMainPage = new QWidget(m_hsStack);
    auto *mainLayout = new QVBoxLayout(m_hsMainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(16);

    auto *statusCard = new QFrame(m_hsMainPage);
    statusCard->setObjectName("hsStatusCard");
    statusCard->setStyleSheet("#hsStatusCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    auto *statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(22, 18, 22, 18);
    statusLayout->setSpacing(8);
    auto *statusTitle = new QLabel("当前状态", statusCard);
    statusTitle->setStyleSheet("font-size: 13px; color: #6b7280;");
    statusLayout->addWidget(statusTitle);
    m_hsStatusLabel = new QLabel("未启动", statusCard);
    m_hsStatusLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #111827;");
    m_hsStatusLabel->setWordWrap(true);
    statusLayout->addWidget(m_hsStatusLabel);
    m_hsRelayLabel = new QLabel(QString(), statusCard);
    m_hsRelayLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
    statusLayout->addWidget(m_hsRelayLabel);
    mainLayout->addWidget(statusCard);

    auto *endpointCard = new QFrame(m_hsMainPage);
    endpointCard->setObjectName("hsEndpointCard");
    endpointCard->setStyleSheet("#hsEndpointCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px; }");
    auto *epLayout = new QVBoxLayout(endpointCard);
    epLayout->setContentsMargins(22, 18, 22, 18);
    epLayout->setSpacing(10);
    auto *epTitle = new QLabel("联机地址", endpointCard);
    epTitle->setStyleSheet("font-size: 15px; font-weight: 700; color: #111827;");
    epLayout->addWidget(epTitle);
    auto *epDesc = new QLabel("把下面这个地址发给朋友，让他们在游戏的「直接连接」里输入即可。", endpointCard);
    epDesc->setWordWrap(true);
    epDesc->setStyleSheet("font-size: 12px; color: #6b7280;");
    epLayout->addWidget(epDesc);

    auto *epRow = new QHBoxLayout();
    m_hsEndpointLabel = new QLabel(QStringLiteral("（尚未启动）"), endpointCard);
    m_hsEndpointLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_hsEndpointLabel->setStyleSheet("font-size: 16px; font-weight: 700; color: #059669;");
    epRow->addWidget(m_hsEndpointLabel, 1);
    m_hsCopyButton = new QPushButton("复制", endpointCard);
    m_hsCopyButton->setCursor(Qt::PointingHandCursor);
    m_hsCopyButton->setFixedSize(80, 34);
    m_hsCopyButton->setEnabled(false);
    m_hsCopyButton->setStyleSheet(
        "QPushButton { background: #f3f4f6; color: #374151; border: 1px solid #d1d5db;"
        "  border-radius: 8px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: #e5e7eb; }"
        "QPushButton:disabled { color: #9ca3af; }");
    epRow->addWidget(m_hsCopyButton);
    epLayout->addLayout(epRow);
    mainLayout->addWidget(endpointCard);

    auto *actionRow = new QHBoxLayout();
    // Game port input (defaults to 25565 when left empty).
    auto *portRow = new QHBoxLayout();
    auto *portLabel = new QLabel("游戏端口：", m_hsMainPage);
    portLabel->setStyleSheet("font-size: 13px; color: #374151;");
    portRow->addWidget(portLabel);
    m_hsPortEdit = new QLineEdit(m_hsMainPage);
    m_hsPortEdit->setPlaceholderText("25565");
    m_hsPortEdit->setFixedWidth(120);
    m_hsPortEdit->setStyleSheet(
        "QLineEdit { padding: 6px 10px; border: 1px solid #d1d5db; border-radius: 8px;"
        "  background: #f9fafb; font-size: 13px; }");
    portRow->addWidget(m_hsPortEdit);
    auto *portHint = new QLabel("留空则默认 25565", m_hsMainPage);
    portHint->setStyleSheet("font-size: 12px; color: #9ca3af;");
    portRow->addWidget(portHint);
    portRow->addStretch();
    mainLayout->addLayout(portRow);

    m_hsStartButton = new QPushButton("创建联机房间", m_hsMainPage);
    m_hsStartButton->setCursor(Qt::PointingHandCursor);
    m_hsStartButton->setFixedHeight(40);
    m_hsStartButton->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 8px;"
        "  padding: 6px 24px; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:disabled { background: #9ca3af; }");
    actionRow->addWidget(m_hsStartButton);
    m_hsStopButton = new QPushButton("结束房间", m_hsMainPage);
    m_hsStopButton->setCursor(Qt::PointingHandCursor);
    m_hsStopButton->setFixedHeight(40);
    m_hsStopButton->setEnabled(false);
    m_hsStopButton->setStyleSheet(
        "QPushButton { background: #ffffff; color: #dc2626; border: 1px solid #dc2626;"
        "  border-radius: 8px; padding: 6px 24px; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #fef2f2; }"
        "QPushButton:disabled { color: #9ca3af; border-color: #d1d5db; }");
    actionRow->addWidget(m_hsStopButton);
    actionRow->addStretch();
    mainLayout->addLayout(actionRow);

    auto *hint = new QLabel(
        "提示：内核不会主动连接游戏端口，可以先启动房间再进游戏。\n"
        "同一台机器同时只能运行一个联机内核；房间结束后需要重新创建。",
        m_hsMainPage);
    hint->setWordWrap(true);
    hint->setStyleSheet("font-size: 12px; color: #9ca3af;");
    mainLayout->addWidget(hint);

    mainLayout->addStretch();
    m_hsStack->addWidget(m_hsMainPage);

    connect(m_hsStartButton, &QPushButton::clicked, this, [this]() {
        if (!m_hongshi) return;
        if (m_hongshi->isReady()) {
            m_hsStatusLabel->setText("房间已创建，等待朋友加入");
            m_hsEndpointLabel->setText(m_hongshi->endpoint());
            m_hsCopyButton->setEnabled(true);
            m_hsStopButton->setEnabled(true);
            m_hsStartButton->setEnabled(false);
            return;
        }
        m_hsStartButton->setEnabled(false);
        m_hsStatusLabel->setText("正在启动内核并申请隧道...");
        m_hsEndpointLabel->setText(QStringLiteral("（等待中）"));
        m_hsCopyButton->setEnabled(false);

        auto *readyConn = new QMetaObject::Connection();
        *readyConn = connect(m_hongshi, &HongshiClient::ready, this,
                             [this, readyConn](const QString &endpoint) {
            disconnect(*readyConn); delete readyConn;
            m_hsStatusLabel->setText("房间已创建，等待朋友加入");
            m_hsEndpointLabel->setText(endpoint);
            m_hsCopyButton->setEnabled(true);
            m_hsStopButton->setEnabled(true);
            m_hsRelayLabel->setText(QStringLiteral("当前中继: %1").arg(m_hongshi->currentRelay()));
        });
        auto *failConn = new QMetaObject::Connection();
        *failConn = connect(m_hongshi, &HongshiClient::failed, this,
                            [this, readyConn, failConn](const QString &err) {
            disconnect(*readyConn); delete readyConn;
            disconnect(*failConn); delete failConn;
            m_hsStartButton->setEnabled(true);
            m_hsStatusLabel->setText(QString("启动失败: %1").arg(err));
            m_hsEndpointLabel->setText(QStringLiteral("（尚未启动）"));
            m_hsCopyButton->setEnabled(false);
        });
        int port = 25565;
        if (m_hsPortEdit) {
            bool ok = false;
            const int typed = m_hsPortEdit->text().trimmed().toInt(&ok);
            if (ok && typed > 0 && typed <= 65535) port = typed;
        }
        m_hongshi->start(QString(), port);
    });

    connect(m_hsStopButton, &QPushButton::clicked, this, [this]() {
        if (m_hongshi) m_hongshi->stop();
        m_hsStatusLabel->setText("未启动");
        m_hsEndpointLabel->setText(QStringLiteral("（尚未启动）"));
        m_hsRelayLabel->setText(QString());
        m_hsCopyButton->setEnabled(false);
        m_hsStopButton->setEnabled(false);
        m_hsStartButton->setEnabled(true);
    });

    connect(m_hsCopyButton, &QPushButton::clicked, this, [this]() {
        if (m_hsEndpointLabel) {
            QGuiApplication::clipboard()->setText(m_hsEndpointLabel->text());
            appendLog(QString("红石联机：已复制地址 %1").arg(m_hsEndpointLabel->text()));
        }
    });

    refreshHongshiInstallState();
    return page;
}

void MainWindow::refreshHongshiInstallState()
{
    if (!m_hsStack) return;

    if (!m_hongshi) {
        m_hongshi = new HongshiClient(this);
        connect(m_hongshi, &HongshiClient::logLine, this, [this](const QString &line) {
            appendLog(line);
        });
        connect(m_hongshi, &HongshiClient::processExited, this, [this](int code) {
            if (!m_hsStatusLabel) return;
            if (code == 0) {
                m_hsStatusLabel->setText("房间已结束（进程退出）");
            }
            if (m_hsEndpointLabel) m_hsEndpointLabel->setText(QStringLiteral("（尚未启动）"));
            if (m_hsCopyButton) m_hsCopyButton->setEnabled(false);
            if (m_hsStopButton) m_hsStopButton->setEnabled(false);
            if (m_hsStartButton) m_hsStartButton->setEnabled(true);
        });
    }

    const QString exePath = hongshiDir() + "/" + HongshiClient::executableName();
    const bool installed = QFileInfo::exists(exePath) && QFileInfo(exePath).size() > 0;
    m_hongshi->setExecutable(exePath);

    if (!installed) {
        m_hsStack->setCurrentWidget(m_hsInstallPage);
        return;
    }

    m_hsStack->setCurrentWidget(m_hsMainPage);
    m_hongshi->fetchRelays();
}

void MainWindow::installHongshiCore()
{
    if (!m_hongshi) return;
    m_hongshi->ensureKernel(hongshiDir());
}
