// 联机标签页 —— 陶瓦联机（Terracotta）入口与 Win7 兼容性守卫。
#include "main_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QStackedWidget>
#include <QTabWidget>
#include <QButtonGroup>
#include <QMessageBox>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool MainWindow::isWindows7OrBelow()
{
#ifdef Q_OS_WIN
    // RtlGetVersion is unaffected by compatibility manifests (unlike
    // GetVersionEx), so it reports the true OS version.
    typedef LONG(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(
            reinterpret_cast<void *>(GetProcAddress(hMod, "RtlGetVersion")));
        if (fn) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0) {
                // Windows 7 = 6.1, Vista = 6.0, XP = 5.x
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

    // The multiplayer tab hosts a vertical sub-tab layout (left nav + right
    // content), matching the pattern used by the download tab.
    auto *rootLayout = new QHBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *navWidget = new QWidget(page);
    navWidget->setFixedWidth(180);
    navWidget->setObjectName("multiplayerNav");
    navWidget->setStyleSheet(
        "#multiplayerNav { background-color: #f9fafb; border-right: 1px solid #e5e7eb; }"
    );
    auto *navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(16, 20, 16, 20);
    navLayout->setSpacing(6);

    auto *navTitle = new QLabel("联机", navWidget);
    navTitle->setStyleSheet("font-size: 16px; font-weight: 700; margin-bottom: 8px;");
    navLayout->addWidget(navTitle);

    auto *terracottaBtn = new QPushButton("🌐  陶瓦联机", navWidget);
    terracottaBtn->setCheckable(true);
    terracottaBtn->setChecked(true);
    terracottaBtn->setFixedHeight(44);
    terracottaBtn->setCursor(Qt::PointingHandCursor);
    terracottaBtn->setStyleSheet(
        "QPushButton { text-align: left; padding-left: 14px; background: transparent;"
        "  border: none; border-radius: 8px; font-size: 14px; font-weight: 500; color: #6b7280; }"
        "QPushButton:hover { background-color: rgba(59,130,246,0.08); color: #3b82f6; }"
        "QPushButton:checked { background-color: rgba(59,130,246,0.12); color: #3b82f6; font-weight: 600; }"
    );
    navLayout->addWidget(terracottaBtn);
    navLayout->addStretch();

    auto *rightContainer = new QWidget(page);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(24, 24, 24, 24);
    rightLayout->setSpacing(16);

    auto *contentStack = new QStackedWidget(rightContainer);
    rightLayout->addWidget(contentStack);

    // --- Terracotta page ---
    auto *tcPage = new QWidget(contentStack);
    auto *tcLayout = new QVBoxLayout(tcPage);
    tcLayout->setContentsMargins(0, 0, 0, 0);
    tcLayout->setSpacing(16);

    auto *tcCard = new QFrame(tcPage);
    tcCard->setObjectName("terracottaCard");
    tcCard->setStyleSheet("#terracottaCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }");
    auto *tcCardLayout = new QVBoxLayout(tcCard);
    tcCardLayout->setContentsMargins(20, 18, 20, 18);
    tcCardLayout->setSpacing(10);

    auto *tcTitle = new QLabel("陶瓦联机 (Terracotta)", tcCard);
    tcTitle->setStyleSheet("font-size: 18px; font-weight: 800; color: #111827;");
    tcCardLayout->addWidget(tcTitle);

    auto *tcDesc = new QLabel("陶瓦联机让你无需公网 IP 或端口映射，就能和朋友直接联机畅玩。", tcCard);
    tcDesc->setWordWrap(true);
    tcDesc->setStyleSheet("font-size: 13px; color: #4b5563;");
    tcCardLayout->addWidget(tcDesc);

    auto *startBtn = new QPushButton("启动联机", tcCard);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setFixedHeight(40);
    startBtn->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 10px; padding: 8px 24px; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }");
    connect(startBtn, &QPushButton::clicked, this, [this]() {
        appendLog("陶瓦联机：请求启动联机会话（功能开发中）");
    });
    tcCardLayout->addWidget(startBtn, 0, Qt::AlignLeft);

    tcLayout->addWidget(tcCard);
    tcLayout->addStretch();
    contentStack->addWidget(tcPage);

    rootLayout->addWidget(navWidget);
    rootLayout->addWidget(rightContainer, 1);
    return page;
}

void MainWindow::applyWin7MultiplayerGuard()
{
    if (!m_multiplayerTab || !m_mainTabWidget) return;
    if (m_multiplayerTabIndex < 0) return;

    // Only relevant on Win7 and below, and only when the user hasn't chosen
    // to permanently skip the compatibility check.
    if (!isWindows7OrBelow()) return;
    if (m_core.configStore()->config().forceEnableMultiplayerOnWin7) return;

    // Create a full-page overlay covering the entire multiplayer tab content.
    auto *overlay = new QWidget(m_multiplayerTab);
    overlay->setObjectName("win7MultiplayerOverlay");
    overlay->setStyleSheet("#win7MultiplayerOverlay { background-color: rgba(15,23,42,0.55); }");
    overlay->setGeometry(m_multiplayerTab->rect());

    auto *ovLayout = new QVBoxLayout(overlay);
    ovLayout->setAlignment(Qt::AlignCenter);

    auto *panel = new QFrame(overlay);
    panel->setObjectName("win7MpPanel");
    panel->setStyleSheet(
        "#win7MpPanel { background: #ffffff; border-radius: 16px; border: 1px solid #e5e7eb; }"
    );
    panel->setFixedWidth(520);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(28, 24, 28, 22);
    panelLayout->setSpacing(16);

    auto *title = new QLabel("联机可能不兼容当前系统", panel);
    title->setStyleSheet("font-size: 18px; font-weight: 800; color: #dc2626;");
    panelLayout->addWidget(title);

    auto *desc = new QLabel(
        "检测到当前系统为 Windows 7 或更低版本。陶瓦联机在较新的系统上体验更佳，"
        "在旧系统上可能存在兼容性问题。\n\n是否仍要继续使用联机功能？",
        panel);
    desc->setWordWrap(true);
    desc->setStyleSheet("font-size: 13px; color: #374151;");
    panelLayout->addWidget(desc);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto *noBtn = new QPushButton("否", panel);
    noBtn->setCursor(Qt::PointingHandCursor);
    noBtn->setFixedSize(96, 36);
    noBtn->setStyleSheet(
        "QPushButton { background: #f3f4f6; color: #374151; border: 1px solid #d1d5db;"
        "  border-radius: 8px; font-size: 14px; font-weight: 600; }"
        "QPushButton:hover { background: #e5e7eb; }"
    );

    auto *yesBtn = new QPushButton("是", panel);
    yesBtn->setCursor(Qt::PointingHandCursor);
    yesBtn->setFixedSize(96, 36);
    yesBtn->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none;"
        "  border-radius: 8px; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }"
    );

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

    connect(yesBtn, &QPushButton::clicked, this, [this, overlay]() {
        overlay->hide();
    });

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
