#include "main_window.h"
#include "download_progress_dialog.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScopeGuard>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QPainter>
#include <QPainterPath>
#include <random>
#include <functional>

#include "download_manager.h"

namespace {
inline int randomInt(int min, int max)
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

struct DownloadOption {
    int id;
    QString title;
    QString description;
    QString accentColor;
};

void showError(QWidget *parent, const QString &message)
{
    QMessageBox::critical(parent, "Kill", message);
}

void showInfo(QWidget *parent, const QString &message)
{
    QMessageBox::information(parent, "Kill", message);
}

QString formatDownloadCount(qint64 count)
{
    if (count >= 100000000) return QString::number(count / 100000000.0, 'f', 2) + " 亿";
    if (count >= 10000) return QString::number(count / 10000.0, 'f', 2) + " 万";
    return QString::number(count);
}

QString normalizeLoaderName(const QString &raw)
{
    const QString s = raw.toLower().trimmed();
    if (s.isEmpty() || s == "unknown" || s == "none" || s == "any" || s == "all") return {};
    if (s == "fabric") return "Fabric";
    if (s == "forge") return "Forge";
    if (s == "neoforge" || s == "neo-forge" || s == "neoforged" || s == "neo") return "NeoForge";
    if (s == "quilt") return "Quilt";
    if (s == "liteloader") return "LiteLoader";
    if (s == "rift") return "Rift";
    if (s == "modloader") return "ModLoader";
    if (s == "bukkit" || s == "spigot" || s == "paper") return "Bukkit";
    if (s == "bungeecord" || s == "bungee") return "BungeeCord";
    if (s == "velocity") return "Velocity";
    if (s == "minecraft" || s == "vanilla" || s == "data-pack" || s == "datapack") return {};
    QString cap = raw.trimmed();
    if (cap.isEmpty()) return {};
    if (cap.size() > 32) return {};
    cap[0] = cap[0].toUpper();
    return cap;
}

namespace {
enum class McVersionKind {
    NumericRelease,
    NumericPrerelease,
    NumericSnapshot,
    LegacySnapshot,
    Classic,
    Unknown
};

struct McVersionSeries {
    McVersionKind kind = McVersionKind::Unknown;
    QString series;
    bool hasNumericKey = false;
    qlonglong major = 0;
    qlonglong minor = 0;
    QString legacyPrefix;
    QString raw;
};

McVersionSeries parseMcVersionSeries(const QString &versionRaw)
{
    McVersionSeries result;
    result.raw = versionRaw;
    if (versionRaw.isEmpty()) return result;

    static const QRegularExpression legacySnapRx(R"(^(\d{2})w(\d{2})([a-zA-Z]?.*)$)");
    QRegularExpressionMatch ls = legacySnapRx.match(versionRaw);
    if (ls.hasMatch()) {
        result.kind = McVersionKind::LegacySnapshot;
        result.legacyPrefix = ls.captured(1) + "w";
        result.series = result.legacyPrefix;
        result.hasNumericKey = false;
        return result;
    }

    static const QRegularExpression classicRx(R"(^(?:b[0-9]|a[0-9]|infdev|indev|rd-[0-9]|c[0-9]).*$)",
                                               QRegularExpression::CaseInsensitiveOption);
    if (classicRx.match(versionRaw).hasMatch()) {
        result.kind = McVersionKind::Classic;
        result.series = QStringLiteral("Classic");
        return result;
    }

    static const QRegularExpression preReleaseRx(
        R"(^(.+?)[-_. ]+(?:pre|rc|beta|preview|alpha|test|snapshot)[-_. ]*\d*.*$)",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch pr = preReleaseRx.match(versionRaw);
    if (pr.hasMatch()) {
        const McVersionSeries base = parseMcVersionSeries(pr.captured(1));
        if (base.kind != McVersionKind::Unknown && base.kind != McVersionKind::Classic) {
            result.kind = McVersionKind::NumericPrerelease;
            result.series = base.series;
            result.hasNumericKey = base.hasNumericKey;
            result.major = base.major;
            result.minor = base.minor;
            return result;
        }
    }

    static const QRegularExpression numericSnapshotRx(
        R"(^(\d+)\.(\d+)(?:\.(\d+))?[-_. ]+(?:snapshot|pre|rc|beta|preview|alpha|test)[-_. ]*\d*.*$)",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch ns = numericSnapshotRx.match(versionRaw);
    if (ns.hasMatch()) {
        bool okM = false, okN = false;
        const qlonglong ma = ns.captured(1).toLongLong(&okM);
        const qlonglong mi = ns.captured(2).toLongLong(&okN);
        if (okM && okN) {
            result.kind = McVersionKind::NumericSnapshot;
            result.hasNumericKey = true;
            result.major = ma;
            result.minor = mi;
            result.series = QString("%1.%2").arg(ma).arg(mi);
            return result;
        }
    }

    static const QRegularExpression spacedSnapshotRx(
        R"(^(\d+)\.(\d+)(?:\.(\d+))?[ ]+(?:Snapshot|Pre|RC|Beta|Preview|Alpha|Test)[-_. ]*\d*.*$)",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch ss = spacedSnapshotRx.match(versionRaw);
    if (ss.hasMatch()) {
        bool okM = false, okN = false;
        const qlonglong ma = ss.captured(1).toLongLong(&okM);
        const qlonglong mi = ss.captured(2).toLongLong(&okN);
        if (okM && okN) {
            result.kind = McVersionKind::NumericSnapshot;
            result.hasNumericKey = true;
            result.major = ma;
            result.minor = mi;
            result.series = QString("%1.%2").arg(ma).arg(mi);
            return result;
        }
    }

    static const QRegularExpression compactSnapshotRx(R"(^(\d+)\.(\d+)(?:\.(\d+))?snapshot\d+.*$)",
                                                       QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch cs = compactSnapshotRx.match(versionRaw);
    if (cs.hasMatch()) {
        bool okM = false, okN = false;
        const qlonglong ma = cs.captured(1).toLongLong(&okM);
        const qlonglong mi = cs.captured(2).toLongLong(&okN);
        if (okM && okN) {
            result.kind = McVersionKind::NumericSnapshot;
            result.hasNumericKey = true;
            result.major = ma;
            result.minor = mi;
            result.series = QString("%1.%2").arg(ma).arg(mi);
            return result;
        }
    }

    static const QRegularExpression releaseRx(R"(^(\d+)\.(\d+)(?:\.(\d+))?(?:[-_.].*)?$)");
    QRegularExpressionMatch rel = releaseRx.match(versionRaw);
    if (rel.hasMatch()) {
        bool okM = false, okN = false;
        const qlonglong ma = rel.captured(1).toLongLong(&okM);
        const qlonglong mi = rel.captured(2).toLongLong(&okN);
        if (okM && okN) {
            result.kind = McVersionKind::NumericRelease;
            result.hasNumericKey = true;
            result.major = ma;
            result.minor = mi;
            result.series = QString("%1.%2").arg(ma).arg(mi);
            return result;
        }
    }

    static const QRegularExpression standalonePreRx(R"(^Pre-Release\s*\d+$)",
                                                     QRegularExpression::CaseInsensitiveOption);
    if (standalonePreRx.match(versionRaw).hasMatch()) {
        result.kind = McVersionKind::Unknown;
        result.series = QStringLiteral("Other");
        return result;
    }

    result.kind = McVersionKind::Unknown;
    result.series = versionRaw;
    return result;
}

int compareNumericSeries(const McVersionSeries &a, const McVersionSeries &b)
{
    if (a.hasNumericKey && b.hasNumericKey) {
        if (a.major != b.major) return a.major > b.major ? 1 : -1;
        if (a.minor != b.minor) return a.minor > b.minor ? 1 : -1;
    }
    if (a.hasNumericKey && !b.hasNumericKey) return 1;
    if (!a.hasNumericKey && b.hasNumericKey) return -1;
    return 0;
}
}

QList<QString> extractMajorVersionFamilies(const QStringList &gameVersions)
{
    if (gameVersions.isEmpty()) return {};

    QSet<QString> families;
    for (const QString &gv : gameVersions) {
        if (gv.isEmpty()) continue;
        const McVersionSeries info = parseMcVersionSeries(gv);
        QString series = info.series;
        if (info.kind == McVersionKind::Unknown && !info.raw.isEmpty()) {
            series = info.raw;
        }
        if (series.isEmpty()) continue;
        families.insert(series);
    }
    if (families.isEmpty()) return {};

    QList<QString> result = families.values();
    std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        if (a.isEmpty() && b.isEmpty()) return false;
        if (a.isEmpty()) return false;
        if (b.isEmpty()) return true;

        const McVersionSeries sa = parseMcVersionSeries(a);
        const McVersionSeries sb = parseMcVersionSeries(b);
        const int numCmp = compareNumericSeries(sa, sb);
        if (numCmp != 0) return numCmp > 0;

        static const QRegularExpression legSnap(R"(^(\d{2})w$)");
        const QRegularExpressionMatch la = legSnap.match(a);
        const QRegularExpressionMatch lb = legSnap.match(b);
        if (la.hasMatch() && lb.hasMatch()) {
            bool okA = false, okB = false;
            const int ya = la.captured(1).toInt(&okA);
            const int yb = lb.captured(1).toInt(&okB);
            if (okA && okB) return ya > yb;
            return a.compare(b, Qt::CaseInsensitive) > 0;
        }
        if (la.hasMatch() && !lb.hasMatch()) return sb.kind == McVersionKind::Classic;
        if (!la.hasMatch() && lb.hasMatch()) return !(sa.kind == McVersionKind::Classic);

        if (sa.kind == McVersionKind::Classic && sb.kind != McVersionKind::Classic) return false;
        if (sa.kind != McVersionKind::Classic && sb.kind == McVersionKind::Classic) return true;

        return a.compare(b, Qt::CaseInsensitive) > 0;
    });
    return result;
}

int compareMajorVersionFamily(const QString &a, const QString &b)
{
    if (a.isEmpty() && b.isEmpty()) return 0;
    if (a.isEmpty()) return -1;
    if (b.isEmpty()) return 1;

    const McVersionSeries sa = parseMcVersionSeries(a);
    const McVersionSeries sb = parseMcVersionSeries(b);
    const int num = compareNumericSeries(sa, sb);
    if (num != 0) return num;

    static const QRegularExpression legSnap(R"(^(\d{2})w$)");
    const QRegularExpressionMatch la = legSnap.match(a);
    const QRegularExpressionMatch lb = legSnap.match(b);
    if (la.hasMatch() && lb.hasMatch()) {
        bool okA = false, okB = false;
        const int ya = la.captured(1).toInt(&okA);
        const int yb = lb.captured(1).toInt(&okB);
        if (okA && okB && ya != yb) return ya > yb ? 1 : -1;
        return 0;
    }
    if (la.hasMatch() && !lb.hasMatch()) return sb.kind == McVersionKind::Classic ? 1 : -1;
    if (!la.hasMatch() && lb.hasMatch()) return sa.kind == McVersionKind::Classic ? -1 : 1;

    if (sa.kind == McVersionKind::Classic && sb.kind != McVersionKind::Classic) return -1;
    if (sa.kind != McVersionKind::Classic && sb.kind == McVersionKind::Classic) return 1;

    const int cmp = a.compare(b, Qt::CaseInsensitive);
    return cmp > 0 ? 1 : (cmp < 0 ? -1 : 0);
}

QJsonObject modrinthPrimaryFile(const QJsonObject &versionObj)
{
    if (versionObj.isEmpty()) return {};

    if (versionObj.contains("primaryFile")) {
        const QJsonObject pf = versionObj.value("primaryFile").toObject();
        if (!pf.isEmpty()) {
            const QString fn = pf.value("filename").toString();
            const QString url = pf.value("url").toString();
            if (!fn.isEmpty() || !url.isEmpty()) {
                return pf;
            }
        }
    }

    const QJsonArray files = versionObj.value("files").toArray();
    if (files.isEmpty()) return {};

    QJsonObject firstValid;
    for (const QJsonValue &fv : files) {
        const QJsonObject fo = fv.toObject();
        if (fo.isEmpty()) continue;

        if (firstValid.isEmpty()) {
            const QString fn = fo.value("filename").toString();
            const QString url = fo.value("url").toString();
            if (!fn.isEmpty() || !url.isEmpty()) {
                firstValid = fo;
            }
        }

        if (fo.value("primary").toBool()) {
            return fo;
        }
    }

    if (!firstValid.isEmpty()) return firstValid;
    return files.first().toObject();
}

QPixmap createRoundedPixmap(const QPixmap &src, int radius)
{
    if (src.isNull()) {
        QPixmap defaultIcon(48, 48);
        defaultIcon.fill(QColor("#3b82f6"));
        QPainter p(&defaultIcon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ffffff"));
        p.drawEllipse(8, 8, 32, 32);
        p.setPen(QPen(QColor("#3b82f6"), 3));
        p.setFont(QFont("Arial", 18, QFont::Bold));
        p.drawText(defaultIcon.rect(), Qt::AlignCenter, "M");
        return defaultIcon;
    }
    QSize size(48, 48);
    QPixmap scaled = src.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap result(size);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, size.width(), size.height(), radius, radius);
    painter.setClipPath(path);
    int x = (scaled.width() - size.width()) / 2;
    int y = (scaled.height() - size.height()) / 2;
    painter.drawPixmap(-x, -y, scaled);
    return result;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1280, 860);
    setWindowTitle("Kill - Minecraft Launcher");
    m_networkManager = new QNetworkAccessManager(this);
    buildUi();
    m_logFilePath = logFilePath();
    connect(&m_core, &LauncherCore::logMessage, this, &MainWindow::appendLog);
    appendLog(QString("日志文件: %1").arg(QDir::toNativeSeparators(m_logFilePath)));
    loadConfigToUi();
    refreshInstalledVersionsUi();
}

void MainWindow::buildUi()
{
    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(buildTopBar());
    mainLayout->addWidget(buildAnnouncementBar());

    m_mainStackedWidget = new QStackedWidget(this);
    m_mainStackedWidget->setStyleSheet("QStackedWidget { background-color: transparent; }");

    m_mainTabsPage = new QWidget(m_mainStackedWidget);
    auto *tabsLayout = new QVBoxLayout(m_mainTabsPage);
    tabsLayout->setContentsMargins(0, 0, 0, 0);
    tabsLayout->setSpacing(0);

    auto *tabs = new QTabWidget(m_mainTabsPage);
    m_mainTabWidget = tabs;
    tabs->addTab(buildHomeTab(), "启动");
    tabs->addTab(buildAccountsTab(), "账户");
    tabs->addTab(buildDownloadTab(), "下载");
    tabs->addTab(buildVersionManagementTab(), "版本管理");
    tabs->addTab(buildSettingsTab(), "设置");
    tabs->addTab(buildLogsTab(), "日志");
    tabsLayout->addWidget(tabs, 1);

    m_mainStackedWidget->addWidget(m_mainTabsPage);
    m_aboutPage = buildAboutPage();
    m_mainStackedWidget->addWidget(m_aboutPage);
    m_mainStackedWidget->setCurrentWidget(m_mainTabsPage);

    mainLayout->addWidget(m_mainStackedWidget, 1);

    // 底部进度区域
    auto *statusWidget = new QWidget(this);
    auto *statusLayout = new QHBoxLayout(statusWidget);
    statusLayout->setContentsMargins(10, 5, 10, 5);

    m_statusLabel = new QLabel("就绪", this);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statusLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setMinimumWidth(300);
    m_progressBar->setVisible(false);
    statusLayout->addWidget(m_progressBar);

    mainLayout->addWidget(statusWidget);
    setCentralWidget(centralWidget);

    connect(&m_core, &LauncherCore::progressStarted, this, [this](const QString &message) {
        m_progressBar->setVisible(true);
        m_progressBar->setValue(0);
        m_statusLabel->setText(message);
    });
    connect(&m_core, &LauncherCore::progressUpdated, this, [this](int value, int maximum, const QString &message) {
        m_progressBar->setMaximum(maximum);
        m_progressBar->setValue(value);
        m_statusLabel->setText(message);
    });
    connect(&m_core, &LauncherCore::progressFinished, this, [this](const QString &message) {
        m_progressBar->setValue(100);
        m_statusLabel->setText(message);
    });

    applyTheme(false);
}

QWidget *MainWindow::buildTopBar()
{
    auto *topBar = new QWidget(this);
    topBar->setObjectName("topBar");
    topBar->setFixedHeight(56);
    topBar->setStyleSheet(
        "#topBar {"
        "  background-color: #ffffff;"
        "  border-bottom: 1px solid #e5e7eb;"
        "}"
    );

    auto *layout = new QHBoxLayout(topBar);
    layout->setContentsMargins(20, 0, 20, 0);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel("Kill", topBar);
    titleLabel->setStyleSheet(
        "font-size: 20px;"
        "font-weight: 700;"
        "color: #111827;"
    );

    auto *subtitleLabel = new QLabel("Minecraft Launcher", topBar);
    subtitleLabel->setStyleSheet(
        "font-size: 13px;"
        "color: #6b7280;"
    );

    m_themeToggleButton = new QPushButton("🌙 暗色", topBar);
    m_themeToggleButton->setFixedSize(96, 36);
    m_themeToggleButton->setCursor(Qt::PointingHandCursor);
    m_themeToggleButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 8px;"
        "  color: #111827;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #e5e7eb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #d1d5db;"
        "}"
    );
    connect(m_themeToggleButton, &QPushButton::clicked, this, [this] {
        animateButton(m_themeToggleButton);
        toggleTheme();
    });

    m_aboutEntryButton = new QPushButton("ℹ️ 关于", topBar);
    m_aboutEntryButton->setFixedSize(96, 36);
    m_aboutEntryButton->setCursor(Qt::PointingHandCursor);
    m_aboutEntryButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 8px;"
        "  color: #111827;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #e5e7eb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #d1d5db;"
        "}"
    );
    connect(m_aboutEntryButton, &QPushButton::clicked, this, [this] {
        animateButton(m_aboutEntryButton);
        showAboutPage();
    });

    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
    layout->addStretch();
    layout->addWidget(m_aboutEntryButton);
    layout->addWidget(m_themeToggleButton);

    return topBar;
}

QWidget *MainWindow::buildAnnouncementBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName("announcementBar");
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 8, 12, 8);
    layout->setSpacing(10);

    auto *iconLabel = new QLabel("📢", bar);
    iconLabel->setStyleSheet("font-size: 15px;");
    layout->addWidget(iconLabel);

    m_announcementLabel = new QLabel(
        QStringLiteral(
            "【严正公告】我们坚决反对个别人士的恶意举报——这种行为严重破坏了 MC 社区的繁荣发展！"
            "Kill 郑重承诺：绝不下架、绝不关闭任何下载功能，版本、模组、整合包、资源包下载将永久保持开放。"),
        bar);
    m_announcementLabel->setWordWrap(true);
    m_announcementLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_announcementLabel, 1);

    m_announcementCloseButton = new QPushButton("✕", bar);
    m_announcementCloseButton->setFixedSize(24, 24);
    m_announcementCloseButton->setCursor(Qt::PointingHandCursor);
    m_announcementCloseButton->setToolTip("收起公告");
    layout->addWidget(m_announcementCloseButton);

    connect(m_announcementCloseButton, &QPushButton::clicked, this, [this] {
        if (m_announcementBar) {
            m_announcementBar->setVisible(false);
        }
    });

    m_announcementBar = bar;
    return bar;
}

void MainWindow::applyTheme(bool dark)
{
    m_isDarkTheme = dark;
    if (m_themeToggleButton) {
        m_themeToggleButton->setText(dark ? "☀️ 亮色" : "🌙 暗色");
    }

    QString baseStyle;
    if (dark) {
        baseStyle = QString(
            "QMainWindow { background-color: #111827; }"
            "QWidget { color: #e5e7eb; }"
            "QTabWidget::pane { border: 1px solid #374151; background-color: #111827; top: -1px; }"
            "QTabBar::tab {"
            "  background: #1f2937; color: #9ca3af; padding: 10px 22px;"
            "  border: 1px solid #374151; border-bottom: none;"
            "  border-top-left-radius: 6px; border-top-right-radius: 6px;"
            "  margin-right: 4px; font-size: 13px; font-weight: 500;"
            "}"
            "QTabBar::tab:selected {"
            "  background: #111827; color: #60a5fa;"
            "  border-bottom: 2px solid #3b82f6;"
            "}"
            "QTabBar::tab:hover:!selected { background: #374151; color: #e5e7eb; }"
            "QGroupBox {"
            "  border: 1px solid #374151; border-radius: 8px;"
            "  margin-top: 14px; padding-top: 8px;"
            "  background-color: #1f2937; font-weight: 600; font-size: 13px; color: #e5e7eb;"
            "}"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }"
            "QPushButton {"
            "  background-color: #374151; color: #e5e7eb;"
            "  border: 1px solid #4b5563; border-radius: 6px;"
            "  padding: 6px 14px; font-size: 13px; font-weight: 500;"
            "}"
            "QPushButton:hover { background-color: #4b5563; border-color: #6b7280; }"
            "QPushButton:pressed { background-color: #1f2937; }"
            "QLineEdit, QComboBox, QSpinBox {"
            "  background-color: #111827; color: #e5e7eb;"
            "  border: 1px solid #374151; border-radius: 6px;"
            "  padding: 5px 10px; font-size: 13px; selection-background-color: #3b82f6;"
            "}"
            "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #3b82f6; }"
            "QComboBox::drop-down { border: none; width: 24px; }"
            "QComboBox QAbstractItemView {"
            "  background-color: #1f2937; color: #e5e7eb;"
            "  border: 1px solid #374151; selection-background-color: #3b82f6;"
            "}"
            "QTableWidget {"
            "  background-color: #111827; color: #e5e7eb;"
            "  gridline-color: #374151; border: 1px solid #374151; border-radius: 6px;"
            "  font-size: 13px;"
            "}"
            "QTableWidget::item:selected { background-color: #1e3a8a; color: #ffffff; }"
            "QHeaderView::section {"
            "  background-color: #1f2937; color: #e5e7eb; padding: 8px;"
            "  border: none; border-bottom: 1px solid #374151; font-weight: 600;"
            "}"
            "QCheckBox { spacing: 8px; font-size: 13px; color: #e5e7eb; }"
            "QCheckBox::indicator {"
            "  width: 18px; height: 18px; border-radius: 4px;"
            "  border: 1px solid #4b5563; background-color: #111827;"
            "}"
            "QCheckBox::indicator:checked { background-color: #3b82f6; border-color: #3b82f6; }"
            "QLabel { color: #e5e7eb; font-size: 13px; }"
            "QScrollBar:vertical { background: #1f2937; width: 10px; border-radius: 5px; }"
            "QScrollBar::handle:vertical { background: #4b5563; border-radius: 5px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: #6b7280; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal { background: #1f2937; height: 10px; border-radius: 5px; }"
            "QScrollBar::handle:horizontal { background: #4b5563; border-radius: 5px; min-width: 30px; }"
            "QScrollBar::handle:horizontal:hover { background: #6b7280; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QPlainTextEdit, QTextEdit {"
            "  background-color: #111827; color: #e5e7eb;"
            "  border: 1px solid #374151; border-radius: 6px; font-size: 13px;"
            "}"
            "QSplitter::handle { background-color: #374151; }"
            "QProgressBar {"
            "  background-color: #1f2937; border: 1px solid #374151;"
            "  border-radius: 6px; text-align: center; color: #e5e7eb; font-size: 12px;"
            "}"
            "QProgressBar::chunk { background-color: #3b82f6; border-radius: 5px; }"
            "QMessageBox { background-color: #111827; }"
            "QMessageBox QLabel { color: #e5e7eb; }"
            "QMessageBox QPushButton {"
            "  min-width: 80px; padding: 6px 16px;"
            "}"
        );
    } else {
        baseStyle = QString(
            "QMainWindow { background-color: #f9fafb; }"
            "QWidget { color: #111827; }"
            "QTabWidget::pane { border: 1px solid #e5e7eb; background-color: #f9fafb; top: -1px; }"
            "QTabBar::tab {"
            "  background: #f3f4f6; color: #6b7280; padding: 10px 22px;"
            "  border: 1px solid #e5e7eb; border-bottom: none;"
            "  border-top-left-radius: 6px; border-top-right-radius: 6px;"
            "  margin-right: 4px; font-size: 13px; font-weight: 500;"
            "}"
            "QTabBar::tab:selected {"
            "  background: #ffffff; color: #2563eb;"
            "  border-bottom: 2px solid #3b82f6;"
            "}"
            "QTabBar::tab:hover:!selected { background: #e5e7eb; color: #111827; }"
            "QGroupBox {"
            "  border: 1px solid #e5e7eb; border-radius: 8px;"
            "  margin-top: 14px; padding-top: 8px;"
            "  background-color: #ffffff; font-weight: 600; font-size: 13px; color: #111827;"
            "}"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }"
            "QPushButton {"
            "  background-color: #ffffff; color: #111827;"
            "  border: 1px solid #d1d5db; border-radius: 6px;"
            "  padding: 6px 14px; font-size: 13px; font-weight: 500;"
            "}"
            "QPushButton:hover { background-color: #f9fafb; border-color: #9ca3af; }"
            "QPushButton:pressed { background-color: #f3f4f6; }"
            "QLineEdit, QComboBox, QSpinBox {"
            "  background-color: #ffffff; color: #111827;"
            "  border: 1px solid #d1d5db; border-radius: 6px;"
            "  padding: 5px 10px; font-size: 13px; selection-background-color: #3b82f6;"
            "}"
            "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #3b82f6; }"
            "QComboBox::drop-down { border: none; width: 24px; }"
            "QComboBox QAbstractItemView {"
            "  background-color: #ffffff; color: #111827;"
            "  border: 1px solid #e5e7eb; selection-background-color: #dbeafe;"
            "}"
            "QTableWidget {"
            "  background-color: #ffffff; color: #111827;"
            "  gridline-color: #e5e7eb; border: 1px solid #e5e7eb; border-radius: 6px;"
            "  font-size: 13px;"
            "}"
            "QTableWidget::item:selected { background-color: #dbeafe; color: #1e3a8a; }"
            "QHeaderView::section {"
            "  background-color: #f9fafb; color: #111827; padding: 8px;"
            "  border: none; border-bottom: 1px solid #e5e7eb; font-weight: 600;"
            "}"
            "QCheckBox { spacing: 8px; font-size: 13px; color: #111827; }"
            "QCheckBox::indicator {"
            "  width: 18px; height: 18px; border-radius: 4px;"
            "  border: 1px solid #d1d5db; background-color: #ffffff;"
            "}"
            "QCheckBox::indicator:checked { background-color: #3b82f6; border-color: #3b82f6; }"
            "QLabel { color: #111827; font-size: 13px; }"
            "QScrollBar:vertical { background: #f3f4f6; width: 10px; border-radius: 5px; }"
            "QScrollBar::handle:vertical { background: #d1d5db; border-radius: 5px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: #9ca3af; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal { background: #f3f4f6; height: 10px; border-radius: 5px; }"
            "QScrollBar::handle:horizontal { background: #d1d5db; border-radius: 5px; min-width: 30px; }"
            "QScrollBar::handle:horizontal:hover { background: #9ca3af; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QPlainTextEdit, QTextEdit {"
            "  background-color: #ffffff; color: #111827;"
            "  border: 1px solid #e5e7eb; border-radius: 6px; font-size: 13px;"
            "}"
            "QSplitter::handle { background-color: #e5e7eb; }"
            "QProgressBar {"
            "  background-color: #f3f4f6; border: 1px solid #e5e7eb;"
            "  border-radius: 6px; text-align: center; color: #111827; font-size: 12px;"
            "}"
            "QProgressBar::chunk { background-color: #3b82f6; border-radius: 5px; }"
        );
    }
    this->setStyleSheet(baseStyle);
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setStyleSheet(baseStyle);
    }

    const QString topBarLight =
        "#topBar {"
        "  background-color: #ffffff;"
        "  border-bottom: 1px solid #e5e7eb;"
        "}";
    const QString topBarDark =
        "#topBar {"
        "  background-color: #1f2937;"
        "  border-bottom: 1px solid #374151;"
        "}";
    const QString generalBtnLight =
        "QPushButton {"
        "  background-color: #f3f4f6;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 8px;"
        "  color: #111827;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #e5e7eb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #d1d5db;"
        "}";
    const QString generalBtnDark =
        "QPushButton {"
        "  background-color: #374151;"
        "  border: 1px solid #4b5563;"
        "  border-radius: 8px;"
        "  color: #e5e7eb;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #4b5563;"
        "  border-color: #6b7280;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #1f2937;"
        "}";
    const QString backBtnLight =
        "QPushButton {"
        "  background-color: #f3f4f6;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 10px;"
        "  color: #374151;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover {"
        "  background-color: #e5e7eb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #d1d5db;"
        "}";
    const QString backBtnDark =
        "QPushButton {"
        "  background-color: #1f2937;"
        "  border: 1px solid #4b5563;"
        "  border-radius: 10px;"
        "  color: #e5e7eb;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover {"
        "  background-color: #374151;"
        "  border-color: #6b7280;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #111827;"
        "}";
    const QString changelogEditLight =
        "QPlainTextEdit {"
        "  background-color: #f9fafb;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 8px;"
        "  padding: 12px;"
        "  font-family: Consolas, 'Courier New', 'Microsoft YaHei', monospace;"
        "  font-size: 13px;"
        "  color: #111827;"
        "}";
    const QString changelogEditDark =
        "QPlainTextEdit {"
        "  background-color: #111827;"
        "  border: 1px solid #374151;"
        "  border-radius: 8px;"
        "  padding: 12px;"
        "  font-family: Consolas, 'Courier New', 'Microsoft YaHei', monospace;"
        "  font-size: 13px;"
        "  color: #e5e7eb;"
        "}";

    auto *topBar = findChild<QWidget *>("topBar");
    if (topBar) {
        topBar->setStyleSheet(dark ? topBarDark : topBarLight);
    }
    if (m_announcementBar) {
        m_announcementBar->setStyleSheet(
            dark
                ? "#announcementBar { background-color: #78350f; border-bottom: 1px solid #92400e; }"
                : "#announcementBar { background-color: #fffbeb; border-bottom: 1px solid #fcd34d; }");
    }
    if (m_announcementLabel) {
        m_announcementLabel->setStyleSheet(
            dark ? "font-size: 13px; font-weight: 600; color: #fde68a;"
                 : "font-size: 13px; font-weight: 600; color: #92400e;");
    }
    if (m_announcementCloseButton) {
        m_announcementCloseButton->setStyleSheet(
            dark
                ? "QPushButton { background: transparent; border: none; color: #fde68a; font-size: 14px; font-weight: 700; }"
                  "QPushButton:hover { color: #ffffff; }"
                : "QPushButton { background: transparent; border: none; color: #92400e; font-size: 14px; font-weight: 700; }"
                  "QPushButton:hover { color: #451a03; }");
    }
    if (m_themeToggleButton) {
        m_themeToggleButton->setStyleSheet(dark ? generalBtnDark : generalBtnLight);
    }
    if (m_aboutEntryButton) {
        m_aboutEntryButton->setStyleSheet(dark ? generalBtnDark : generalBtnLight);
    }
    if (m_modsBackButton) {
        m_modsBackButton->setStyleSheet(dark ? backBtnDark : backBtnLight);
    }
    if (m_aboutBackButton) {
        m_aboutBackButton->setStyleSheet(dark ? backBtnDark : backBtnLight);
    }
    if (m_aboutUpdateLogEdit) {
        m_aboutUpdateLogEdit->setStyleSheet(dark ? changelogEditDark : changelogEditLight);
    }
}

void MainWindow::toggleTheme()
{
    applyTheme(!m_isDarkTheme);
}

void MainWindow::animateButton(QPushButton *button)
{
    if (!button) return;
    auto *animation = new QPropertyAnimation(button, "geometry", this);
    const QRect startRect = button->geometry();
    animation->setDuration(100);
    animation->setKeyValueAt(0, startRect);
    animation->setKeyValueAt(0.5, QRect(startRect.x(), startRect.y() + 2, startRect.width(), startRect.height()));
    animation->setKeyValueAt(1, startRect);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::animateExpand(QWidget *widget, bool expand, int durationMs)
{
    if (!widget) return;
    const int targetHeight = expand ? widget->sizeHint().height() : 0;
    widget->setFixedHeight(expand ? 0 : widget->height());
    widget->show();
    auto *animation = new QPropertyAnimation(widget, "maximumHeight", this);
    animation->setDuration(durationMs);
    animation->setStartValue(widget->height());
    animation->setEndValue(targetHeight);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QPropertyAnimation::finished, this, [widget, expand]() {
        if (!expand) widget->hide();
        else widget->setMaximumHeight(QWIDGETSIZE_MAX);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

QString MainWindow::groupBoxStyle()
{
    return QStringLiteral(
        "QGroupBox {"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 14px;"
        "  margin-top: 10px;"
        "  padding-top: 20px;"
        "  background-color: #ffffff;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 20px;"
        "  padding: 0 8px;"
        "  color: #111827;"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
    );
}

QString MainWindow::labelStyle()
{
    return QStringLiteral(
        "font-size: 13px; font-weight: 500; color: #374151;"
    );
}

QWidget *MainWindow::buildDownloadTab()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *navWidget = new QWidget(page);
    navWidget->setFixedWidth(180);
    navWidget->setObjectName("downloadNav");
    auto *navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(16, 20, 16, 20);
    navLayout->setSpacing(6);

    auto *navTitle = new QLabel("下载中心", navWidget);
    navTitle->setStyleSheet("font-size: 16px; font-weight: 700; margin-bottom: 8px;");
    navLayout->addWidget(navTitle);

    m_downloadNavGroup = new QButtonGroup(this);
    m_downloadNavGroup->setExclusive(true);
    m_downloadNavButtons.clear();

    QStringList categories = {"版本", "模组", "整合包", "资源包", "数据包"};
    QStringList categoryIcons = {"🎮", "🧩", "📦", "🎨", "📊"};
    for (int i = 0; i < categories.size(); ++i) {
        auto *btn = new QPushButton(categoryIcons[i] + "  " + categories[i], navWidget);
        btn->setCheckable(true);
        btn->setFixedHeight(44);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  text-align: left; padding-left: 14px;"
            "  background: transparent; border: none; border-radius: 8px;"
            "  font-size: 14px; font-weight: 500; color: #6b7280;"
            "}"
            "QPushButton:hover { background-color: rgba(59, 130, 246, 0.08); color: #3b82f6; }"
            "QPushButton:checked {"
            "  background-color: rgba(59, 130, 246, 0.12); color: #3b82f6;"
            "  font-weight: 600;"
            "}"
        );
        m_downloadNavGroup->addButton(btn, i);
        m_downloadNavButtons.append(btn);
        navLayout->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, btn, i] {
            animateButton(btn);
            m_downloadStackedWidget->setCurrentIndex(i);
        });
    }
    navLayout->addStretch();

    auto *rightContainer = new QWidget(page);
    rightContainer->setObjectName("downloadRight");
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(16, 16, 16, 16);
    rightLayout->setSpacing(12);

    m_downloadStackedWidget = new QStackedWidget(rightContainer);
    m_downloadStackedWidget->addWidget(buildDownloadVersionsPage());
    m_downloadStackedWidget->addWidget(buildDownloadModsPage());
    m_downloadStackedWidget->addWidget(buildDownloadModpacksPage());
    m_downloadStackedWidget->addWidget(buildDownloadResourcepacksPage());
    m_downloadStackedWidget->addWidget(buildDownloadDatapacksPage());
    rightLayout->addWidget(m_downloadStackedWidget);

    pageLayout->addWidget(navWidget);
    pageLayout->addWidget(rightContainer, 1);

    if (!m_downloadNavButtons.isEmpty()) {
        m_downloadNavButtons.first()->setChecked(true);
    }

    return page;
}

QWidget *MainWindow::buildDownloadVersionsPage()
{
    m_versionFlowStack = new QStackedWidget(this);
    m_versionFlowStack->setStyleSheet("QStackedWidget { background: transparent; }");

    m_versionListPage = new QWidget(m_versionFlowStack);
    auto *layout = new QVBoxLayout(m_versionListPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *searchCard = new QFrame(m_versionListPage);
    searchCard->setObjectName("versionSearchCard");
    searchCard->setStyleSheet(
        "#versionSearchCard {"
        "  background-color: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px;"
        "}"
    );
    auto *searchRow = new QHBoxLayout(searchCard);
    searchRow->setContentsMargins(16, 12, 16, 12);
    searchRow->setSpacing(14);

    auto *searchLabel = new QLabel("搜索", searchCard);
    searchLabel->setStyleSheet("font-size: 13px; color: #374151;");
    searchRow->addWidget(searchLabel);

    m_downloadVersionSearchEdit = new QLineEdit(searchCard);
    m_downloadVersionSearchEdit->setPlaceholderText("输入版本号进行搜索…");
    m_downloadVersionSearchEdit->setClearButtonEnabled(true);
    m_downloadVersionSearchEdit->setMinimumWidth(180);
    m_downloadVersionSearchEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 6px 10px; border: 1px solid #d1d5db; border-radius: 8px;"
        "  background-color: #f9fafb; font-size: 13px; color: #1f2937;"
        "}"
        "QLineEdit:focus { border: 1px solid #3b82f6; background-color: #ffffff; }"
    );
    searchRow->addWidget(m_downloadVersionSearchEdit, 1);

    auto *typeLabel = new QLabel("类型", searchCard);
    typeLabel->setStyleSheet("font-size: 13px; color: #374151;");
    searchRow->addWidget(typeLabel);

    m_downloadVersionTypeCombo = new QComboBox(searchCard);
    m_downloadVersionTypeCombo->addItem("正式版", "release");
    m_downloadVersionTypeCombo->addItem("快照版", "snapshot");
    m_downloadVersionTypeCombo->addItem("愚人节版", "april_fools");
    m_downloadVersionTypeCombo->addItem("远古版", "old");
    m_downloadVersionTypeCombo->addItem("所有版本", "all");
    m_downloadVersionTypeCombo->setCurrentIndex(0);
    m_downloadVersionTypeCombo->setMinimumWidth(120);
    searchRow->addWidget(m_downloadVersionTypeCombo);

    m_refreshDownloadVersionsButton = new QPushButton("刷新", searchCard);
    m_refreshDownloadVersionsButton->setCursor(Qt::PointingHandCursor);
    m_refreshDownloadVersionsButton->setFixedHeight(32);
    m_refreshDownloadVersionsButton->setMinimumWidth(80);
    m_refreshDownloadVersionsButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #3b82f6; color: #ffffff; border: none;"
        "  border-radius: 8px; font-size: 13px; font-weight: 600; padding: 0 16px;"
        "}"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1d4ed8; }"
    );
    searchRow->addWidget(m_refreshDownloadVersionsButton);

    layout->addWidget(searchCard);

    m_versionCenterStack = new QStackedWidget(m_versionListPage);
    m_versionCenterStack->setObjectName("versionCenterStack");

    auto *loadingPage = new QWidget(m_versionCenterStack);
    auto *loadingLayout = new QVBoxLayout(loadingPage);
    loadingLayout->setAlignment(Qt::AlignCenter);
    auto *loadingLabel = new QLabel("正在获取版本列表…", loadingPage);
    loadingLabel->setStyleSheet("font-size: 14px; color: #6b7280;");
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLayout->addWidget(loadingLabel);
    m_versionCenterStack->addWidget(loadingPage);

    auto *failedPage = new QWidget(m_versionCenterStack);
    auto *failedLayout = new QVBoxLayout(failedPage);
    failedLayout->setAlignment(Qt::AlignCenter);
    auto *failedLabel = new QPushButton("获取版本列表失败，点击重试", failedPage);
    failedLabel->setCursor(Qt::PointingHandCursor);
    failedLabel->setStyleSheet(
        "QPushButton { background: transparent; border: none; font-size: 14px; color: #dc2626; }"
        "QPushButton:hover { color: #b91c1c; }"
    );
    failedLayout->addWidget(failedLabel);
    m_versionCenterStack->addWidget(failedPage);

    m_downloadVersionList = new QListWidget(m_versionCenterStack);
    m_downloadVersionList->setObjectName("downloadVersionList");
    m_downloadVersionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_downloadVersionList->setUniformItemSizes(true);
    m_downloadVersionList->setSpacing(0);
    m_downloadVersionList->setIconSize(QSize(32, 32));
    m_downloadVersionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_downloadVersionList->setStyleSheet(
        "QListWidget {"
        "  background-color: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px;"
        "  padding: 4px; outline: none;"
        "}"
        "QListWidget::item { padding: 0px; border-radius: 8px; color: #111827; }"
        "QListWidget::item:hover { background-color: #f3f4f6; }"
        "QListWidget::item:selected { background-color: rgba(59, 130, 246, 0.12); }"
    );
    m_versionCenterStack->addWidget(m_downloadVersionList);
    m_versionCenterStack->setCurrentWidget(loadingPage);

    layout->addWidget(m_versionCenterStack, 1);

    m_versionFlowStack->addWidget(m_versionListPage);

    m_versionInstallPage = buildVersionInstallPage();
    m_versionFlowStack->addWidget(m_versionInstallPage);
    m_versionFlowStack->setCurrentWidget(m_versionListPage);

    connect(m_refreshDownloadVersionsButton, &QPushButton::clicked, this, [this]() {
        animateButton(m_refreshDownloadVersionsButton);
        refreshDownloadVersions();
    });

    connect(m_downloadVersionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_allDownloadVersions.isEmpty()) {
            populateDownloadVersionList();
        }
    });

    connect(m_downloadVersionSearchEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_allDownloadVersions.isEmpty()) {
            populateDownloadVersionList();
        }
    });

    connect(m_downloadVersionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QJsonObject obj = QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray()).object();
        if (obj.isEmpty()) return;
        showVersionInstallPage(obj);
    });

    connect(failedLabel, &QPushButton::clicked, this, [this]() { refreshDownloadVersions(); });

    return m_versionFlowStack;
}

QWidget *MainWindow::buildVersionInstallPage()
{
    auto *page = new QWidget(m_versionFlowStack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(10);

    auto *backButton = new QPushButton("←", page);
    backButton->setFixedSize(38, 36);
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setToolTip("返回版本列表");
    backButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6; border: 1px solid #e5e7eb; border-radius: 10px;"
        "  color: #374151; font-size: 18px; font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: #e5e7eb; border-color: #d1d5db; }"
        "QPushButton:pressed { background-color: #d1d5db; }"
    );
    topRow->addWidget(backButton);

    auto *pageTitle = new QLabel("安装新游戏", page);
    pageTitle->setStyleSheet("font-size: 18px; font-weight: 700; color: #111827;");
    topRow->addWidget(pageTitle);
    topRow->addStretch();
    layout->addLayout(topRow);

    auto *infoCard = new QFrame(page);
    infoCard->setObjectName("versionInstallCard");
    infoCard->setStyleSheet(
        "#versionInstallCard {"
        "  background-color: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px;"
        "}"
    );
    auto *infoLayout = new QHBoxLayout(infoCard);
    infoLayout->setContentsMargins(20, 18, 20, 18);
    infoLayout->setSpacing(18);

    m_versionInstallIcon = new QLabel(infoCard);
    m_versionInstallIcon->setFixedSize(64, 64);
    m_versionInstallIcon->setScaledContents(true);
    infoLayout->addWidget(m_versionInstallIcon, 0, Qt::AlignTop);

    auto *infoTextLayout = new QVBoxLayout();
    infoTextLayout->setSpacing(6);
    m_versionInstallTitle = new QLabel(infoCard);
    m_versionInstallTitle->setStyleSheet("font-size: 22px; font-weight: 700; color: #111827;");
    m_versionInstallTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoTextLayout->addWidget(m_versionInstallTitle);
    m_versionInstallType = new QLabel(infoCard);
    m_versionInstallType->setStyleSheet("font-size: 13px; color: #6b7280;");
    infoTextLayout->addWidget(m_versionInstallType);
    m_versionInstallReleaseTime = new QLabel(infoCard);
    m_versionInstallReleaseTime->setStyleSheet("font-size: 13px; color: #6b7280;");
    infoTextLayout->addWidget(m_versionInstallReleaseTime);
    infoLayout->addLayout(infoTextLayout, 1);

    layout->addWidget(infoCard);

    auto *formCard = new QFrame(page);
    formCard->setObjectName("versionInstallFormCard");
    formCard->setStyleSheet(
        "#versionInstallFormCard {"
        "  background-color: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px;"
        "}"
    );
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(20, 18, 20, 18);
    formLayout->setSpacing(10);

    auto *nameLabel = new QLabel("实例名称", formCard);
    nameLabel->setStyleSheet("font-size: 13px; font-weight: 600; color: #374151;");
    formLayout->addWidget(nameLabel);

    m_versionInstallNameEdit = new QLineEdit(formCard);
    m_versionInstallNameEdit->setPlaceholderText("请输入实例名称");
    m_versionInstallNameEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 10px 14px; border: 1px solid #d1d5db; border-radius: 10px;"
        "  background-color: #f9fafb; font-size: 13px; color: #1f2937;"
        "}"
        "QLineEdit:focus { border: 1px solid #3b82f6; background-color: #ffffff; }"
    );
    formLayout->addWidget(m_versionInstallNameEdit);

    auto *noteLabel = new QLabel("下载完成后，游戏文件将保存到 .minecraft/versions/<实例名称>/", formCard);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
    formLayout->addWidget(noteLabel);

    layout->addWidget(formCard);
    layout->addStretch();

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->setSpacing(10);

    auto *cancelButton = new QPushButton("取消", page);
    cancelButton->setFixedWidth(100);
    cancelButton->setFixedHeight(38);
    cancelButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6; color: #374151; border: 1px solid #e5e7eb;"
        "  border-radius: 10px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #e5e7eb; }"
    );
    btnRow->addWidget(cancelButton);

    m_versionInstallConfirmButton = new QPushButton("确认安装", page);
    m_versionInstallConfirmButton->setFixedWidth(120);
    m_versionInstallConfirmButton->setFixedHeight(38);
    m_versionInstallConfirmButton->setCursor(Qt::PointingHandCursor);
    m_versionInstallConfirmButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #3b82f6; color: #ffffff; border: none;"
        "  border-radius: 10px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1d4ed8; }"
    );
    btnRow->addWidget(m_versionInstallConfirmButton);

    layout->addLayout(btnRow);

    connect(backButton, &QPushButton::clicked, this, [this]() { leaveVersionInstallPage(); });
    connect(cancelButton, &QPushButton::clicked, this, [this]() { leaveVersionInstallPage(); });
    connect(m_versionInstallConfirmButton, &QPushButton::clicked, this, [this]() { performVersionInstall(); });

    return page;
}

void MainWindow::showVersionInstallPage(const QJsonObject &versionObj)
{
    if (!m_versionFlowStack || !m_versionInstallPage) return;

    m_installingVersion = versionObj;

    const QString versionId = versionObj.value("id").toString();
    const QString versionType = versionObj.value("type").toString();
    const QString releaseTime = versionObj.value("releaseTime").toString();

    if (m_versionInstallIcon) {
        m_versionInstallIcon->setPixmap(versionIconFor(versionId, versionType).pixmap(64, 64));
    }
    if (m_versionInstallTitle) {
        m_versionInstallTitle->setText(versionId);
    }
    if (m_versionInstallType) {
        m_versionInstallType->setText(QString("类型：%1").arg(versionTypeDisplayName(versionType)));
    }
    if (m_versionInstallReleaseTime) {
        QString pretty = releaseTime;
        if (!pretty.isEmpty()) {
            pretty.replace("T", " ");
            if (pretty.endsWith("Z")) pretty.chop(1);
        }
        m_versionInstallReleaseTime->setText(QString("发布时间：%1").arg(pretty.isEmpty() ? QStringLiteral("未知") : pretty));
    }
    if (m_versionInstallNameEdit) {
        m_versionInstallNameEdit->setText(versionId);
    }

    m_versionFlowStack->setCurrentWidget(m_versionInstallPage);
}

void MainWindow::leaveVersionInstallPage()
{
    if (m_versionFlowStack && m_versionListPage) {
        m_versionFlowStack->setCurrentWidget(m_versionListPage);
    }
}

void MainWindow::performVersionInstall()
{
    if (m_installingVersion.isEmpty()) return;

    QString instName = m_versionInstallNameEdit ? m_versionInstallNameEdit->text().trimmed() : QString();
    if (instName.isEmpty()) {
        showError(this, "请输入实例名称");
        return;
    }

    const QString verId = m_installingVersion.value("id").toString();
    appendLog(QString("开始安装版本 [%1] -> %2").arg(instName, verId));

    QString error;
    const bool ok = m_core.installVanillaVersionWithDialog(verId, &error, this);
    if (!ok && !error.isEmpty()) {
        showError(this, error);
        return;
    }

    showInfo(this, QString("版本 %1 安装任务已启动，请查看日志进度").arg(instName));
    leaveVersionInstallPage();
    refreshInstalledVersionsUi();
}

QWidget *MainWindow::buildDownloadModsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *topBar = new QWidget(page);
    topBar->setFixedHeight(52);
    auto *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(16, 8, 16, 8);
    topBarLayout->setSpacing(12);

    m_modsBackButton = new QPushButton("←", topBar);
    m_modsBackButton->setFixedSize(38, 36);
    m_modsBackButton->setCursor(Qt::PointingHandCursor);
    m_modsBackButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 10px;"
        "  color: #374151;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover {"
        "  background-color: #e5e7eb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #d1d5db;"
        "}"
    );
    m_modsBackButton->setVisible(false);
    topBarLayout->addWidget(m_modsBackButton);

    auto *pageTitle = new QLabel("下载模组", topBar);
    pageTitle->setStyleSheet("font-size: 18px; font-weight: 700; color: #111827;");
    topBarLayout->addWidget(pageTitle);
    topBarLayout->addStretch();

    layout->addWidget(topBar);

    m_modsSubStack = new QStackedWidget(page);
    m_modsSubStack->setStyleSheet("QStackedWidget { background-color: transparent; }");
    layout->addWidget(m_modsSubStack, 1);

    m_modsSearchPage = new QWidget(m_modsSubStack);
    auto *searchLayoutOuter = new QVBoxLayout(m_modsSearchPage);
    searchLayoutOuter->setContentsMargins(16, 12, 16, 16);
    searchLayoutOuter->setSpacing(12);

    auto *searchBox = new QGroupBox("Modrinth / CurseForge 搜索", m_modsSearchPage);
    searchBox->setStyleSheet(
        "QGroupBox {"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 12px;"
        "  margin-top: 8px;"
        "  padding-top: 16px;"
        "  background-color: #ffffff;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 16px;"
        "  padding: 0 8px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  color: #374151;"
        "}"
    );
    auto *searchLayout = new QGridLayout(searchBox);
    searchLayout->setContentsMargins(16, 16, 16, 16);
    searchLayout->setHorizontalSpacing(12);
    searchLayout->setVerticalSpacing(10);

    m_resourceQueryEdit = new QLineEdit(searchBox);
    m_resourceGameVersionEdit = new QLineEdit(searchBox);
    m_resourceSourceCombo = new QComboBox(searchBox);
    m_resourceTargetTypeCombo = new QComboBox(searchBox);
    m_resourceLoaderCombo = new QComboBox(searchBox);
    m_resourceSearchButton = new QPushButton("🔍 搜索", searchBox);

    m_resourceSourceCombo->addItems({"Modrinth", "CurseForge"});
    m_resourceTargetTypeCombo->addItem("模组 (mod)", "mod");
    m_resourceLoaderCombo->addItems({"", "fabric", "forge", "neoforge", "quilt"});
    m_resourceGameVersionEdit->setPlaceholderText("例如 1.20.1");
    m_resourceQueryEdit->setPlaceholderText("例如 sodium / jei / fabric api");

    const QString inputStyle =
        "QLineEdit, QComboBox {"
        "  padding: 8px 12px;"
        "  border: 1px solid #d1d5db;"
        "  border-radius: 8px;"
        "  font-size: 13px;"
        "  background-color: #ffffff;"
        "}"
        "QLineEdit:focus, QComboBox:focus {"
        "  border-color: #3b82f6;"
        "  outline: none;"
        "}";

    const QString btnStyle =
        "QPushButton {"
        "  padding: 8px 16px;"
        "  background-color: #3b82f6;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1d4ed8; }";

    m_resourceQueryEdit->setStyleSheet(inputStyle);
    m_resourceGameVersionEdit->setStyleSheet(inputStyle);
    m_resourceSourceCombo->setStyleSheet(inputStyle);
    m_resourceLoaderCombo->setStyleSheet(inputStyle);
    m_resourceSearchButton->setStyleSheet(btnStyle);
    m_resourceSearchButton->setCursor(Qt::PointingHandCursor);
    m_resourceSearchButton->setMinimumHeight(36);

    searchLayout->addWidget(new QLabel("关键词"), 0, 0);
    searchLayout->addWidget(m_resourceQueryEdit, 0, 1, 1, 3);
    searchLayout->addWidget(new QLabel("来源"), 1, 0);
    searchLayout->addWidget(m_resourceSourceCombo, 1, 1);
    searchLayout->addWidget(new QLabel("MC 版本"), 1, 2);
    searchLayout->addWidget(m_resourceGameVersionEdit, 1, 3);
    searchLayout->addWidget(new QLabel("加载器"), 2, 0);
    searchLayout->addWidget(m_resourceLoaderCombo, 2, 1);
    searchLayout->addWidget(m_resourceSearchButton, 2, 3);

    searchLayoutOuter->addWidget(searchBox);

    m_modResultsScroll = new QScrollArea(m_modsSearchPage);
    m_modResultsScroll->setWidgetResizable(true);
    m_modResultsScroll->setFrameShape(QFrame::NoFrame);
    m_modResultsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_modResultsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_modResultsScroll->setStyleSheet(
        "QScrollArea { background-color: transparent; border: none; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 8px; margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #d1d5db; min-height: 24px; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #9ca3af; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );

    auto *resultsContainer = new QWidget(m_modResultsScroll);
    m_modResultsLayout = new QVBoxLayout(resultsContainer);
    m_modResultsLayout->setContentsMargins(0, 0, 0, 0);
    m_modResultsLayout->setSpacing(8);
    m_modResultsLayout->addStretch();
    m_modResultsScroll->setWidget(resultsContainer);

    searchLayoutOuter->addWidget(m_modResultsScroll, 1);

    m_modsSubStack->addWidget(m_modsSearchPage);

    m_modsDetailPage = new QWidget(m_modsSubStack);
    m_modsSubStack->addWidget(m_modsDetailPage);

    connect(m_modsBackButton, &QPushButton::clicked, this, [this] {
        animateButton(m_modsBackButton);
        m_modsSubStack->setCurrentWidget(m_modsSearchPage);
        m_modsBackButton->setVisible(false);
    });

    connect(m_resourceSearchButton, &QPushButton::clicked, this, [this] {
        animateButton(m_resourceSearchButton);
        const QString targetType = "mod";
        QString error;
        QJsonArray result;
        if (m_resourceSourceCombo->currentText() == "Modrinth") {
            result = m_core.searchModrinth(
                m_resourceQueryEdit->text(),
                m_resourceGameVersionEdit->text(),
                targetType,
                &error);
            if (!error.isEmpty()) { appendLog(QString("搜索模组失败: %1").arg(error)); showError(this, error); return; }
        } else {
            result = m_core.searchCurseForge(
                m_resourceQueryEdit->text(),
                m_resourceGameVersionEdit->text(),
                &error);
            if (!error.isEmpty()) { appendLog(QString("搜索模组失败: %1").arg(error)); showError(this, error); return; }
        }
        populateModSearchResults(result, m_resourceSourceCombo->currentText(), m_modResultsLayout);
    });

    return page;
}

QWidget *MainWindow::buildDownloadModpacksPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *pageTitle = new QLabel("下载整合包", page);
    pageTitle->setStyleSheet("font-size: 18px; font-weight: 700; margin-bottom: 4px;");
    layout->addWidget(pageTitle);

    auto *searchBox = new QGroupBox("Modrinth / CurseForge 整合包搜索", page);
    auto *searchLayout = new QGridLayout(searchBox);

    auto *queryEdit = new QLineEdit(searchBox);
    auto *gameVersionEdit = new QLineEdit(searchBox);
    auto *sourceCombo = new QComboBox(searchBox);
    auto *searchButton = new QPushButton("🔍 搜索", searchBox);
    auto *downloadButton = new QPushButton("⬇️ 下载选中项", searchBox);
    auto *viewDetailsBtn = new QPushButton("ℹ️ 查看详情", searchBox);
    auto *resultsTable = new QTableWidget(page);

    sourceCombo->addItems({"Modrinth", "CurseForge"});
    gameVersionEdit->setPlaceholderText("例如 1.20.1");
    queryEdit->setPlaceholderText("例如 All the Mods / FTB");

    resultsTable->setColumnCount(4);
    resultsTable->setHorizontalHeaderLabels({"名称", "版本", "来源", "附加信息"});
    resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    searchLayout->addWidget(new QLabel("关键词"), 0, 0);
    searchLayout->addWidget(queryEdit, 0, 1, 1, 3);
    searchLayout->addWidget(new QLabel("来源"), 1, 0);
    searchLayout->addWidget(sourceCombo, 1, 1);
    searchLayout->addWidget(new QLabel("MC 版本"), 1, 2);
    searchLayout->addWidget(gameVersionEdit, 1, 3);
    searchLayout->addWidget(viewDetailsBtn, 2, 0, 1, 2);
    searchLayout->addWidget(searchButton, 2, 2);
    searchLayout->addWidget(downloadButton, 2, 3);

    layout->addWidget(searchBox);
    layout->addWidget(resultsTable, 1);

    connect(searchButton, &QPushButton::clicked, this, [this, queryEdit, gameVersionEdit, sourceCombo, resultsTable] {
        animateButton(qobject_cast<QPushButton *>(sender()));
        QString error;
        QJsonArray result;
        const QString targetType = "modpack";
        if (sourceCombo->currentText() == "Modrinth") {
            result = m_core.searchModrinth(queryEdit->text(), gameVersionEdit->text(), targetType, &error);
            if (!error.isEmpty()) { appendLog(QString("搜索整合包失败: %1").arg(error)); showError(this, error); return; }
            populateResultsTable(result, "Modrinth");
            for (int r = 0; r < resultsTable->rowCount(); ++r) { for (int c = 0; c < resultsTable->columnCount(); ++c) {
                auto *src = m_resourceResultsTable->item(r, c);
                if (src) {
                    auto *dst = new QTableWidgetItem(*src);
                    resultsTable->setItem(r, c, dst);
                }
            } }
        } else {
            result = m_core.searchCurseForge(queryEdit->text(), gameVersionEdit->text(), &error);
            if (!error.isEmpty()) { appendLog(QString("搜索整合包失败: %1").arg(error)); showError(this, error); return; }
            populateResultsTable(result, "CurseForge");
            resultsTable->setRowCount(0);
            for (int r = 0; r < m_resourceResultsTable->rowCount(); ++r) {
                resultsTable->insertRow(r);
                for (int c = 0; c < m_resourceResultsTable->columnCount(); ++c) {
                    auto *src = m_resourceResultsTable->item(r, c);
                    if (src) resultsTable->setItem(r, c, new QTableWidgetItem(*src));
                }
            }
        }
    });

    connect(viewDetailsBtn, &QPushButton::clicked, this, [this, resultsTable] {
        const int row = resultsTable->currentRow();
        if (row < 0) { showError(this, "请先选中要查看的整合包。"); return; }
        for (int r = 0; r < m_resourceResultsTable->rowCount(); ++r) {
            for (int c = 0; c < m_resourceResultsTable->columnCount(); ++c) {
                auto *src = resultsTable->item(r, c);
                if (src) m_resourceResultsTable->setItem(r, c, new QTableWidgetItem(*src));
            }
        }
        m_resourceResultsTable->setCurrentCell(row, 0);
        showResourceDetails();
    });

    connect(downloadButton, &QPushButton::clicked, this, [this, resultsTable, sourceCombo, gameVersionEdit] {
        animateButton(qobject_cast<QPushButton *>(sender()));
        const int row = resultsTable->currentRow();
        if (row < 0) { showError(this, "请先选中要下载的整合包。"); return; }
        QString error;
        const QString source = resultsTable->item(row, 2)->text();
        const QJsonObject payload = QJsonDocument::fromJson(
            resultsTable->item(row, 0)->data(Qt::UserRole).toByteArray()).object();
        const QString targetType = "modpack";
        if (source == "Modrinth") {
            if (!saveUiToConfig()) return;
            if (!m_core.downloadModrinthProjectWithDialog(
                    payload.value("project_id").toString(payload.value("id").toString()),
                    gameVersionEdit->text(), "", targetType, &error, this)) {
                if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                    appendLog(QString("Modrinth 整合包下载失败: %1").arg(error)); showError(this, error);
                }
                return;
            }
        } else {
            if (!saveUiToConfig()) return;
            qint64 modId = payload.value("id").toInteger();
            qint64 fileId = payload.value("mainFileId").toInteger();
            if (fileId == 0) {
                const auto indexes = payload.value("latestFilesIndexes").toArray();
                if (!indexes.isEmpty()) fileId = indexes.first().toObject().value("fileId").toInteger();
            }
            if (modId == 0 || fileId == 0) { showError(this, "CurseForge 结果里没有可用文件 ID。"); return; }
            if (!m_core.downloadCurseForgeFileWithDialog(modId, fileId, targetType, &error, this)) {
                if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                    appendLog(QString("CurseForge 整合包下载失败: %1").arg(error)); showError(this, error);
                }
                return;
            }
        }
        showInfo(this, "整合包下载完成。");
    });

    return page;
}

QWidget *MainWindow::buildDownloadResourcepacksPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *pageTitle = new QLabel("下载资源包", page);
    pageTitle->setStyleSheet("font-size: 18px; font-weight: 700; margin-bottom: 4px;");
    layout->addWidget(pageTitle);

    auto *searchBox = new QGroupBox("Modrinth / CurseForge 资源包搜索", page);
    auto *searchLayout = new QGridLayout(searchBox);

    auto *queryEdit = new QLineEdit(searchBox);
    auto *gameVersionEdit = new QLineEdit(searchBox);
    auto *sourceCombo = new QComboBox(searchBox);
    auto *searchButton = new QPushButton("🔍 搜索", searchBox);
    auto *downloadButton = new QPushButton("⬇️ 下载选中项", searchBox);
    auto *viewDetailsBtn = new QPushButton("ℹ️ 查看详情", searchBox);
    auto *resultsTable = new QTableWidget(page);

    sourceCombo->addItems({"Modrinth", "CurseForge"});
    gameVersionEdit->setPlaceholderText("例如 1.20.1");
    queryEdit->setPlaceholderText("资源包名称或关键词");

    resultsTable->setColumnCount(4);
    resultsTable->setHorizontalHeaderLabels({"名称", "版本", "来源", "附加信息"});
    resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    searchLayout->addWidget(new QLabel("关键词"), 0, 0);
    searchLayout->addWidget(queryEdit, 0, 1, 1, 3);
    searchLayout->addWidget(new QLabel("来源"), 1, 0);
    searchLayout->addWidget(sourceCombo, 1, 1);
    searchLayout->addWidget(new QLabel("MC 版本"), 1, 2);
    searchLayout->addWidget(gameVersionEdit, 1, 3);
    searchLayout->addWidget(viewDetailsBtn, 2, 0, 1, 2);
    searchLayout->addWidget(searchButton, 2, 2);
    searchLayout->addWidget(downloadButton, 2, 3);

    layout->addWidget(searchBox);
    layout->addWidget(resultsTable, 1);

    connect(searchButton, &QPushButton::clicked, this, [this, queryEdit, gameVersionEdit, sourceCombo, resultsTable] {
        animateButton(qobject_cast<QPushButton *>(sender()));
        QString error;
        QJsonArray result;
        const QString targetType = "resourcepack";
        if (sourceCombo->currentText() == "Modrinth") {
            result = m_core.searchModrinth(queryEdit->text(), gameVersionEdit->text(), targetType, &error);
            if (!error.isEmpty()) { appendLog(QString("搜索资源包失败: %1").arg(error)); showError(this, error); return; }
            populateResultsTable(result, "Modrinth");
            resultsTable->setRowCount(0);
            for (int r = 0; r < m_resourceResultsTable->rowCount(); ++r) {
                resultsTable->insertRow(r);
                for (int c = 0; c < m_resourceResultsTable->columnCount(); ++c) {
                    auto *src = m_resourceResultsTable->item(r, c);
                    if (src) resultsTable->setItem(r, c, new QTableWidgetItem(*src));
                }
            }
        } else {
            result = m_core.searchCurseForge(queryEdit->text(), gameVersionEdit->text(), &error);
            if (!error.isEmpty()) { appendLog(QString("搜索资源包失败: %1").arg(error)); showError(this, error); return; }
            populateResultsTable(result, "CurseForge");
            resultsTable->setRowCount(0);
            for (int r = 0; r < m_resourceResultsTable->rowCount(); ++r) {
                resultsTable->insertRow(r);
                for (int c = 0; c < m_resourceResultsTable->columnCount(); ++c) {
                    auto *src = m_resourceResultsTable->item(r, c);
                    if (src) resultsTable->setItem(r, c, new QTableWidgetItem(*src));
                }
            }
        }
    });

    connect(viewDetailsBtn, &QPushButton::clicked, this, [this, resultsTable] {
        const int row = resultsTable->currentRow();
        if (row < 0) { showError(this, "请先选中要查看的资源包。"); return; }
        m_resourceResultsTable->setRowCount(0);
        for (int r = 0; r < resultsTable->rowCount(); ++r) {
            m_resourceResultsTable->insertRow(r);
            for (int c = 0; c < resultsTable->columnCount(); ++c) {
                auto *src = resultsTable->item(r, c);
                if (src) m_resourceResultsTable->setItem(r, c, new QTableWidgetItem(*src));
            }
        }
        m_resourceResultsTable->setCurrentCell(row, 0);
        showResourceDetails();
    });

    connect(downloadButton, &QPushButton::clicked, this, [this, resultsTable, sourceCombo, gameVersionEdit] {
        animateButton(qobject_cast<QPushButton *>(sender()));
        const int row = resultsTable->currentRow();
        if (row < 0) { showError(this, "请先选中要下载的资源包。"); return; }
        QString error;
        const QString source = resultsTable->item(row, 2)->text();
        const QJsonObject payload = QJsonDocument::fromJson(
            resultsTable->item(row, 0)->data(Qt::UserRole).toByteArray()).object();
        const QString targetType = "resourcepack";
        if (source == "Modrinth") {
            if (!saveUiToConfig()) return;
            if (!m_core.downloadModrinthProjectWithDialog(
                    payload.value("project_id").toString(payload.value("id").toString()),
                    gameVersionEdit->text(), "", targetType, &error, this)) {
                if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                    appendLog(QString("Modrinth 资源包下载失败: %1").arg(error)); showError(this, error);
                }
                return;
            }
        } else {
            if (!saveUiToConfig()) return;
            qint64 modId = payload.value("id").toInteger();
            qint64 fileId = payload.value("mainFileId").toInteger();
            if (fileId == 0) {
                const auto indexes = payload.value("latestFilesIndexes").toArray();
                if (!indexes.isEmpty()) fileId = indexes.first().toObject().value("fileId").toInteger();
            }
            if (modId == 0 || fileId == 0) { showError(this, "CurseForge 结果里没有可用文件 ID。"); return; }
            if (!m_core.downloadCurseForgeFileWithDialog(modId, fileId, targetType, &error, this)) {
                if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                    appendLog(QString("CurseForge 资源包下载失败: %1").arg(error)); showError(this, error);
                }
                return;
            }
        }
        showInfo(this, "资源包下载完成。");
    });

    return page;
}

QWidget *MainWindow::buildDownloadDatapacksPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *pageTitle = new QLabel("下载数据包", page);
    pageTitle->setStyleSheet("font-size: 18px; font-weight: 700; margin-bottom: 4px;");
    layout->addWidget(pageTitle);

    auto *searchBox = new QGroupBox("Modrinth 数据包搜索 (CurseForge 暂不支持数据包)", page);
    auto *searchLayout = new QGridLayout(searchBox);

    auto *queryEdit = new QLineEdit(searchBox);
    auto *gameVersionEdit = new QLineEdit(searchBox);
    auto *searchButton = new QPushButton("🔍 搜索", searchBox);
    auto *downloadButton = new QPushButton("⬇️ 下载选中项", searchBox);
    auto *viewDetailsBtn = new QPushButton("ℹ️ 查看详情", searchBox);
    auto *resultsTable = new QTableWidget(page);

    gameVersionEdit->setPlaceholderText("例如 1.20.1");
    queryEdit->setPlaceholderText("数据包名称或关键词（如 Tinkers, Terralith）");

    resultsTable->setColumnCount(4);
    resultsTable->setHorizontalHeaderLabels({"名称", "版本", "来源", "附加信息"});
    resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    searchLayout->addWidget(new QLabel("关键词"), 0, 0);
    searchLayout->addWidget(queryEdit, 0, 1, 1, 3);
    searchLayout->addWidget(new QLabel("MC 版本"), 1, 0);
    searchLayout->addWidget(gameVersionEdit, 1, 1);
    searchLayout->addWidget(viewDetailsBtn, 1, 2);
    searchLayout->addWidget(searchButton, 1, 3);
    searchLayout->addWidget(downloadButton, 2, 2, 1, 2);

    layout->addWidget(searchBox);
    layout->addWidget(resultsTable, 1);

    connect(searchButton, &QPushButton::clicked, this, [this, queryEdit, gameVersionEdit, resultsTable] {
        animateButton(qobject_cast<QPushButton *>(sender()));
        QString error;
        QJsonArray result;
        const QString targetType = "datapack";
        result = m_core.searchModrinth(queryEdit->text(), gameVersionEdit->text(), targetType, &error);
        if (!error.isEmpty()) { appendLog(QString("搜索数据包失败: %1").arg(error)); showError(this, error); return; }
        populateResultsTable(result, "Modrinth");
        resultsTable->setRowCount(0);
        for (int r = 0; r < m_resourceResultsTable->rowCount(); ++r) {
            resultsTable->insertRow(r);
            for (int c = 0; c < m_resourceResultsTable->columnCount(); ++c) {
                auto *src = m_resourceResultsTable->item(r, c);
                if (src) resultsTable->setItem(r, c, new QTableWidgetItem(*src));
            }
        }
    });

    connect(viewDetailsBtn, &QPushButton::clicked, this, [this, resultsTable] {
        const int row = resultsTable->currentRow();
        if (row < 0) { showError(this, "请先选中要查看的数据包。"); return; }
        m_resourceResultsTable->setRowCount(0);
        for (int r = 0; r < resultsTable->rowCount(); ++r) {
            m_resourceResultsTable->insertRow(r);
            for (int c = 0; c < resultsTable->columnCount(); ++c) {
                auto *src = resultsTable->item(r, c);
                if (src) m_resourceResultsTable->setItem(r, c, new QTableWidgetItem(*src));
            }
        }
        m_resourceResultsTable->setCurrentCell(row, 0);
        showResourceDetails();
    });

    connect(downloadButton, &QPushButton::clicked, this, [this, resultsTable, gameVersionEdit] {
        animateButton(qobject_cast<QPushButton *>(sender()));
        const int row = resultsTable->currentRow();
        if (row < 0) { showError(this, "请先选中要下载的数据包。"); return; }
        QString error;
        const QJsonObject payload = QJsonDocument::fromJson(
            resultsTable->item(row, 0)->data(Qt::UserRole).toByteArray()).object();
        const QString targetType = "datapack";
        if (!saveUiToConfig()) return;
        if (!m_core.downloadModrinthProjectWithDialog(
                payload.value("project_id").toString(payload.value("id").toString()),
                gameVersionEdit->text(), "", targetType, &error, this)) {
            if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                appendLog(QString("Modrinth 数据包下载失败: %1").arg(error)); showError(this, error);
            }
            return;
        }
        showInfo(this, "数据包下载完成。");
    });

    return page;
}

QWidget *MainWindow::buildHomeTab()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *navWidget = new QFrame(page);
    navWidget->setFixedWidth(220);
    navWidget->setObjectName("homeNavPanel");
    navWidget->setStyleSheet(
        "#homeNavPanel {"
        "  background-color: #f9fafb;"
        "  border-right: 1px solid #e5e7eb;"
        "}"
    );
    auto *navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(16, 20, 16, 20);
    navLayout->setSpacing(6);

    auto *navTitle = new QLabel("启动设置", navWidget);
    navTitle->setStyleSheet("font-size: 16px; font-weight: 700; margin-bottom: 8px;");
    navLayout->addWidget(navTitle);

    m_homeNavGroup = new QButtonGroup(this);
    m_homeNavGroup->setExclusive(true);
    m_homeNavButtons.clear();

    QStringList categories = {"版本", "账户"};
    QStringList categoryIcons = {"🎮", "👤"};
    for (int i = 0; i < categories.size(); ++i) {
        auto *btn = new QPushButton(categoryIcons[i] + "  " + categories[i], navWidget);
        btn->setCheckable(true);
        btn->setFixedHeight(44);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  text-align: left; padding-left: 14px;"
            "  background: transparent; border: none; border-radius: 8px;"
            "  font-size: 14px; font-weight: 500; color: #6b7280;"
            "}"
            "QPushButton:hover { background-color: rgba(59, 130, 246, 0.08); color: #3b82f6; }"
            "QPushButton:checked {"
            "  background-color: rgba(59, 130, 246, 0.12); color: #3b82f6;"
            "  font-weight: 600;"
            "}"
        );
        m_homeNavGroup->addButton(btn, i);
        m_homeNavButtons.append(btn);
        navLayout->addWidget(btn);
    }
    navLayout->addStretch();

    auto *rightContainer = new QWidget(page);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_homeStackedWidget = new QStackedWidget(rightContainer);
    m_homeStackedWidget->setFrameShape(QFrame::NoFrame);

    auto *versionsPage = new QWidget(m_homeStackedWidget);
    {
        auto *root = new QVBoxLayout(versionsPage);
        root->setContentsMargins(0, 0, 0, 0);

        auto *scroll = new QScrollArea(versionsPage);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto *content = new QWidget(scroll);
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(24, 24, 24, 24);
        contentLayout->setSpacing(18);

        auto *title = new QLabel("版本设置", content);
        title->setStyleSheet("font-size: 20px; font-weight: 700; margin-bottom: 4px;");
        contentLayout->addWidget(title);

        auto *box = new QGroupBox("选择游戏版本", content);
        box->setStyleSheet(groupBoxStyle());
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(20, 24, 20, 20);
        boxLayout->setSpacing(14);

        m_homeVersionTypeCombo = new QComboBox(box);
        m_homeVersionCombo = new QComboBox(box);

        m_homeVersionTypeCombo->addItem("所有版本", "all");
        m_homeVersionTypeCombo->addItem("正式版 (Release)", "release");
        m_homeVersionTypeCombo->addItem("快照版 (Snapshot)", "snapshot");
        m_homeVersionTypeCombo->addItem("远古版 (Beta)", "old_beta");
        m_homeVersionTypeCombo->addItem("远古版 (Alpha)", "old_alpha");
        m_homeVersionTypeCombo->addItem("愚人节版", "april_fools");

        auto *typeLabel = new QLabel("版本类型筛选");
        typeLabel->setStyleSheet(labelStyle());
        boxLayout->addWidget(typeLabel);
        boxLayout->addWidget(m_homeVersionTypeCombo);

        auto *verLabel = new QLabel("选择版本");
        verLabel->setStyleSheet(labelStyle());
        boxLayout->addWidget(verLabel);
        boxLayout->addWidget(m_homeVersionCombo);

        contentLayout->addWidget(box);
        contentLayout->addStretch();
        scroll->setWidget(content);
        root->addWidget(scroll);
    }

    auto *accountsPage = new QWidget(m_homeStackedWidget);
    {
        auto *root = new QVBoxLayout(accountsPage);
        root->setContentsMargins(0, 0, 0, 0);

        auto *scroll = new QScrollArea(accountsPage);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto *content = new QWidget(scroll);
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(24, 24, 24, 24);
        contentLayout->setSpacing(18);

        auto *title = new QLabel("账户设置", content);
        title->setStyleSheet("font-size: 20px; font-weight: 700; margin-bottom: 4px;");
        contentLayout->addWidget(title);

        auto *box = new QGroupBox("选择账户", content);
        box->setStyleSheet(groupBoxStyle());
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(20, 24, 20, 20);
        boxLayout->setSpacing(14);

        m_homeAccountCombo = new QComboBox(box);
        auto *accLabel = new QLabel("启动游戏使用的账户");
        accLabel->setStyleSheet(labelStyle());
        boxLayout->addWidget(accLabel);
        boxLayout->addWidget(m_homeAccountCombo);

        auto *hint = new QLabel(
            "提示：可以在左侧「账户」页面添加、管理更多的离线账户或 Microsoft 正版账户。",
            box);
        hint->setWordWrap(true);
        hint->setStyleSheet("font-size: 12px; color: #6b7280; padding: 8px 12px; background: #f3f4f6; border-radius: 8px;");
        boxLayout->addWidget(hint);

        contentLayout->addWidget(box);
        contentLayout->addStretch();
        scroll->setWidget(content);
        root->addWidget(scroll);
    }

    m_homeStackedWidget->addWidget(versionsPage);
    m_homeStackedWidget->addWidget(accountsPage);

    for (int i = 0; i < m_homeNavButtons.size(); ++i) {
        connect(m_homeNavButtons[i], &QPushButton::clicked, this, [this, i] {
            animateButton(m_homeNavButtons[i]);
            m_homeStackedWidget->setCurrentIndex(i);
        });
    }

    auto *bottomBar = new QWidget(rightContainer);
    bottomBar->setFixedHeight(96);
    bottomBar->setStyleSheet(
        "QWidget { background-color: #ffffff; border-top: 1px solid #e5e7eb; }"
    );
    auto *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(24, 16, 24, 16);
    bottomLayout->setSpacing(16);

    auto *hintLabel = new QLabel(
        "若版本尚未安装，请前往「下载」页面下载版本后再启动游戏。",
        bottomBar);
    hintLabel->setStyleSheet("font-size: 13px; color: #6b7280;");
    hintLabel->setWordWrap(true);
    bottomLayout->addWidget(hintLabel, 1);

    auto *manageButton = new QPushButton("版本管理", bottomBar);
    manageButton->setMinimumSize(QSize(140, 52));
    manageButton->setCursor(Qt::PointingHandCursor);
    manageButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #ffffff;"
        "  color: #111827;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 14px;"
        "  font-size: 15px;"
        "  font-weight: 600;"
        "  padding: 10px 24px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #f9fafb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #f3f4f6;"
        "}"
    );
    bottomLayout->addWidget(manageButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_launchButton = new QPushButton("启动游戏", bottomBar);
    m_launchButton->setMinimumSize(QSize(200, 60));
    m_launchButton->setCursor(Qt::PointingHandCursor);
    m_launchButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #2563eb;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  padding: 10px 28px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1d4ed8;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #1e40af;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #93c5fd;"
        "  color: #dbeafe;"
        "}"
    );
    bottomLayout->addWidget(m_launchButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    rightLayout->addWidget(m_homeStackedWidget, 1);
    rightLayout->addWidget(bottomBar, 0);

    pageLayout->addWidget(navWidget);
    pageLayout->addWidget(rightContainer, 1);

    connect(m_homeVersionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_remoteVersions.isEmpty()) {
            refreshRemoteVersionsUi();
        }
    });
    connect(m_homeVersionCombo, &QComboBox::currentTextChanged, this, [this](const QString &versionId) {
        syncSelectedVersion(versionId, m_homeVersionCombo);
        refreshVersionManagementUi();
    });
    connect(m_launchButton, &QPushButton::clicked, this, [this] {
        animateButton(m_launchButton);
        LauncherConfig &cfg = m_core.configStore()->config();
        if (!m_homeAccountCombo->currentData().toString().isEmpty()) {
            cfg.selectedAccountIndex = m_homeAccountCombo->currentData().toInt();
            m_core.saveConfig(nullptr);
        }
        QString error;
        if (!m_core.launchSelectedVersionWithDialog(&error, this)) {
            if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                appendLog(QString("启动失败: %1").arg(error));
                showError(this, error);
            }
            return;
        }
        showInfo(this, "Minecraft 已开始启动。");
    });
    connect(manageButton, &QPushButton::clicked, this, [this, manageButton] {
        animateButton(manageButton);
        if (m_mainTabWidget) {
            m_mainTabWidget->setCurrentIndex(3);
        }
    });

    if (!m_homeNavButtons.isEmpty()) {
        m_homeNavButtons.first()->setChecked(true);
        m_homeStackedWidget->setCurrentIndex(0);
    }

    return page;
}

QWidget *MainWindow::buildAccountsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *overviewBox = new QGroupBox("当前账户", page);
    auto *overviewLayout = new QHBoxLayout(overviewBox);
    m_accountsOverviewCombo = new QComboBox(overviewBox);
    overviewLayout->addWidget(new QLabel("默认账户"));
    overviewLayout->addWidget(m_accountsOverviewCombo);

    auto *offlineBox = new QGroupBox("离线账户", page);
    auto *offlineLayout = new QHBoxLayout(offlineBox);
    m_offlineNameEdit = new QLineEdit(offlineBox);
    m_offlineNameEdit->setPlaceholderText("输入离线用户名");
    m_addOfflineButton = new QPushButton("添加离线账户", offlineBox);
    offlineLayout->addWidget(m_offlineNameEdit);
    offlineLayout->addWidget(m_addOfflineButton);

    auto *msaBox = new QGroupBox("正版 Microsoft 账户", page);
    auto *msaLayout = new QVBoxLayout(msaBox);
    auto *msaHint = new QLabel(
        "使用设备码登录 Microsoft 正版账户：\n"
        "1. 点击下方「设备码登录」按钮。\n"
        "2. 弹窗会显示验证码和验证地址。\n"
        "3. 手动在浏览器打开验证地址，输入验证码后完成登录和授权。\n"
        "4. 回到本窗口等待自动轮询检测，登录完成会自动提示。",
        msaBox);
    msaHint->setWordWrap(true);
    msaHint->setProperty("role", "hint");
    m_openMicrosoftDeviceButton = new QPushButton("设备码登录", msaBox);
    msaLayout->addWidget(msaHint);
    msaLayout->addWidget(m_openMicrosoftDeviceButton);

    layout->addWidget(overviewBox);
    layout->addWidget(offlineBox);
    layout->addWidget(msaBox);
    layout->addStretch();

    connect(m_addOfflineButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!m_core.addOfflineAccount(m_offlineNameEdit->text(), &error)) {
            showError(this, error);
            return;
        }
        m_offlineNameEdit->clear();
        refreshAccountsUi();
        showInfo(this, "离线账户已添加。");
    });

    connect(m_openMicrosoftDeviceButton, &QPushButton::clicked, this, [this] {
        if (!saveUiToConfig()) {
            return;
        }
        runMicrosoftDeviceLoginFlow();
    });

    return page;
}

QWidget *MainWindow::buildSettingsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *baseBox = new QGroupBox("基础配置", page);
    auto *baseLayout = new QFormLayout(baseBox);

    m_minecraftDirEdit = new QLineEdit(baseBox);
    m_javaPathEdit = new QLineEdit(baseBox);
    m_javaArgsEdit = new QLineEdit(baseBox);
    m_extraGameArgsEdit = new QLineEdit(baseBox);
    m_clientIdEdit = new QLineEdit(baseBox);
    m_curseForgeKeyEdit = new QLineEdit(baseBox);
    m_mirrorAutoRadio = new QRadioButton("自动", baseBox);
    m_mirrorOnRadio = new QRadioButton("镜像源", baseBox);
    m_mirrorOffRadio = new QRadioButton("官方源", baseBox);
    m_minMemorySpin = new QSpinBox(baseBox);
    m_maxMemorySpin = new QSpinBox(baseBox);
    m_verifyFilesCheck = new QCheckBox("启用文件校验", baseBox);
    m_versionIsolationCheck = new QCheckBox("启用版本隔离", baseBox);

    m_minMemorySpin->setRange(512, 65536);
    m_maxMemorySpin->setRange(512, 65536);
    m_curseForgeKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);

    auto *mcDirRow = new QWidget(baseBox);
    auto *mcDirLayout = new QHBoxLayout(mcDirRow);
    mcDirLayout->setContentsMargins(0, 0, 0, 0);
    auto *mcDirBrowse = new QPushButton("浏览", mcDirRow);
    mcDirLayout->addWidget(m_minecraftDirEdit);
    mcDirLayout->addWidget(mcDirBrowse);

    auto *javaRow = new QWidget(baseBox);
    auto *javaLayout = new QHBoxLayout(javaRow);
    javaLayout->setContentsMargins(0, 0, 0, 0);
    auto *javaBrowse = new QPushButton("浏览", javaRow);
    auto *javaDetect = new QPushButton("自动探测", javaRow);
    auto *javaDownload = new QPushButton("下载 Java", javaRow);
    javaLayout->addWidget(m_javaPathEdit);
    javaLayout->addWidget(javaBrowse);
    javaLayout->addWidget(javaDetect);
    javaLayout->addWidget(javaDownload);

    auto *saveButton = new QPushButton("保存设置", baseBox);

    auto *mirrorRow = new QWidget(baseBox);
    auto *mirrorLayout = new QHBoxLayout(mirrorRow);
    mirrorLayout->setContentsMargins(0, 0, 0, 0);
    mirrorLayout->addWidget(m_mirrorAutoRadio);
    mirrorLayout->addWidget(m_mirrorOnRadio);
    mirrorLayout->addWidget(m_mirrorOffRadio);
    mirrorLayout->addStretch();

    baseLayout->addRow("Minecraft 目录", mcDirRow);
    baseLayout->addRow("Java 路径", javaRow);
    baseLayout->addRow("JVM 参数", m_javaArgsEdit);
    baseLayout->addRow("附加游戏参数", m_extraGameArgsEdit);
    baseLayout->addRow("Microsoft Client ID", m_clientIdEdit);
    baseLayout->addRow("CurseForge API Key", m_curseForgeKeyEdit);
    baseLayout->addRow("镜像源选择", mirrorRow);
    baseLayout->addRow("最小内存 (MB)", m_minMemorySpin);
    baseLayout->addRow("最大内存 (MB)", m_maxMemorySpin);
    baseLayout->addRow("", m_verifyFilesCheck);
    baseLayout->addRow("", m_versionIsolationCheck);
    baseLayout->addRow("", saveButton);

    layout->addWidget(baseBox);
    layout->addStretch();

    connect(mcDirBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "选择 Minecraft 目录", m_minecraftDirEdit->text());
        if (!dir.isEmpty()) {
            m_minecraftDirEdit->setText(dir);
        }
    });

    connect(javaBrowse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, "选择 javaw.exe", m_javaPathEdit->text(), "Java (javaw.exe)");
        if (!file.isEmpty()) {
            m_javaPathEdit->setText(file);
        }
    });

    connect(javaDetect, &QPushButton::clicked, this, [this] {
        const QStringList candidates = m_core.discoverJavaExecutables();
        if (candidates.isEmpty()) {
            showError(this, "没有探测到可用 Java，启动时会尝试自动安装。");
            return;
        }
        m_javaPathEdit->setText(candidates.first());
        showInfo(this, QString("已选中 Java:\n%1").arg(candidates.first()));
    });

    connect(javaDownload, &QPushButton::clicked, this, [this] {
        downloadJava();
    });

    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (!saveUiToConfig()) {
            return;
        }
        showInfo(this, "设置已保存。");
        refreshAccountsUi();
        refreshInstalledVersionsUi();
    });

    return page;
}

QWidget *MainWindow::buildLogsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *buttonRow = new QHBoxLayout();
    auto *openLogDirButton = new QPushButton("打开日志目录", page);
    auto *copyLogPathButton = new QPushButton("复制当前日志路径", page);
    buttonRow->addWidget(openLogDirButton);
    buttonRow->addWidget(copyLogPathButton);
    buttonRow->addStretch();

    m_logsEdit = new QPlainTextEdit(page);
    m_logsEdit->setReadOnly(true);
    layout->addLayout(buttonRow);
    layout->addWidget(m_logsEdit);

    connect(openLogDirButton, &QPushButton::clicked, this, &MainWindow::openLogDirectory);
    connect(copyLogPathButton, &QPushButton::clicked, this, &MainWindow::copyCurrentLogPath);

    return page;
}

QWidget *MainWindow::buildAboutPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *topBar = new QWidget(page);
    topBar->setFixedHeight(52);
    auto *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(16, 8, 16, 8);
    topBarLayout->setSpacing(12);

    m_aboutBackButton = new QPushButton("←", topBar);
    m_aboutBackButton->setFixedSize(38, 36);
    m_aboutBackButton->setCursor(Qt::PointingHandCursor);
    m_aboutBackButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 10px;"
        "  color: #374151;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover {"
        "  background-color: #e5e7eb;"
        "  border-color: #d1d5db;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #d1d5db;"
        "}"
    );
    topBarLayout->addWidget(m_aboutBackButton);

    auto *pageTitle = new QLabel("关于", topBar);
    pageTitle->setStyleSheet("font-size: 18px; font-weight: 700; color: #111827;");
    topBarLayout->addWidget(pageTitle);
    topBarLayout->addStretch();

    layout->addWidget(topBar);

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    auto *contentPage = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(24, 20, 24, 24);
    contentLayout->setSpacing(20);

    auto *infoBox = new QGroupBox("基本信息", contentPage);
    infoBox->setStyleSheet(
        "QGroupBox {"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 12px;"
        "  margin-top: 8px;"
        "  padding-top: 16px;"
        "  background-color: #ffffff;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 16px;"
        "  padding: 0 8px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  color: #374151;"
        "}"
    );
    auto *infoLayout = new QFormLayout(infoBox);
    infoLayout->setContentsMargins(20, 20, 20, 20);
    infoLayout->setHorizontalSpacing(16);
    infoLayout->setVerticalSpacing(14);
    infoLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *appNameLabel = new QLabel("Kill - Minecraft Launcher", infoBox);
    appNameLabel->setStyleSheet("font-size: 15px; font-weight: 600; color: #111827;");
    infoLayout->addRow("程序名称：", appNameLabel);

    auto *versionLabel = new QLabel("1.4", infoBox);
    versionLabel->setStyleSheet("font-size: 14px; color: #111827;");
    infoLayout->addRow("当前版本：", versionLabel);

    auto *authorLabel = new QLabel("dyz131005", infoBox);
    authorLabel->setStyleSheet("font-size: 14px; color: #111827;");
    infoLayout->addRow("作者：", authorLabel);

    auto *repoRow = new QWidget(infoBox);
    auto *repoRowLayout = new QHBoxLayout(repoRow);
    repoRowLayout->setContentsMargins(0, 0, 0, 0);
    repoRowLayout->setSpacing(10);

    auto *repoLabel = new QLabel("https://github.com/dyz131005/", repoRow);
    repoLabel->setStyleSheet("font-size: 14px; color: #2563eb;");
    repoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *openRepoButton = new QPushButton("打开 GitHub 仓库", repoRow);
    openRepoButton->setCursor(Qt::PointingHandCursor);
    openRepoButton->setStyleSheet(
        "QPushButton {"
        "  padding: 6px 14px;"
        "  background-color: #3b82f6;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2563eb;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #1d4ed8;"
        "}"
    );
    connect(openRepoButton, &QPushButton::clicked, this, [openRepoButton, this] {
        animateButton(openRepoButton);
        QDesktopServices::openUrl(QUrl("https://github.com/dyz131005/"));
    });

    repoRowLayout->addWidget(repoLabel, 1);
    repoRowLayout->addWidget(openRepoButton, 0, Qt::AlignRight);
    infoLayout->addRow("GitHub 仓库：", repoRow);

    contentLayout->addWidget(infoBox);

    auto *changelogBox = new QGroupBox("更新日志", contentPage);
    changelogBox->setStyleSheet(
        "QGroupBox {"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 12px;"
        "  margin-top: 8px;"
        "  padding-top: 16px;"
        "  background-color: #ffffff;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 16px;"
        "  padding: 0 8px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  color: #374151;"
        "}"
    );
    auto *changelogLayout = new QVBoxLayout(changelogBox);
    changelogLayout->setContentsMargins(16, 20, 16, 16);

    m_aboutUpdateLogEdit = new QPlainTextEdit(changelogBox);
    m_aboutUpdateLogEdit->setReadOnly(true);
    m_aboutUpdateLogEdit->setMinimumHeight(260);
    m_aboutUpdateLogEdit->setStyleSheet(
        "QPlainTextEdit {"
        "  background-color: #f9fafb;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 8px;"
        "  padding: 12px;"
        "  font-family: Consolas, 'Courier New', 'Microsoft YaHei', monospace;"
        "  font-size: 13px;"
        "  color: #111827;"
        "}"
    );

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString logPath = exeDir + QDir::separator() + "updatelog.txt";
    QFile logFile(logPath);
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&logFile);
        m_aboutUpdateLogEdit->setPlainText(stream.readAll());
        logFile.close();
    } else {
        m_aboutUpdateLogEdit->setPlainText(QString(
            "（未找到 %1\n"
            "请将更新日志以 UTF-8 编码保存为 updatelog.txt 放在程序同目录下。）"
        ).arg(QDir::toNativeSeparators(logPath)));
    }

    changelogLayout->addWidget(m_aboutUpdateLogEdit);
    contentLayout->addWidget(changelogBox);

    contentLayout->addStretch();
    scrollArea->setWidget(contentPage);
    layout->addWidget(scrollArea, 1);

    connect(m_aboutBackButton, &QPushButton::clicked, this, [this] {
        animateButton(m_aboutBackButton);
        leaveAboutPage();
    });

    return page;
}

void MainWindow::showAboutPage()
{
    if (!m_mainStackedWidget || !m_mainTabsPage || !m_aboutPage || !m_mainTabWidget) {
        return;
    }
    m_mainTabBeforeAbout = m_mainTabWidget->currentIndex();
    m_mainStackedWidget->setCurrentWidget(m_aboutPage);
}

void MainWindow::leaveAboutPage()
{
    if (!m_mainStackedWidget || !m_mainTabsPage || !m_mainTabWidget) {
        return;
    }
    m_mainStackedWidget->setCurrentWidget(m_mainTabsPage);
    if (m_mainTabBeforeAbout >= 0 && m_mainTabBeforeAbout < m_mainTabWidget->count()) {
        m_mainTabWidget->setCurrentIndex(m_mainTabBeforeAbout);
    }
}

void MainWindow::loadConfigToUi()
{
    const LauncherConfig &config = m_core.configStore()->config();
    m_minecraftDirEdit->setText(config.minecraftDir);
    m_javaPathEdit->setText(config.javaPath);
    m_javaArgsEdit->setText(config.javaArgs);
    m_extraGameArgsEdit->setText(config.extraGameArgs);
    m_clientIdEdit->setText(config.microsoftClientId);
    m_curseForgeKeyEdit->setText(config.curseForgeApiKey);
    m_minMemorySpin->setValue(config.minMemoryMb);
    m_maxMemorySpin->setValue(config.maxMemoryMb);
    m_verifyFilesCheck->setChecked(config.verifyFiles);
    m_versionIsolationCheck->setChecked(config.versionIsolation);

    {
        const QString mode = config.mirrorMode.trimmed().toLower();
        if (mode == "mirror") {
            m_mirrorOnRadio->setChecked(true);
        } else if (mode == "official") {
            m_mirrorOffRadio->setChecked(true);
        } else {
            m_mirrorAutoRadio->setChecked(true);
        }
    }

    refreshAccountsUi();
    refreshInstalledVersionsUi();
    refreshVersionManagementUi();
    appendLog("配置已加载");
}

bool MainWindow::saveUiToConfig()
{
    LauncherConfig &config = m_core.configStore()->config();
    config.minecraftDir = m_minecraftDirEdit->text().trimmed();
    config.javaPath = m_javaPathEdit->text().trimmed();
    config.javaArgs = m_javaArgsEdit->text().trimmed();
    config.extraGameArgs = m_extraGameArgsEdit->text().trimmed();
    config.microsoftClientId = m_clientIdEdit->text().trimmed();
    config.curseForgeApiKey = m_curseForgeKeyEdit->text().trimmed();
    if (m_mirrorOnRadio->isChecked()) {
        config.mirrorMode = "mirror";
        config.mirrorSource = "bmclapi";
    } else if (m_mirrorOffRadio->isChecked()) {
        config.mirrorMode = "official";
        config.mirrorSource = "mojang";
    } else {
        config.mirrorMode = "auto";
        config.mirrorSource = "bmclapi";
    }
    config.minMemoryMb = m_minMemorySpin->value();
    config.maxMemoryMb = m_maxMemorySpin->value();
    config.verifyFiles = m_verifyFilesCheck->isChecked();
    config.versionIsolation = m_versionIsolationCheck->isChecked();
    config.selectedVersion = currentManagedVersion();
    config.selectedAccountIndex = m_homeAccountCombo->currentData().toInt();

    if (config.minecraftDir.isEmpty()) {
        showError(this, "Minecraft 目录不能为空。");
        return false;
    }
    if (config.maxMemoryMb < config.minMemoryMb) {
        showError(this, "最大内存不能小于最小内存。");
        return false;
    }
    if (!currentManagedVersion().isEmpty() && !saveCurrentVersionProfile()) {
        return false;
    }

    QString error;
    if (!m_core.saveConfig(&error)) {
        showError(this, error);
        return false;
    }
    return true;
}

void MainWindow::refreshAccountsUi()
{
    const LauncherConfig &config = m_core.configStore()->config();
    m_homeAccountCombo->clear();
    m_accountsOverviewCombo->clear();

    for (int i = 0; i < config.accounts.size(); ++i) {
        const Account &account = config.accounts.at(i);
        m_homeAccountCombo->addItem(account.displayName(), i);
        m_accountsOverviewCombo->addItem(account.displayName(), i);
    }

    const int index = config.selectedAccountIndex >= 0 ? config.selectedAccountIndex : 0;
    if (m_homeAccountCombo->count() > 0) {
        m_homeAccountCombo->setCurrentIndex(qBound(0, index, m_homeAccountCombo->count() - 1));
        m_accountsOverviewCombo->setCurrentIndex(qBound(0, index, m_accountsOverviewCombo->count() - 1));
    }
}

void MainWindow::refreshInstalledVersionsUi()
{
    const QString currentHome = m_homeVersionCombo ? m_homeVersionCombo->currentText() : QString();
    const QString currentManage = m_manageVersionCombo ? m_manageVersionCombo->currentText() : QString();
    const LauncherConfig &config = m_core.configStore()->config();
    const QStringList versions = m_core.listInstalledVersions();

    if (m_homeVersionCombo) {
        const QSignalBlocker blocker(m_homeVersionCombo);
        m_homeVersionCombo->clear();
        m_homeVersionCombo->addItems(versions);
    }
    if (m_manageVersionCombo) {
        const QSignalBlocker blocker(m_manageVersionCombo);
        m_manageVersionCombo->clear();
        m_manageVersionCombo->addItems(versions);
    }

    QString selected = config.selectedVersion;
    if (!versions.contains(selected)) {
        selected = versions.contains(currentManage) ? currentManage : currentHome;
    }
    if (!versions.contains(selected) && !versions.isEmpty()) {
        selected = versions.first();
    }

    if (m_homeVersionCombo) {
        const QSignalBlocker blocker(m_homeVersionCombo);
        const int index = m_homeVersionCombo->findText(selected);
        if (index >= 0) {
            m_homeVersionCombo->setCurrentIndex(index);
        }
    }
    if (m_manageVersionCombo) {
        const QSignalBlocker blocker(m_manageVersionCombo);
        const int index = m_manageVersionCombo->findText(selected);
        if (index >= 0) {
            m_manageVersionCombo->setCurrentIndex(index);
        }
    }
    syncSelectedVersion(selected);
}

void MainWindow::syncSelectedVersion(const QString &versionId, QComboBox *source)
{
    m_core.configStore()->config().selectedVersion = versionId;

    if (m_homeVersionCombo && source != m_homeVersionCombo) {
        const QSignalBlocker blocker(m_homeVersionCombo);
        const int index = m_homeVersionCombo->findText(versionId);
        if (index >= 0) {
            m_homeVersionCombo->setCurrentIndex(index);
        }
    }
    if (m_manageVersionCombo && source != m_manageVersionCombo) {
        const QSignalBlocker blocker(m_manageVersionCombo);
        const int index = m_manageVersionCombo->findText(versionId);
        if (index >= 0) {
            m_manageVersionCombo->setCurrentIndex(index);
        }
    }
}

QString MainWindow::currentManagedVersion() const
{
    if (m_manageVersionCombo && !m_manageVersionCombo->currentText().isEmpty()) {
        return m_manageVersionCombo->currentText();
    }
    if (m_homeVersionCombo) {
        return m_homeVersionCombo->currentText();
    }
    return {};
}

void MainWindow::updateVersionProfileEditorState()
{
    const bool isolated = m_manageIsolationCheck && m_manageIsolationCheck->isChecked();
    const QList<QWidget *> widgets = {
        m_manageJavaPathEdit,
        m_manageJavaArgsEdit,
        m_manageGameArgsEdit,
        m_manageMinMemorySpin,
        m_manageMaxMemorySpin
    };
    for (QWidget *widget : widgets) {
        if (widget) {
            widget->setEnabled(isolated);
        }
    }
}

void MainWindow::loadVersionProfileToUi()
{
    const QString versionId = currentManagedVersion();
    if (versionId.isEmpty()) {
        if (m_manageIsolationCheck) {
            m_manageIsolationCheck->setChecked(false);
        }
        if (m_manageJavaPathEdit) m_manageJavaPathEdit->clear();
        if (m_manageJavaArgsEdit) m_manageJavaArgsEdit->clear();
        if (m_manageGameArgsEdit) m_manageGameArgsEdit->clear();
        if (m_manageMinMemorySpin) m_manageMinMemorySpin->setValue(1024);
        if (m_manageMaxMemorySpin) m_manageMaxMemorySpin->setValue(4096);
        if (m_manageInstanceDirLabel) m_manageInstanceDirLabel->setText("-");
        if (m_manageModsDirLabel) m_manageModsDirLabel->setText("-");
        if (m_manageWorldsDirLabel) m_manageWorldsDirLabel->setText("-");
        if (m_manageBaseVersionLabel) m_manageBaseVersionLabel->setText("-");
        updateVersionProfileEditorState();
        return;
    }

    const LauncherVersionProfile profile = m_core.effectiveVersionProfile(versionId);
    if (m_manageIsolationCheck) {
        const QSignalBlocker blocker(m_manageIsolationCheck);
        m_manageIsolationCheck->setChecked(profile.versionIsolation);
    }
    if (m_manageJavaPathEdit) m_manageJavaPathEdit->setText(profile.javaPath);
    if (m_manageJavaArgsEdit) m_manageJavaArgsEdit->setText(profile.javaArgs);
    if (m_manageGameArgsEdit) m_manageGameArgsEdit->setText(profile.extraGameArgs);
    if (m_manageMinMemorySpin) m_manageMinMemorySpin->setValue(profile.minMemoryMb);
    if (m_manageMaxMemorySpin) m_manageMaxMemorySpin->setValue(profile.maxMemoryMb);
    if (m_manageInstanceDirLabel) m_manageInstanceDirLabel->setText(QDir::toNativeSeparators(m_core.versionInstanceDirectory(versionId)));
    if (m_manageModsDirLabel) m_manageModsDirLabel->setText(QDir::toNativeSeparators(m_core.versionModsDirectory(versionId)));
    if (m_manageWorldsDirLabel) m_manageWorldsDirLabel->setText(QDir::toNativeSeparators(m_core.versionWorldsDirectory(versionId)));
    if (m_manageBaseVersionLabel) m_manageBaseVersionLabel->setText(m_core.versionBaseGameVersion(versionId));
    updateVersionProfileEditorState();
}

bool MainWindow::saveCurrentVersionProfile()
{
    const QString versionId = currentManagedVersion();
    if (versionId.isEmpty()) {
        showError(this, "请先选择要保存配置的版本。");
        return false;
    }
    if (m_manageMaxMemorySpin->value() < m_manageMinMemorySpin->value()) {
        showError(this, "当前版本的最大内存不能小于最小内存。");
        return false;
    }

    LauncherVersionProfile profile;
    profile.versionIsolation = m_manageIsolationCheck->isChecked();
    profile.javaPath = m_manageJavaPathEdit->text().trimmed();
    profile.javaArgs = m_manageJavaArgsEdit->text().trimmed();
    profile.extraGameArgs = m_manageGameArgsEdit->text().trimmed();
    profile.minMemoryMb = m_manageMinMemorySpin->value();
    profile.maxMemoryMb = m_manageMaxMemorySpin->value();

    QString error;
    if (!m_core.saveVersionProfile(versionId, profile, &error)) {
        showError(this, error);
        return false;
    }
    return true;
}

void MainWindow::refreshVersionManagementUi()
{
    loadVersionProfileToUi();
    if (m_manageAvailableLoadersTable) {
        m_manageAvailableLoadersTable->setRowCount(0);
    }
    refreshVersionModsUi();
    refreshVersionWorldsUi();
    refreshInstalledLoadersUi();
}

void MainWindow::refreshVersionModsUi()
{
    if (!m_manageModsTable) {
        return;
    }

    const QString versionId = currentManagedVersion();
    m_manageModsTable->setRowCount(0);
    if (versionId.isEmpty()) {
        return;
    }

    const QJsonArray mods = m_core.listVersionMods(versionId);
    for (const QJsonValue &value : mods) {
        const QJsonObject object = value.toObject();
        const int row = m_manageModsTable->rowCount();
        m_manageModsTable->insertRow(row);

        auto *nameItem = new QTableWidgetItem(object.value("displayName").toString());
        nameItem->setData(Qt::UserRole, object.value("fileName").toString());
        m_manageModsTable->setItem(row, 0, nameItem);

        auto *statusItem = new QTableWidgetItem(object.value("enabled").toBool() ? "已启用" : "已禁用");
        statusItem->setData(Qt::UserRole, object.value("enabled").toBool());
        m_manageModsTable->setItem(row, 1, statusItem);
        m_manageModsTable->setItem(row, 2, new QTableWidgetItem(object.value("scope").toString() == "version" ? "当前版本" : "全局"));
        m_manageModsTable->setItem(row, 3, new QTableWidgetItem(object.value("modified").toString()));
    }
}

void MainWindow::refreshVersionWorldsUi()
{
    if (!m_manageWorldsTable) {
        return;
    }

    const QString versionId = currentManagedVersion();
    m_manageWorldsTable->setRowCount(0);
    if (versionId.isEmpty()) {
        return;
    }

    const QJsonArray worlds = m_core.listVersionWorlds(versionId);
    for (const QJsonValue &value : worlds) {
        const QJsonObject object = value.toObject();
        const int row = m_manageWorldsTable->rowCount();
        m_manageWorldsTable->insertRow(row);

        auto *nameItem = new QTableWidgetItem(object.value("name").toString());
        nameItem->setData(Qt::UserRole, object.value("name").toString());
        m_manageWorldsTable->setItem(row, 0, nameItem);
        m_manageWorldsTable->setItem(row, 1, new QTableWidgetItem(object.value("modified").toString()));
        m_manageWorldsTable->setItem(row, 2, new QTableWidgetItem(object.value("path").toString()));
    }
}

void MainWindow::refreshInstalledLoadersUi()
{
    if (!m_manageInstalledLoadersTable) {
        return;
    }

    const QString versionId = currentManagedVersion();
    m_manageInstalledLoadersTable->setRowCount(0);
    if (versionId.isEmpty()) {
        return;
    }

    const QString baseVersion = m_core.versionBaseGameVersion(versionId);
    const QString activeJson = m_core.resolveActiveLoaderJson(baseVersion);
    const QJsonArray loaders = m_core.scanInstalledLoaders(versionId);
    for (const QJsonValue &value : loaders) {
        const QJsonObject object = value.toObject();
        const int row = m_manageInstalledLoadersTable->rowCount();
        m_manageInstalledLoadersTable->insertRow(row);

        const QString loaderId = object.value("id").toString();
        QString display = loaderId;
        if (!activeJson.isEmpty() && activeJson.compare(loaderId, Qt::CaseInsensitive) == 0) {
            display = QString("%1 (已激活)").arg(loaderId);
        }
        auto *idItem = new QTableWidgetItem(display);
        idItem->setData(Qt::UserRole, loaderId);
        idItem->setData(Qt::UserRole + 1, object.value("loaderType").toString());
        m_manageInstalledLoadersTable->setItem(row, 0, idItem);
        m_manageInstalledLoadersTable->setItem(row, 1, new QTableWidgetItem(object.value("loaderType").toString()));
        m_manageInstalledLoadersTable->setItem(row, 2, new QTableWidgetItem(object.value("inheritsFrom").toString()));
    }
}

void MainWindow::refreshAvailableVersionLoaders()
{
    if (!m_manageAvailableLoadersTable) {
        return;
    }

    const QString versionId = currentManagedVersion();
    if (versionId.isEmpty()) {
        showError(this, "请先选择版本。");
        return;
    }

    const QString baseVersion = m_core.versionBaseGameVersion(versionId);
    const QString loaderType = m_manageLoaderTypeCombo->currentText();
    QString error;
    m_allLoaderVersions = m_core.fetchLoaderVersions(baseVersion, loaderType, &error);
    if (!error.isEmpty()) {
        showError(this, error);
        return;
    }

    m_manageAvailableLoadersTable->setRowCount(0);
    for (const QJsonValue &value : m_allLoaderVersions) {
        QString loaderVersion;
        if (value.isObject()) {
            const QJsonObject obj = value.toObject();
            if (loaderType == "fabric" || loaderType == "quilt") {
                loaderVersion = obj.value("loader").toObject().value("version").toString();
            } else {
                loaderVersion = obj.value("version").toString();
            }
        } else if (value.isString()) {
            loaderVersion = value.toString();
        }
        if (loaderVersion.isEmpty()) {
            continue;
        }

        const int row = m_manageAvailableLoadersTable->rowCount();
        m_manageAvailableLoadersTable->insertRow(row);
        auto *versionItem = new QTableWidgetItem(loaderVersion);
        versionItem->setData(Qt::UserRole, loaderVersion);
        m_manageAvailableLoadersTable->setItem(row, 0, versionItem);
        m_manageAvailableLoadersTable->setItem(row, 1, new QTableWidgetItem(baseVersion));
    }
}

void MainWindow::refreshRemoteVersionsUi()
{
    QString error;
    if (m_remoteVersions.isEmpty()) {
        m_remoteVersions = m_core.fetchVersionManifest(&error);
        if (!error.isEmpty()) {
            showError(this, error);
            return;
        }
    }

    const QString selectedType = m_homeVersionTypeCombo->currentData().toString();
    const QJsonArray filteredVersions = m_core.filterVersionsByType(m_remoteVersions, selectedType);

    m_homeVersionCombo->clear();
    for (const QJsonValue &value : filteredVersions) {
        m_homeVersionCombo->addItem(value.toObject().value("id").toString());
    }
    appendLog(QString("已刷新版本列表，共 %1 个（%2）").arg(filteredVersions.size()).arg(m_homeVersionTypeCombo->currentText()));
}

void MainWindow::appendLog(const QString &message)
{
    const QStringList lines = message.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return;
    }

    for (const QString &rawLine : lines) {
        const QString line = QString("[%1] %2")
                                 .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
                                      rawLine.trimmed());
        writeLogLine(line);
        if (m_logsEdit) {
            m_logsEdit->appendPlainText(line);
        }
    }
}

static QString mw_win32ErrorMessage(unsigned long errorCode)
{
#ifdef Q_OS_WIN
    if (errorCode == 0) return QString();
    LPWSTR messageBuffer = nullptr;
    const size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);
    QString msg = QString::fromWCharArray(messageBuffer, static_cast<int>(size)).trimmed();
    LocalFree(messageBuffer);
    if (msg.endsWith('\n')) msg.chop(1);
    if (msg.endsWith('\r')) msg.chop(1);
    return msg;
#else
    Q_UNUSED(errorCode);
    return QString();
#endif
}

static QString mw_ioContextForPath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    QString info = QString("路径=\"%1\", exists=%2").arg(native).arg(QFileInfo::exists(native));
    const QFileInfo fi(native);
    if (fi.exists()) {
        info += QString(", isDir=%1, isFile=%2, isWritable=%3, size=%4")
                    .arg(fi.isDir())
                    .arg(fi.isFile())
                    .arg(fi.isWritable())
                    .arg(fi.size());
    }
    const QString parent = QFileInfo(native).absolutePath();
    const QFileInfo pfi(parent);
    info += QString(" | parent=\"%1\", exists=%2, isDir=%3, isWritable=%4")
                .arg(QDir::toNativeSeparators(parent))
                .arg(pfi.exists())
                .arg(pfi.isDir())
                .arg(pfi.isWritable());
    return info;
}

static QString mw_diagnosePathHierarchy(const QString &fullPath)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(fullPath);
    QStringList segments;
    const QStringList parts = native.split('\\', Qt::SkipEmptyParts);
    QString accum;
    for (const QString &p : parts) {
        if (accum.isEmpty()) {
            accum = p;
        } else {
            accum += "\\" + p;
        }
        segments.append(accum);
    }
    QString report;
    for (const QString &seg : segments) {
        const std::wstring wseg = seg.toStdWString();
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        const BOOL ok = GetFileAttributesExW(wseg.c_str(), GetFileExInfoStandard, &fad);
        DWORD lastErr = 0;
        QString tag;
        if (!ok) {
            lastErr = GetLastError();
            tag = QString("❌ 不存在, GetLastError=%1 %2")
                      .arg(lastErr)
                      .arg(mw_win32ErrorMessage(lastErr));
        } else {
            QStringList attrs;
            attrs.append(QString("sizeHi=%1, sizeLo=%2").arg(fad.nFileSizeHigh).arg(fad.nFileSizeLow));
            if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) attrs.append("DIR");
            else attrs.append("FILE");
            if (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) attrs.append("REPARSE_POINT");
            if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) attrs.append("READONLY");
            if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) attrs.append("HIDDEN");
            if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) attrs.append("SYSTEM");
            tag = attrs.join(",");
        }
        report += QString("\n    → [%1] %2").arg(seg, tag);
    }
    return report;
#else
    Q_UNUSED(fullPath);
    return QString();
#endif
}

struct LogDiagnoseResult {
    unsigned long createFileLastError = 0;
    HANDLE handle = INVALID_HANDLE_VALUE;
};

static LogDiagnoseResult mw_diagnoseCreateFile(const QString &fullPath)
{
#ifdef Q_OS_WIN
    LogDiagnoseResult r;
    const std::wstring wpath = QDir::toNativeSeparators(fullPath).toStdWString();
    r.handle = CreateFileW(
        wpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    r.createFileLastError = (r.handle == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
    return r;
#else
    LogDiagnoseResult r;
    return r;
#endif
}

static QString mw_diagnoseHexDump(const QString &s)
{
    QString dump;
    const int n = s.size();
    for (int i = 0; i < n; ++i) {
        const QChar ch = s.at(i);
        const ushort code = ch.unicode();
        dump += QString(" U+%1").arg(code, 4, 16, QChar('0'));
        if ((i + 1) % 16 == 0) dump += "\n       ";
    }
    return dump;
}

#ifdef Q_OS_WIN
struct DirectoryProbeResult {
    bool writable = false;
    DWORD createFileLastError = 0;
    DWORD writeFileLastError = 0;
    QString attemptedPath;
    QString paramTrace;
};

struct OpenParamRecipe {
    const char *name;
    DWORD desiredAccess;
    DWORD shareMode;
    DWORD flagsAndAttrs;
    bool useLongPathPrefix;
};

static DirectoryProbeResult mw_probeDirectoryWritable(const QString &dirPath)
{
    DirectoryProbeResult r;
    const QString plainProbe = dirPath + "/_kill_probe_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";
    r.attemptedPath = plainProbe;

    static const OpenParamRecipe recipes[] = {
        {"Original"        , GENERIC_WRITE                                       , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_ATTRIBUTE_NORMAL                      , false},
        {"+GENERIC_READ"   , GENERIC_READ | GENERIC_WRITE                        , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_ATTRIBUTE_NORMAL                      , false},
        {"ShareRead"       , GENERIC_READ | GENERIC_WRITE                        , FILE_SHARE_READ                                        , FILE_ATTRIBUTE_NORMAL                      , false},
        {"ShareNone"       , GENERIC_READ | GENERIC_WRITE                        , 0                                                      , FILE_ATTRIBUTE_NORMAL                      , false},
        {"LongPathPrefix"  , GENERIC_READ | GENERIC_WRITE                        , FILE_SHARE_READ                                        , FILE_ATTRIBUTE_NORMAL                      , true },
        {"BackupSemantics" , GENERIC_READ | GENERIC_WRITE                        , FILE_SHARE_READ                                        , FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, false},
    };

    QString trace;
    for (const auto &rc : recipes) {
        QString target = plainProbe;
        if (rc.useLongPathPrefix) {
            target = "\\\\?\\" + QDir::toNativeSeparators(target);
        }
        const std::wstring wpath = QDir::toNativeSeparators(target).toStdWString();
        HANDLE h = CreateFileW(
            wpath.c_str(),
            rc.desiredAccess,
            rc.shareMode,
            nullptr,
            CREATE_ALWAYS,
            rc.flagsAndAttrs,
            nullptr);
        DWORD ce = 0;
        bool ok = true;
        if (h == INVALID_HANDLE_VALUE) {
            ce = GetLastError();
            ok = false;
        } else {
            static const char probeData[] = "KillDirProbe-OK\n";
            DWORD written = 0;
            const BOOL wok = WriteFile(h, probeData, static_cast<DWORD>(strlen(probeData)), &written, nullptr);
            const DWORD we = wok ? 0 : GetLastError();
            FlushFileBuffers(h);
            CloseHandle(h);
            DeleteFileW(wpath.c_str());
            ok = wok && (written == static_cast<DWORD>(strlen(probeData)));
            if (!ok) r.writeFileLastError = we;
        }
        trace += QString("\n      · %1=%2  CreateErr=%3(0x%4)")
                     .arg(rc.name)
                     .arg(ok ? "✅OK" : "❌FAIL")
                     .arg(ce).arg(ce, 0, 16);
        if (ce != 0) trace += QString(" %1").arg(mw_win32ErrorMessage(ce));
        if (ok && !r.writable) {
            r.writable = true;
            r.createFileLastError = 0;
        }
        if (!ok && r.createFileLastError == 0 && ce != 0) {
            r.createFileLastError = ce;
        }
    }
    r.paramTrace = trace;
    return r;
}
#endif

struct LogOpenAttempt {
    DWORD mode = 0;
    DWORD lastError = 0;
    HANDLE handle = INVALID_HANDLE_VALUE;
};

static LogOpenAttempt mw_tryOpenNative(const QString &fullPath, DWORD creationDisposition)
{
#ifdef Q_OS_WIN
    LogOpenAttempt r;
    r.mode = creationDisposition;
    const std::wstring wpath = QDir::toNativeSeparators(fullPath).toStdWString();
    r.handle = CreateFileW(
        wpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        creationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    r.lastError = (r.handle == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
    return r;
#else
    LogOpenAttempt r;
    return r;
#endif
}

QString MainWindow::logFilePath() const
{
    QStringList candidateDirs;

    QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataRoot.isEmpty()) appDataRoot = QDir::homePath() + "/.kill";
    candidateDirs.append(appDataRoot + "/logs");

    const QString exeDir = QCoreApplication::applicationDirPath();
    if (!exeDir.isEmpty()) {
        candidateDirs.append(exeDir + "/logs");
    }

    QString docsRoot = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!docsRoot.isEmpty()) {
        candidateDirs.append(docsRoot + "/Kill/logs");
    }

    QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!tempRoot.isEmpty()) {
        candidateDirs.append(tempRoot + "/Kill/logs");
    }

    const QString fallbackName = "kill-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".log";

    QString selectedDir;
    QStringList probeReport;

    int idx = 0;
    for (const QString &dir : candidateDirs) {
        idx++;
        const bool mkOk = QDir().mkpath(dir);
#ifdef Q_OS_WIN
        DirectoryProbeResult pr;
        if (mkOk) {
            pr = mw_probeDirectoryWritable(dir);
        } else {
            pr.writable = false;
            pr.createFileLastError = GetLastError();
        }
        QString line = QString("[候选%1] %2  mkpath=%3  probe=%4")
                           .arg(idx)
                           .arg(QDir::toNativeSeparators(dir))
                           .arg(mkOk ? "OK" : "FAIL")
                           .arg(pr.writable ? "WRITABLE" : "DENIED");
        if (!pr.writable) {
            if (pr.createFileLastError != 0) {
                line += QString(" [CreateFile err=%1 (0x%2) %3]")
                            .arg(pr.createFileLastError)
                            .arg(pr.createFileLastError, 0, 16)
                            .arg(mw_win32ErrorMessage(pr.createFileLastError));
            } else if (pr.writeFileLastError != 0) {
                line += QString(" [WriteFile err=%1 (0x%2) %3]")
                            .arg(pr.writeFileLastError)
                            .arg(pr.writeFileLastError, 0, 16)
                            .arg(mw_win32ErrorMessage(pr.writeFileLastError));
            } else if (!mkOk) {
                line += QString(" (目录创建失败)");
            }
        }
        if (!pr.paramTrace.isEmpty()) {
            line += QString("\n    🧪 参数矩阵:%1").arg(pr.paramTrace);
        }
        probeReport.append(line);
        if (pr.writable) {
            selectedDir = dir;
            break;
        }
#else
        if (mkOk) {
            QFileInfo fi(dir);
            if (fi.isDir() && fi.isWritable()) {
                selectedDir = dir;
                probeReport.append(QString("[候选%1] %2  mkpath=OK  probe=WRITABLE").arg(idx).arg(QDir::toNativeSeparators(dir)));
                break;
            } else {
                probeReport.append(QString("[候选%1] %2  mkpath=OK  probe=DENIED (QFileInfo不可写)").arg(idx).arg(QDir::toNativeSeparators(dir)));
            }
        } else {
            probeReport.append(QString("[候选%1] %2  mkpath=FAIL").arg(idx).arg(QDir::toNativeSeparators(dir)));
        }
#endif
    }

    if (selectedDir.isEmpty()) {
        selectedDir = QDir::tempPath();
        probeReport.append(QString("[FALLBACK] 所有候选目录都不可写！退回临时目录: %1").arg(QDir::toNativeSeparators(selectedDir)));
    }

    if (m_logsEdit) {
        QString full = QString("[%1] 🗂️ 日志目录可用性逐路径探测:")
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        for (const QString &l : probeReport) {
            full += "\n  " + l;
        }
        full += QString("\n  🏁 最终采用日志目录: %1").arg(QDir::toNativeSeparators(selectedDir));
        m_logsEdit->appendPlainText(full);
    }

    return selectedDir + "/" + fallbackName;
}

void MainWindow::writeLogLine(const QString &line)
{
    static bool s_inWriteLog = false;
    if (s_inWriteLog) {
        return;
    }
    struct Guard {
        bool *flag;
        explicit Guard(bool *f) : flag(f) { *flag = true; }
        ~Guard() { *flag = false; }
    } guard(&s_inWriteLog);

    const QString cleanPath = QDir::fromNativeSeparators(QDir::toNativeSeparators(m_logFilePath));
    QFile file(cleanPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
#ifdef Q_OS_WIN
        const unsigned long qtOpenLastErr = GetLastError();
#else
        const unsigned long qtOpenLastErr = 0;
#endif
        QString msg = QString("日志写入失败(open): %1").arg(QDir::toNativeSeparators(cleanPath));
        msg += QString("\n  IO上下文: %1").arg(mw_ioContextForPath(cleanPath));
        msg += QString("\n  QFile.error=%1, QFile.errorString=%2")
                   .arg(file.error())
                   .arg(file.errorString());
        msg += QString("\n  QtOpen Win32错误码=%1 (0x%2), 描述: %3")
                   .arg(qtOpenLastErr)
                   .arg(qtOpenLastErr, 0, 16)
                   .arg(qtOpenLastErr ? (mw_win32ErrorMessage(qtOpenLastErr).isEmpty()
                                            ? QString("(无系统描述)")
                                            : mw_win32ErrorMessage(qtOpenLastErr))
                                      : QString("(无)"));

#ifdef Q_OS_WIN
        msg += QString("\n  🔍 路径字符十六进制转储（完整路径, %1 个QChar）:%2")
                   .arg(cleanPath.size())
                   .arg(mw_diagnoseHexDump(cleanPath));
        const QFileInfo fi(cleanPath);
        const QString fileName = fi.fileName();
        msg += QString("\n  🔍 文件名十六进制转储（\"%1\", %2 个QChar）:%3")
                   .arg(fileName)
                   .arg(fileName.size())
                   .arg(mw_diagnoseHexDump(fileName));
        const QString parentDir = fi.absolutePath();
        const QString asciiTestPath = parentDir + "/_kill_diag_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";

        LogOpenAttempt openAlways = mw_tryOpenNative(cleanPath, OPEN_ALWAYS);
        LogOpenAttempt createAlways = mw_tryOpenNative(cleanPath, CREATE_ALWAYS);
        LogOpenAttempt createNew = mw_tryOpenNative(cleanPath, CREATE_NEW);
        if (openAlways.handle != INVALID_HANDLE_VALUE) CloseHandle(openAlways.handle);
        if (createAlways.handle != INVALID_HANDLE_VALUE) CloseHandle(createAlways.handle);
        if (createNew.handle != INVALID_HANDLE_VALUE) CloseHandle(createNew.handle);
        msg += QString("\n  🧪 原文件名 3 种创建模式对比:")
                  + QString("\n     OPEN_ALWAYS   → h=%1 err=%2 (0x%3) %4")
                        .arg(quintptr(openAlways.handle), 0, 16)
                        .arg(openAlways.lastError).arg(openAlways.lastError, 0, 16)
                        .arg(openAlways.lastError ? mw_win32ErrorMessage(openAlways.lastError) : "SUCCESS")
                  + QString("\n     CREATE_ALWAYS → h=%1 err=%2 (0x%3) %4")
                        .arg(quintptr(createAlways.handle), 0, 16)
                        .arg(createAlways.lastError).arg(createAlways.lastError, 0, 16)
                        .arg(createAlways.lastError ? mw_win32ErrorMessage(createAlways.lastError) : "SUCCESS")
                  + QString("\n     CREATE_NEW    → h=%1 err=%2 (0x%3) %4")
                        .arg(quintptr(createNew.handle), 0, 16)
                        .arg(createNew.lastError).arg(createNew.lastError, 0, 16)
                        .arg(createNew.lastError ? mw_win32ErrorMessage(createNew.lastError) : "SUCCESS");

        LogOpenAttempt asciiTest = mw_tryOpenNative(asciiTestPath, CREATE_ALWAYS);
        msg += QString("\n  🧪 同目录 ASCII 对照文件 (CREATE_ALWAYS): path=\"%1\" h=%2 err=%3 (0x%4) %5")
                   .arg(QDir::toNativeSeparators(asciiTestPath))
                   .arg(quintptr(asciiTest.handle), 0, 16)
                   .arg(asciiTest.lastError).arg(asciiTest.lastError, 0, 16)
                   .arg(asciiTest.lastError ? mw_win32ErrorMessage(asciiTest.lastError) : "SUCCESS");

        bool fallbackOk = false;
        QString fallbackMsg;
        if (asciiTest.handle != INVALID_HANDLE_VALUE) {
            const QByteArray utf8 = (line + "\n").toUtf8();
            DWORD written = 0;
            const BOOL ok = WriteFile(asciiTest.handle, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            const DWORD we = ok ? 0 : GetLastError();
            FlushFileBuffers(asciiTest.handle);
            CloseHandle(asciiTest.handle);
            DeleteFileW(QDir::toNativeSeparators(asciiTestPath).toStdWString().c_str());
            if (ok) {
                fallbackOk = true;
                fallbackMsg = QString("✅ ASCII 对照文件 WriteFile 成功 %1 字节，已删除（说明 logs 目录真正可写，问题在原文件名字符）").arg(written);
            } else {
                fallbackMsg = QString("❌ ASCII 对照文件 WriteFile 失败 err=%1 (0x%2) %3")
                                  .arg(we).arg(we, 0, 16).arg(mw_win32ErrorMessage(we));
            }
        } else if (asciiTest.lastError != 0) {
            fallbackMsg = QString("❌ ASCII 对照文件本身就打不开，logs 目录可能被安全软件拦截");
        }
        if (!fallbackMsg.isEmpty()) {
            msg += QString("\n  🧪 %1").arg(fallbackMsg);
        }

        if (fallbackOk || (openAlways.handle != INVALID_HANDLE_VALUE)) {
            msg += QString("\n  👉 诊断结论：logs 目录本身可写，CreateFileW 对 ASCII 文件名可成功；原 kill-*.log 文件名字符串里有肉眼不可见的 Unicode 控制字符/NULL 截断，导致 Win32 层看到的路径不完整。接下来我会自动改用 ASCII 兜底文件名作为本次日志输出文件。");
            static QString s_fallbackLogPath;
            if (s_fallbackLogPath.isEmpty()) {
                const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
                s_fallbackLogPath = parentDir + "/kill_fb_" + ts + ".log";
            }
            HANDLE fb = CreateFileW(
                QDir::toNativeSeparators(s_fallbackLogPath).toStdWString().c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (fb != INVALID_HANDLE_VALUE) {
                SetFilePointer(fb, 0, nullptr, FILE_END);
                const QByteArray utf8 = (line + "\n").toUtf8();
                DWORD written = 0;
                WriteFile(fb, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
                FlushFileBuffers(fb);
                CloseHandle(fb);
                msg += QString("\n  🔧 兜底写入: 已切换到 fallback 日志文件 %1（本次会话后续日志将尝试写这里）").arg(QDir::toNativeSeparators(s_fallbackLogPath));
                m_logFilePath = s_fallbackLogPath;
            }
        }

        msg += QString("\n  🗂️ 路径逐级诊断 (GetFileAttributesExW)：%1").arg(mw_diagnosePathHierarchy(cleanPath));

        const std::wstring wparent = QDir::toNativeSeparators(parentDir).toStdWString();
        WIN32_FILE_ATTRIBUTE_DATA parentFad = {};
        if (GetFileAttributesExW(wparent.c_str(), GetFileExInfoStandard, &parentFad)) {
            QStringList attrs;
            if (parentFad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) attrs.append("DIR");
            if (parentFad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) attrs.append("REPARSE_POINT");
            if (parentFad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) attrs.append("READONLY");
            msg += QString("\n  📁 父目录真实属性: %1").arg(attrs.join(","));
            if (parentFad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                msg += QString("  ⚠️ 父目录是交接点/软链接，目标可能缺失！");
            }
        }
#endif

        if (m_logsEdit) {
            const QString prefixed = QString("[%1] %2")
                                         .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), msg);
            m_logsEdit->appendPlainText(prefixed);
        }
        return;
    }

    QTextStream stream(&file);
    stream << line << '\n';
    stream.flush();
    const auto fileError = file.error();
    file.close();
    if (fileError != QFile::NoError) {
#ifdef Q_OS_WIN
        const unsigned long lastErr = GetLastError();
#else
        const unsigned long lastErr = 0;
#endif
        QString msg = QString("日志写入失败(flush/close): %1")
                          .arg(QDir::toNativeSeparators(cleanPath));
        msg += QString(" | QFile.error=%1, QFile.errorString=%2")
                   .arg(fileError)
                   .arg(file.errorString());
        if (lastErr != 0) {
            const QString desc = mw_win32ErrorMessage(lastErr);
            msg += QString(" | Win32错误码=%1 (0x%2), 描述: %3")
                       .arg(lastErr)
                       .arg(lastErr, 0, 16)
                       .arg(desc.isEmpty() ? QString("(无系统描述)") : desc);
        }
        if (m_logsEdit) {
            const QString prefixed = QString("[%1] %2")
                                         .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), msg);
            m_logsEdit->appendPlainText(prefixed);
        }
    }
}

void MainWindow::openLogDirectory()
{
    const QString directoryPath = QFileInfo(m_logFilePath).absolutePath();
    if (directoryPath.isEmpty() || !QDir(directoryPath).exists()) {
        showError(this, "日志目录不存在。");
        return;
    }

    appendLog(QString("打开日志目录: %1").arg(QDir::toNativeSeparators(directoryPath)));
    QDesktopServices::openUrl(QUrl::fromLocalFile(directoryPath));
}

void MainWindow::copyCurrentLogPath()
{
    if (m_logFilePath.isEmpty()) {
        showError(this, "当前日志路径为空。");
        return;
    }

    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(m_logFilePath));
    appendLog(QString("已复制当前日志路径: %1").arg(QDir::toNativeSeparators(m_logFilePath)));
    showInfo(this, "当前日志路径已复制到剪贴板。");
}

void MainWindow::populateResultsTable(const QJsonArray &array, const QString &source)
{
    m_resourceResultsTable->setRowCount(0);
    for (int i = 0; i < array.size(); ++i) {
        const QJsonObject object = array.at(i).toObject();
        const int row = m_resourceResultsTable->rowCount();
        m_resourceResultsTable->insertRow(row);

        QString name;
        QString version;
        QString extra;

        if (source == "Modrinth") {
            name = object.value("title").toString();
            version = object.value("latest_version").toString();
            extra = object.value("project_type").toString();
        } else {
            name = object.value("name").toString();
            version = QString::number(object.value("id").toInteger());
            extra = object.value("summary").toString();
        }

        auto *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, QJsonDocument(object).toJson(QJsonDocument::Compact));
        m_resourceResultsTable->setItem(row, 0, nameItem);
        m_resourceResultsTable->setItem(row, 1, new QTableWidgetItem(version));
        m_resourceResultsTable->setItem(row, 2, new QTableWidgetItem(source));
        m_resourceResultsTable->setItem(row, 3, new QTableWidgetItem(extra));
    }
    appendLog(QString("搜索完成，结果 %1 条").arg(array.size()));
}

void MainWindow::populateModSearchResults(const QJsonArray &array, const QString &source, QVBoxLayout *resultsLayout)
{
    if (!resultsLayout) return;

    while (resultsLayout->count() > 1) {
        auto *item = resultsLayout->takeAt(0);
        if (auto *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    class ClickableFrame : public QFrame {
    public:
        std::function<void(QMouseEvent *)> pressHandler;
        std::function<void(QMouseEvent *)> releaseHandler;
    protected:
        void mousePressEvent(QMouseEvent *ev) override {
            if (pressHandler) pressHandler(ev);
            QFrame::mousePressEvent(ev);
        }
        void mouseReleaseEvent(QMouseEvent *ev) override {
            if (releaseHandler) releaseHandler(ev);
            QFrame::mouseReleaseEvent(ev);
        }
    };

    for (int i = 0; i < array.size(); ++i) {
        const QJsonObject object = array.at(i).toObject();
        if (object.isEmpty()) continue;

        QString iconUrl;
        QString name, slug, summary, categoriesDisplay, versionsDisplay, downloadsDisplay, updatedDisplay;
        qint64 downloads = 0;

        if (source == "Modrinth") {
            name = object.value("title").toString(object.value("name").toString("未知模组"));
            if (name.isEmpty()) name = "未知模组";
            slug = object.value("slug").toString();
            summary = object.value("description").toString(object.value("body").toString());
            iconUrl = object.value("icon_url").toString();
            downloads = object.value("downloads").toInteger(0);
            downloadsDisplay = formatDownloadCount(downloads);

            const QJsonArray categories = object.value("categories").toArray();
            QStringList catList;
            QSet<QString> seenCats;
            for (const QJsonValue &cat : categories) {
                const QString cs = cat.toString();
                if (!cs.isEmpty() && !seenCats.contains(cs)) {
                    seenCats.insert(cs);
                    catList << cs;
                }
            }
            if (catList.isEmpty()) catList << "mod";
            categoriesDisplay = catList.join(" · ");

            const QJsonArray versions = object.value("game_versions").toArray();
            QStringList verList;
            QSet<QString> seenVers;
            for (const QJsonValue &v : versions) {
                const QString vs = v.toString();
                if (!vs.isEmpty() && !seenVers.contains(vs)) {
                    seenVers.insert(vs);
                    verList << vs;
                }
            }
            if (!verList.isEmpty()) {
                std::sort(verList.begin(), verList.end(), [](const QString &a, const QString &b) {
                    return compareMajorVersionFamily(a, b) > 0;
                });
                versionsDisplay = QString("%1+").arg(verList.first());
            } else {
                versionsDisplay = "-";
            }

            const QJsonArray loaders = object.value("loaders").toArray();
            QStringList loaderList;
            QSet<QString> seenLoaders;
            for (const QJsonValue &l : loaders) {
                const QString ls = l.toString();
                if (ls.isEmpty()) continue;
                const QString ln = normalizeLoaderName(ls);
                if (ln.isEmpty()) continue;
                if (seenLoaders.contains(ln)) continue;
                seenLoaders.insert(ln);
                loaderList << ln;
            }
            if (!loaderList.isEmpty()) {
                versionsDisplay.prepend(loaderList.join(" / ") + "  ");
            }

            const QString dateStr = object.value("date_modified").toString(object.value("date_created").toString(object.value("published").toString()));
            if (!dateStr.isEmpty()) {
                QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
                if (dt.isValid()) {
                    qint64 days = dt.daysTo(QDateTime::currentDateTime());
                    if (days == 0) updatedDisplay = "今天";
                    else if (days == 1) updatedDisplay = "昨天";
                    else if (days < 30) updatedDisplay = QString("%1 天前").arg(days);
                    else if (days < 365) updatedDisplay = QString("%1 个月前").arg(days / 30);
                    else updatedDisplay = QString("%1 年前").arg(days / 365);
                }
            }
        } else {
            name = object.value("name").toString("未知模组");
            if (name.isEmpty()) name = "未知模组";
            slug = QString::number(object.value("id").toInteger(0));
            summary = object.value("summary").toString();
            downloads = object.value("downloadCount").toInteger(0);
            downloadsDisplay = formatDownloadCount(downloads);

            const QJsonArray cats = object.value("categories").toArray();
            QStringList catList;
            QSet<QString> seenCats;
            for (const QJsonValue &c : cats) {
                const QJsonObject co = c.toObject();
                if (co.isEmpty()) continue;
                const QString cn = co.value("name").toString();
                if (!cn.isEmpty() && !seenCats.contains(cn)) {
                    seenCats.insert(cn);
                    catList << cn;
                }
            }
            if (catList.isEmpty()) catList << "mod";
            categoriesDisplay = catList.join(" · ");

            const QJsonObject links = object.value("links").toObject();
            iconUrl = links.value("websiteUrl").toString();
            if (iconUrl.isEmpty()) {
                const QJsonObject logo = object.value("logo").toObject();
                iconUrl = logo.value("thumbnailUrl").toString(logo.value("url").toString());
            }

            versionsDisplay = "全版本";
            updatedDisplay = "CurseForge";
        }

        auto *clickableCard = new ClickableFrame();
        clickableCard->setObjectName("modCard");
        clickableCard->setStyleSheet(
            "#modCard {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e7eb;"
            "  border-radius: 12px;"
            "}"
            "#modCard:hover {"
            "  border-color: #3b82f6;"
            "  background-color: #f0f7ff;"
            "}"
        );
        clickableCard->setCursor(Qt::PointingHandCursor);

        auto *realCardLayout = new QHBoxLayout(clickableCard);
        realCardLayout->setContentsMargins(14, 12, 14, 12);
        realCardLayout->setSpacing(14);

        auto *newIconLabel = new QLabel(clickableCard);
        newIconLabel->setFixedSize(48, 48);
        newIconLabel->setPixmap(createRoundedPixmap(QPixmap(), 12));

        if (!iconUrl.isEmpty() && m_iconCache.contains(iconUrl)) {
            newIconLabel->setPixmap(createRoundedPixmap(m_iconCache[iconUrl], 12));
        } else if (!iconUrl.isEmpty()) {
            const QString cacheKey = iconUrl;
            QNetworkReply *reply = m_networkManager->get(QNetworkRequest(QUrl(iconUrl)));
            connect(reply, &QNetworkReply::finished, this, [this, reply, newIconLabel, cacheKey]() {
                if (reply->error() == QNetworkReply::NoError) {
                    QPixmap pm;
                    pm.loadFromData(reply->readAll());
                    if (!pm.isNull()) {
                        m_iconCache.insert(cacheKey, pm);
                        newIconLabel->setPixmap(createRoundedPixmap(pm, 12));
                    }
                }
                reply->deleteLater();
            });
        }

        auto *newContent = new QWidget(clickableCard);
        auto *newContentLayout = new QVBoxLayout(newContent);
        newContentLayout->setContentsMargins(0, 0, 0, 0);
        newContentLayout->setSpacing(4);

        auto *newTitleRow = new QHBoxLayout();
        newTitleRow->setSpacing(8);
        auto *newTitleLabel = new QLabel(QString("<b>%1</b>  |  %2").arg(name, slug), newContent);
        newTitleLabel->setStyleSheet("font-size: 14px; color: #111827; font-weight: 600;");
        newTitleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        newTitleRow->addWidget(newTitleLabel);
        newTitleRow->addStretch();
        auto *newSourceLabel = new QLabel(source, newContent);
        newSourceLabel->setStyleSheet(
            "font-size: 12px; color: #6b7280;"
            "padding: 2px 8px; background-color: #f3f4f6;"
            "border-radius: 4px;"
        );
        newTitleRow->addWidget(newSourceLabel);
        newContentLayout->addLayout(newTitleRow);

        auto *newTagRow = new QHBoxLayout();
        newTagRow->setSpacing(8);
        auto *newCatLabel = new QLabel(categoriesDisplay, newContent);
        newCatLabel->setStyleSheet(
            "font-size: 12px; color: #059669;"
            "padding: 2px 8px; background-color: #d1fae5;"
            "border-radius: 4px; font-weight: 500;"
        );
        newTagRow->addWidget(newCatLabel);
        newTagRow->addStretch();
        newContentLayout->addLayout(newTagRow);

        auto *newDescLabel = new QLabel(summary, newContent);
        newDescLabel->setWordWrap(true);
        newDescLabel->setStyleSheet("font-size: 13px; color: #6b7280;");
        newContentLayout->addWidget(newDescLabel);

        auto *newMetaRow = new QHBoxLayout();
        newMetaRow->setSpacing(20);
        auto *newVerLabel = new QLabel("◎ " + versionsDisplay, newContent);
        newVerLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
        newMetaRow->addWidget(newVerLabel);
        auto *newDlLabel = new QLabel("↧ " + downloadsDisplay, newContent);
        newDlLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
        newMetaRow->addWidget(newDlLabel);
        if (!updatedDisplay.isEmpty()) {
            auto *newUpdLabel = new QLabel("↑ " + updatedDisplay, newContent);
            newUpdLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
            newMetaRow->addWidget(newUpdLabel);
        }
        newMetaRow->addStretch();
        newContentLayout->addLayout(newMetaRow);

        realCardLayout->addWidget(newIconLabel, 0, Qt::AlignTop);
        realCardLayout->addWidget(newContent, 1);

        const QJsonObject payloadCopy = object;
        const QString sourceCopy = source;
        clickableCard->pressHandler = [clickableCard](QMouseEvent *) {
            auto *anim = new QPropertyAnimation(clickableCard, "geometry");
            const QRect startRect = clickableCard->geometry();
            anim->setDuration(100);
            anim->setKeyValueAt(0, startRect);
            anim->setKeyValueAt(0.5, QRect(startRect.x(), startRect.y() + 2, startRect.width(), startRect.height()));
            anim->setKeyValueAt(1, startRect);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        };
        clickableCard->releaseHandler = [this, payloadCopy, sourceCopy](QMouseEvent *e) {
            if (e->button() == Qt::LeftButton) {
                showModDetailsDialog(payloadCopy, sourceCopy, "mod");
            }
        };

        resultsLayout->insertWidget(resultsLayout->count() - 1, clickableCard);
    }

    appendLog(QString("搜索完成，结果 %1 条").arg(array.size()));
}

void MainWindow::showModDetailsDialog(const QJsonObject &payload, const QString &source, const QString &targetType)
{
    if (!m_modsDetailPage || !m_modsSubStack || !m_modsBackButton) return;

    QLayout *oldLayout = m_modsDetailPage->layout();
    if (oldLayout) {
        while (QLayoutItem *item = oldLayout->takeAt(0)) {
            if (QWidget *w = item->widget()) w->deleteLater();
            delete item;
        }
        delete oldLayout;
    }

    auto *scrollArea = new QScrollArea(m_modsDetailPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { border: none; background-color: transparent; }");

    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 24, 24, 24);
    contentLayout->setSpacing(20);

    auto *pageLayout = new QVBoxLayout(m_modsDetailPage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(scrollArea, 1);

    QString name, slug, description, iconUrl, authorName, projectUrl;
    qint64 downloads = 0;
    QString updatedDisplay;
    QStringList categoriesList, loadersList, gameVersionsList;

    if (source == "Modrinth") {
        name = payload.value("title").toString(payload.value("name").toString("未知模组"));
        slug = payload.value("slug").toString();
        description = payload.value("description").toString(payload.value("body").toString());
        iconUrl = payload.value("icon_url").toString();
        downloads = payload.value("downloads").toInteger(0);
        if (!slug.isEmpty()) {
            projectUrl = QString("https://modrinth.com/mod/%1").arg(slug);
        }

        const QJsonArray categories = payload.value("categories").toArray();
        for (const QJsonValue &cat : categories) {
            const QString cs = cat.toString();
            if (!cs.isEmpty()) categoriesList << cs;
        }
        if (categoriesList.isEmpty()) categoriesList << "mod";

        const QJsonArray loaders = payload.value("loaders").toArray();
        QSet<QString> seenLoaders;
        for (const QJsonValue &l : loaders) {
            const QString ln = l.toString();
            if (ln.isEmpty()) continue;
            const QString normalized = normalizeLoaderName(ln);
            if (normalized.isEmpty()) continue;
            if (seenLoaders.contains(normalized)) continue;
            seenLoaders.insert(normalized);
            loadersList << normalized;
        }

        const QJsonArray versions = payload.value("game_versions").toArray();
        QSet<QString> seenVersions;
        for (const QJsonValue &v : versions) {
            const QString vs = v.toString();
            if (!vs.isEmpty() && !seenVersions.contains(vs)) {
                seenVersions.insert(vs);
                gameVersionsList << vs;
            }
        }
        if (!gameVersionsList.isEmpty()) {
            std::sort(gameVersionsList.begin(), gameVersionsList.end(), [](const QString &a, const QString &b) {
                return compareMajorVersionFamily(a, b) > 0;
            });
        }

        const QJsonArray authors = payload.value("team_members").toArray();
        if (!authors.isEmpty()) {
            const QJsonObject firstAuthor = authors.first().toObject();
            if (!firstAuthor.isEmpty()) {
                const QJsonObject userObj = firstAuthor.value("user").toObject();
                authorName = userObj.value("username").toString(userObj.value("name").toString());
            }
        }
        if (authorName.isEmpty()) {
            authorName = payload.value("author").toString();
        }

        const QString dateStr = payload.value("date_modified").toString(payload.value("date_created").toString(payload.value("published").toString()));
        if (!dateStr.isEmpty()) {
            QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
            if (dt.isValid()) {
                qint64 days = dt.daysTo(QDateTime::currentDateTime());
                if (days == 0) updatedDisplay = "今天更新";
                else if (days == 1) updatedDisplay = "昨天更新";
                else if (days < 30) updatedDisplay = QString("%1 天前更新").arg(days);
                else if (days < 365) updatedDisplay = QString("%1 个月前更新").arg(days / 30);
                else updatedDisplay = QString("%1 年前更新").arg(days / 365);
            }
        }
    } else {
        name = payload.value("name").toString("未知模组");
        slug = QString::number(payload.value("id").toInteger(0));
        description = payload.value("summary").toString();
        downloads = payload.value("downloadCount").toInteger(0);

        const QJsonObject links = payload.value("links").toObject();
        iconUrl = links.value("websiteUrl").toString();
        if (iconUrl.isEmpty()) {
            const QJsonObject logoObj = payload.value("logo").toObject();
            iconUrl = logoObj.value("thumbnailUrl").toString(logoObj.value("url").toString());
        }
        projectUrl = links.value("websiteUrl").toString();

        const QJsonArray cats = payload.value("categories").toArray();
        for (const QJsonValue &c : cats) {
            const QJsonObject cObj = c.toObject();
            if (cObj.isEmpty()) continue;
            const QString cn = cObj.value("name").toString();
            if (!cn.isEmpty()) categoriesList << cn;
        }
        if (categoriesList.isEmpty()) categoriesList << "mod";
        loadersList << "Forge";
        gameVersionsList << "全版本";
        const QJsonArray authorsArr = payload.value("authors").toArray();
        if (authorsArr.isEmpty()) {
            authorName = "CurseForge";
        } else {
            const QJsonObject firstAuthor = authorsArr.first().toObject();
            authorName = firstAuthor.value("name").toString("CurseForge");
        }
        updatedDisplay = "CurseForge";
    }

    auto *headerCard = new QFrame(content);
    headerCard->setObjectName("detailsHeaderCard");
    headerCard->setStyleSheet(
        "#detailsHeaderCard {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 16px;"
        "}"
    );
    auto *headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(24, 24, 24, 24);
    headerLayout->setSpacing(20);

    auto *iconLabel = new QLabel(headerCard);
    iconLabel->setFixedSize(80, 80);
    iconLabel->setPixmap(createRoundedPixmap(QPixmap(), 16));

    if (!iconUrl.isEmpty() && m_iconCache.contains(iconUrl)) {
        QPixmap big = m_iconCache[iconUrl];
        QPixmap scaled = big.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QSize s = scaled.size();
        QPixmap result(80, 80);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addRoundedRect(0, 0, 80, 80, 16, 16);
        painter.setClipPath(path);
        painter.drawPixmap((80 - s.width()) / 2, (80 - s.height()) / 2, scaled);
        iconLabel->setPixmap(result);
    } else if (!iconUrl.isEmpty()) {
        const QString cacheKey = iconUrl;
        QNetworkReply *reply = m_networkManager->get(QNetworkRequest(QUrl(iconUrl)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, iconLabel, cacheKey]() {
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap pm;
                pm.loadFromData(reply->readAll());
                if (!pm.isNull()) {
                    m_iconCache.insert(cacheKey, pm);
                    QPixmap scaled = pm.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    QSize s = scaled.size();
                    QPixmap result(80, 80);
                    result.fill(Qt::transparent);
                    QPainter painter(&result);
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    QPainterPath path;
                    path.addRoundedRect(0, 0, 80, 80, 16, 16);
                    painter.setClipPath(path);
                    painter.drawPixmap((80 - s.width()) / 2, (80 - s.height()) / 2, scaled);
                    iconLabel->setPixmap(result);
                }
            }
            reply->deleteLater();
        });
    }

    auto *infoWidget = new QWidget(headerCard);
    auto *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto *titleLabel = new QLabel(QString("<span style='font-size:22px; font-weight:800; color:#111827;'>%1</span>").arg(name), infoWidget);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();

    auto *sourceBadge = new QLabel(source, infoWidget);
    sourceBadge->setStyleSheet(
        "font-size: 12px; color: #3b82f6; font-weight: 600;"
        "padding: 4px 12px; background-color: #dbeafe;"
        "border-radius: 8px;"
    );
    titleRow->addWidget(sourceBadge);
    infoLayout->addLayout(titleRow);

    auto *metaRow1 = new QHBoxLayout();
    metaRow1->setSpacing(20);
    if (!authorName.isEmpty()) {
        auto *authorLabel = new QLabel("👤 作者: " + authorName, infoWidget);
        authorLabel->setStyleSheet("font-size: 13px; color: #4b5563;");
        metaRow1->addWidget(authorLabel);
    }
    auto *downloadsLabel = new QLabel("↧ 下载量: " + formatDownloadCount(downloads), infoWidget);
    downloadsLabel->setStyleSheet("font-size: 13px; color: #4b5563;");
    metaRow1->addWidget(downloadsLabel);
    if (!updatedDisplay.isEmpty()) {
        auto *updLabel = new QLabel("📅 " + updatedDisplay, infoWidget);
        updLabel->setStyleSheet("font-size: 13px; color: #4b5563;");
        metaRow1->addWidget(updLabel);
    }
    metaRow1->addStretch();
    infoLayout->addLayout(metaRow1);

    if (!categoriesList.isEmpty()) {
        auto *catRow = new QHBoxLayout();
        catRow->setSpacing(6);
        for (const QString &cat : categoriesList) {
            auto *badge = new QLabel(cat, infoWidget);
            badge->setStyleSheet(
                "font-size: 11px; color: #059669; font-weight: 500;"
                "padding: 2px 10px; background-color: #d1fae5;"
                "border-radius: 6px;"
            );
            catRow->addWidget(badge);
        }
        catRow->addStretch();
        infoLayout->addLayout(catRow);
    }

    if (!loadersList.isEmpty()) {
        auto *loaderRow = new QHBoxLayout();
        loaderRow->setSpacing(6);
        auto *loaderHint = new QLabel("加载器:", infoWidget);
        loaderHint->setStyleSheet("font-size: 12px; color: #6b7280; font-weight: 500;");
        loaderRow->addWidget(loaderHint);
        for (const QString &loader : loadersList) {
            QString color = "#3b82f6";
            QString bg = "#dbeafe";
            if (loader == "Forge") { color = "#dc2626"; bg = "#fee2e2"; }
            else if (loader == "NeoForge") { color = "#d97706"; bg = "#fef3c7"; }
            else if (loader == "Quilt") { color = "#9333ea"; bg = "#f3e8ff"; }
            auto *badge = new QLabel(loader, infoWidget);
            badge->setStyleSheet(QString(
                "font-size: 11px; color: %1; font-weight: 600;"
                "padding: 2px 10px; background-color: %2;"
                "border-radius: 6px;"
            ).arg(color, bg));
            loaderRow->addWidget(badge);
        }
        loaderRow->addStretch();
        infoLayout->addLayout(loaderRow);
    }

    if (!gameVersionsList.isEmpty()) {
        auto *verRow = new QHBoxLayout();
        verRow->setSpacing(6);
        auto *verHint = new QLabel("版本:", infoWidget);
        verHint->setStyleSheet("font-size: 12px; color: #6b7280; font-weight: 500;");
        verRow->addWidget(verHint);
        int shown = 0;
        for (const QString &ver : gameVersionsList) {
            if (shown >= 5) {
                auto *more = new QLabel(QString("+%1").arg(gameVersionsList.size() - shown), infoWidget);
                more->setStyleSheet(
                    "font-size: 11px; color: #6b7280; font-weight: 500;"
                    "padding: 2px 8px; background-color: #f3f4f6;"
                    "border-radius: 6px;"
                );
                verRow->addWidget(more);
                break;
            }
            auto *badge = new QLabel(ver, infoWidget);
            badge->setStyleSheet(
                "font-size: 11px; color: #4b5563; font-weight: 500;"
                "padding: 2px 8px; background-color: #f3f4f6;"
                "border-radius: 6px;"
            );
            verRow->addWidget(badge);
            shown++;
        }
        verRow->addStretch();
        infoLayout->addLayout(verRow);
    }

    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    headerLayout->addWidget(infoWidget, 1);
    contentLayout->addWidget(headerCard);

    if (!description.isEmpty()) {
        auto *descCard = new QFrame(content);
        descCard->setObjectName("descCard");
        descCard->setStyleSheet(
            "#descCard {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e7eb;"
            "  border-radius: 12px;"
            "}"
        );
        auto *descLayout = new QVBoxLayout(descCard);
        descLayout->setContentsMargins(20, 16, 20, 16);
        descLayout->setSpacing(6);

        auto *descTitle = new QLabel("📝 简介", descCard);
        descTitle->setStyleSheet("font-size: 14px; font-weight: 700; color: #111827;");
        descLayout->addWidget(descTitle);

        auto *descText = new QLabel(description, descCard);
        descText->setWordWrap(true);
        descText->setStyleSheet("font-size: 13px; color: #4b5563; line-height: 1.6;");
        descLayout->addWidget(descText);

        contentLayout->addWidget(descCard);
    }

    auto *versionsCard = new QFrame(content);
    versionsCard->setObjectName("versionsCard");
    versionsCard->setStyleSheet(
        "#versionsCard {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 12px;"
        "}"
    );
    auto *versionsCardLayout = new QVBoxLayout(versionsCard);
    versionsCardLayout->setContentsMargins(20, 16, 20, 16);
    versionsCardLayout->setSpacing(12);

    auto *verCardTitle = new QLabel("📦 版本列表  <span style='font-size:12px; color:#6b7280; font-weight:400;'>点击版本号选择下载方式</span>", versionsCard);
    verCardTitle->setStyleSheet("font-size: 14px; font-weight: 700; color: #111827;");
    versionsCardLayout->addWidget(verCardTitle);

    auto *verScroll = new QScrollArea(versionsCard);
    verScroll->setWidgetResizable(true);
    verScroll->setFrameShape(QFrame::NoFrame);
    verScroll->setStyleSheet("background-color: transparent;");

    auto *verContent = new QWidget(verScroll);
    auto *verContentLayout = new QVBoxLayout(verContent);
    verContentLayout->setContentsMargins(0, 0, 0, 0);
    verContentLayout->setSpacing(10);

    struct ModVersionEntry {
        QJsonObject fullObj;
        QString modVersionName;
        QString versionType;
        QStringList normalizedLoaders;
        QStringList exactGameVersions;
        QString datePublished;
        QString fileName;
        qint64 fileSize = 0;
    };

    QList<ModVersionEntry *> allEntries;

    QJsonArray realVersions;
    QString fetchWarn;
    if (source == "Modrinth") {
        const QString projectId = payload.value("project_id").toString(payload.value("id").toString(slug));
        QString err;
        realVersions = m_core.getModrinthProjectVersions(projectId, &err);
        if (!err.isEmpty()) {
            fetchWarn = err;
        }
    }

    auto normalizeVersionType = [](const QJsonObject &vobj) -> QString {
        const QString typeRaw = vobj.value("version_type").toString(vobj.value("versionType").toString()).trimmed().toLower();
        if (typeRaw == "release" || typeRaw == "stable") return "Release";
        if (typeRaw == "beta" || typeRaw == "prerelease") return "Beta";
        if (typeRaw == "alpha") return "Alpha";
        const QString name = vobj.value("version_number").toString(vobj.value("name").toString()).toLower();
        if (name.contains("-beta") || name.contains("-pre") || name.contains("-rc")) return "Beta";
        if (name.contains("-alpha") || name.contains("-snapshot") || name.contains("-exp")) return "Alpha";
        return "Release";
    };

    auto formatDateTime = [](const QString &iso) -> QString {
        if (iso.isEmpty()) return "";
        const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
        if (!dt.isValid()) return iso;
        return dt.toString("yyyy-MM-dd HH:mm");
    };

    if (source == "Modrinth" && !realVersions.isEmpty()) {
        for (const QJsonValue &vv : realVersions) {
            const QJsonObject vobjRaw = vv.toObject();
            if (vobjRaw.isEmpty()) continue;

            const QString verId = vobjRaw.value("id").toString(vobjRaw.value("version_number").toString());
            if (verId.isEmpty()) continue;

            QString modVerName = vobjRaw.value("version_number").toString(vobjRaw.value("name").toString(""));
            if (modVerName.isEmpty()) modVerName = verId;

            const QJsonArray gvsArr = vobjRaw.value("game_versions").toArray();
            QStringList exactGameVers;
            for (const QJsonValue &gv : gvsArr) {
                const QString gs = gv.toString();
                if (!gs.isEmpty()) exactGameVers << gs;
            }
            if (exactGameVers.isEmpty()) continue;

            const QJsonArray ldsArr = vobjRaw.value("loaders").toArray();
            QStringList rawLoaders;
            for (const QJsonValue &lv : ldsArr) {
                const QString ls = lv.toString();
                if (!ls.isEmpty()) rawLoaders << ls;
            }
            QSet<QString> seenLoaders;
            QStringList normalizedLoaders;
            for (const QString &raw : rawLoaders) {
                const QString nl = normalizeLoaderName(raw);
                if (nl.isEmpty() || seenLoaders.contains(nl)) continue;
                seenLoaders.insert(nl);
                normalizedLoaders << nl;
            }
            if (normalizedLoaders.isEmpty()) {
                const QJsonObject pf = modrinthPrimaryFile(vobjRaw);
                const QString fn = pf.value("filename").toString().toLower();
                if (fn.contains("fabric")) normalizedLoaders << "Fabric";
                else if (fn.contains("neoforge")) normalizedLoaders << "NeoForge";
                else if (fn.contains("forge")) normalizedLoaders << "Forge";
                else if (fn.contains("quilt")) normalizedLoaders << "Quilt";
                else normalizedLoaders << "Other";
            }

            QJsonObject vobj = vobjRaw;
            QJsonObject pf = modrinthPrimaryFile(vobj);
            if (!pf.isEmpty()) {
                vobj["primaryFile"] = pf;
            } else if (!vobj.contains("primaryFile")) {
                QJsonObject stub;
                stub["filename"] = modVerName + ".jar";
                stub["url"] = "";
                stub["size"] = 0;
                vobj["primaryFile"] = stub;
                pf = stub;
            }

            auto *entry = new ModVersionEntry();
            entry->fullObj = vobj;
            entry->modVersionName = modVerName;
            entry->versionType = normalizeVersionType(vobj);
            entry->normalizedLoaders = normalizedLoaders;
            entry->exactGameVersions = exactGameVers;
            entry->datePublished = vobj.value("date_published").toString(vobj.value("date_created").toString(vobj.value("date_modified").toString()));
            entry->fileName = pf.value("filename").toString(modVerName + ".jar");
            entry->fileSize = pf.value("size").toInteger(0);
            allEntries << entry;
        }
    } else {
        QStringList fallbackGameVers = gameVersionsList.mid(0, qMin(20, gameVersionsList.size()));
        QStringList fallbackLoaders = loadersList.isEmpty() ? QStringList{source == "Modrinth" ? "Fabric" : "Forge"} : loadersList;
        for (const QString &gv : fallbackGameVers) {
            for (const QString &loader : fallbackLoaders) {
                const QString modVerName = QString("v%1.%2").arg(gv).arg(randomInt(1, 10));
                QJsonObject vobj;
                vobj["id"] = QString("%1-%2-%3").arg(slug, gv, loader.toLower());
                vobj["name"] = modVerName;
                vobj["version_number"] = modVerName;
                vobj["game_versions"] = QJsonArray::fromStringList({gv});
                vobj["loaders"] = QJsonArray::fromStringList({loader.toLower()});
                QJsonObject fileObj;
                fileObj["url"] = QString("https://example.com/%1.jar").arg(slug);
                fileObj["filename"] = QString("%1-%2-%3.jar").arg(slug, gv, loader.toLower());
                fileObj["size"] = 1024 * randomInt(500, 5499);
                fileObj["primary"] = true;
                vobj["primaryFile"] = fileObj;
                vobj["files"] = QJsonArray({fileObj});
                auto *entry = new ModVersionEntry();
                entry->fullObj = vobj;
                entry->modVersionName = modVerName;
                entry->versionType = "Release";
                entry->normalizedLoaders = QStringList{loader};
                entry->exactGameVersions = QStringList{gv};
                entry->datePublished = QDateTime::currentDateTime().toString(Qt::ISODate);
                entry->fileName = fileObj["filename"].toString();
                entry->fileSize = fileObj["size"].toInteger(0);
                allEntries << entry;
            }
        }
    }

    QMap<QString, QList<ModVersionEntry *>> byExactGameVersion;
    for (ModVersionEntry *e : allEntries) {
        for (const QString &gv : e->exactGameVersions) {
            if (gv.isEmpty()) continue;
            byExactGameVersion[gv].append(e);
        }
    }

    for (auto it = byExactGameVersion.begin(); it != byExactGameVersion.end(); ++it) {
        QList<ModVersionEntry *> &list = it.value();
        std::sort(list.begin(), list.end(), [](const ModVersionEntry *a, const ModVersionEntry *b) {
            if (a->datePublished.isEmpty() && b->datePublished.isEmpty()) return false;
            if (a->datePublished.isEmpty()) return false;
            if (b->datePublished.isEmpty()) return true;
            const QDateTime dta = QDateTime::fromString(a->datePublished, Qt::ISODate);
            const QDateTime dtb = QDateTime::fromString(b->datePublished, Qt::ISODate);
            if (dta.isValid() && dtb.isValid()) return dta > dtb;
            return a->datePublished.compare(b->datePublished, Qt::CaseInsensitive) > 0;
        });
    }

    auto getReleaseFamily = [](const QString &gv) -> QString {
        const McVersionSeries s = parseMcVersionSeries(gv);
        if (s.kind == McVersionKind::NumericRelease && !s.series.isEmpty()) {
            return s.series;
        }
        if (!s.series.isEmpty()) return s.series;
        if (!s.raw.isEmpty()) return s.raw;
        return gv;
    };

    auto isSnapshotLike = [](const QString &gv) -> bool {
        const McVersionSeries s = parseMcVersionSeries(gv);
        return s.kind != McVersionKind::NumericRelease;
    };

    QStringList exactGvKeys = byExactGameVersion.keys();
    std::sort(exactGvKeys.begin(), exactGvKeys.end(), [](const QString &a, const QString &b) {
        return compareMajorVersionFamily(a, b) > 0;
    });

    QMap<QString, QPair<QStringList, QStringList>> byFamilyReleaseSnapshot;
    for (const QString &egv : exactGvKeys) {
        const QString fam = getReleaseFamily(egv);
        if (isSnapshotLike(egv)) {
            byFamilyReleaseSnapshot[fam].second.append(egv);
        } else {
            byFamilyReleaseSnapshot[fam].first.append(egv);
        }
    }

    QStringList familyKeys = byFamilyReleaseSnapshot.keys();
    std::sort(familyKeys.begin(), familyKeys.end(), [](const QString &a, const QString &b) {
        return compareMajorVersionFamily(a, b) > 0;
    });

    if (!fetchWarn.isEmpty()) {
        auto *warn = new QLabel(QString("⚠️ 读取完整版本列表失败，仅展示搜索页版本：%1").arg(fetchWarn), verContent);
        warn->setStyleSheet(
            "font-size: 12px; color: #92400e; padding: 8px 12px;"
            "background-color: #fef3c7; border-radius: 8px;"
        );
        warn->setWordWrap(true);
        verContentLayout->addWidget(warn);
    }

    auto makeVersionRow = [&](ModVersionEntry *e, const QString &displayForGv) -> QWidget * {
        class VersionRowFrame : public QFrame {
        public:
            std::function<void(QMouseEvent *)> clickHandler;
        protected:
            void mousePressEvent(QMouseEvent *ev) override { QFrame::mousePressEvent(ev); }
            void mouseReleaseEvent(QMouseEvent *ev) override {
                if (ev->button() == Qt::LeftButton && clickHandler) clickHandler(ev);
                QFrame::mouseReleaseEvent(ev);
            }
            void enterEvent(QEnterEvent *) override { setStyleSheet(objectName() + "{ background-color: #eff6ff !important; }"); }
            void leaveEvent(QEvent *) override { setStyleSheet(objectName() + "{ background-color: #ffffff; }"); }
        };

        auto *verRow = new VersionRowFrame();
        verRow->setObjectName("versionRow");
        verRow->setStyleSheet(
            "#versionRow {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e5e7eb;"
            "  border-radius: 8px;"
            "  cursor: pointer;"
            "}"
        );
        verRow->setCursor(Qt::PointingHandCursor);

        auto *vrLayout = new QHBoxLayout(verRow);
        vrLayout->setContentsMargins(12, 10, 12, 10);
        vrLayout->setSpacing(10);

        QString typeIcon;
        QString typeColor;
        QString typeBg;
        if (e->versionType == "Release") { typeIcon = "🟢"; typeColor = "#059669"; typeBg = "#d1fae5"; }
        else if (e->versionType == "Beta") { typeIcon = "🟡"; typeColor = "#d97706"; typeBg = "#fef3c7"; }
        else { typeIcon = "🔴"; typeColor = "#dc2626"; typeBg = "#fee2e2"; }

        auto *typeIconLab = new QLabel(typeIcon, verRow);
        typeIconLab->setStyleSheet("font-size: 14px;");
        vrLayout->addWidget(typeIconLab);

        auto *infoVBox = new QVBoxLayout();
        infoVBox->setContentsMargins(0, 0, 0, 0);
        infoVBox->setSpacing(3);

        auto *verNameRow = new QHBoxLayout();
        verNameRow->setContentsMargins(0, 0, 0, 0);
        verNameRow->setSpacing(6);

        QString displayName = e->modVersionName;
        if (!displayForGv.isEmpty()) {
            displayName += QString(" <span style='color:#6b7280; font-weight:500;'>for %1</span>").arg(displayForGv);
        }
        auto *nameLab = new QLabel(displayName, verRow);
        nameLab->setTextFormat(Qt::RichText);
        nameLab->setStyleSheet("font-size: 13px; font-weight: 700; color: #111827;");
        verNameRow->addWidget(nameLab);

        auto *typeBadge = new QLabel(e->versionType, verRow);
        typeBadge->setStyleSheet(QString(
            "font-size: 10px; color: %1; font-weight: 700;"
            "padding: 1px 7px; background-color: %2;"
            "border-radius: 5px;"
        ).arg(typeColor, typeBg));
        verNameRow->addWidget(typeBadge);

        for (const QString &lk : e->normalizedLoaders) {
            if (lk.isEmpty()) continue;
            QString lColor = "#3b82f6";
            QString lBg = "#dbeafe";
            if (lk == "Forge") { lColor = "#dc2626"; lBg = "#fee2e2"; }
            else if (lk == "NeoForge") { lColor = "#d97706"; lBg = "#fef3c7"; }
            else if (lk == "Quilt") { lColor = "#9333ea"; lBg = "#f3e8ff"; }
            else if (lk == "Other") { lColor = "#6b7280"; lBg = "#f3f4f6"; }
            auto *lb = new QLabel(lk, verRow);
            lb->setStyleSheet(QString(
                "font-size: 10px; color: %1; font-weight: 600;"
                "padding: 1px 7px; background-color: %2;"
                "border-radius: 5px;"
            ).arg(lColor, lBg));
            verNameRow->addWidget(lb);
        }
        verNameRow->addStretch();
        infoVBox->addLayout(verNameRow);

        auto *metaRow = new QHBoxLayout();
        metaRow->setContentsMargins(0, 0, 0, 0);
        metaRow->setSpacing(12);
        const QString dateStr = formatDateTime(e->datePublished);
        if (!dateStr.isEmpty()) {
            auto *dl = new QLabel("📅 " + dateStr, verRow);
            dl->setStyleSheet("font-size: 11px; color: #6b7280;");
            metaRow->addWidget(dl);
        }
        if (!e->fileName.isEmpty()) {
            auto *fl = new QLabel("📄 " + e->fileName, verRow);
            fl->setStyleSheet("font-size: 11px; color: #6b7280;");
            metaRow->addWidget(fl, 1);
        } else {
            metaRow->addStretch(1);
        }
        infoVBox->addLayout(metaRow);

        vrLayout->addLayout(infoVBox, 1);

        QString sizeStr;
        if (e->fileSize > 1024 * 1024) sizeStr = QString::number(e->fileSize / 1024.0 / 1024.0, 'f', 2) + " MB";
        else if (e->fileSize > 0) sizeStr = QString::number(e->fileSize / 1024.0, 'f', 1) + " KB";
        if (!sizeStr.isEmpty()) {
            auto *szLabel = new QLabel(sizeStr, verRow);
            szLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
            vrLayout->addWidget(szLabel);
        }

        auto *dlArrow = new QLabel("↧", verRow);
        dlArrow->setStyleSheet("color: #3b82f6; font-size: 16px; font-weight: bold;");
        vrLayout->addWidget(dlArrow);

        const QJsonObject pf = e->fullObj.value("primaryFile").toObject();
        const QJsonObject verCopy = e->fullObj;
        const QJsonObject fileCopy = pf;
        const QString ttCopy = targetType;
        verRow->clickHandler = [this, verCopy, fileCopy, ttCopy](QMouseEvent *) {
            showDownloadLocationDialog(verCopy, fileCopy, ttCopy, this);
        };

        return verRow;
    };

    auto createGroupWithHeader = [&](const QString &title, const QStringList &orderedExactGvs) -> QWidget * {
        auto *groupFrame = new QFrame(verContent);
        groupFrame->setObjectName("versionGroupFrame");
        groupFrame->setStyleSheet(
            "#versionGroupFrame {"
            "  background-color: #f9fafb;"
            "  border: 1px solid #e5e7eb;"
            "  border-radius: 10px;"
            "}"
        );

        auto *groupLayout = new QVBoxLayout(groupFrame);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(0);

        auto *groupHeader = new QFrame(groupFrame);
        groupHeader->setObjectName("groupHeader");
        groupHeader->setStyleSheet(
            "#groupHeader:hover { background-color: #f3f4f6; border-radius: 10px; }"
            "#groupHeader { cursor: pointer; }"
        );
        groupHeader->setCursor(Qt::PointingHandCursor);
        auto *groupHeaderLayout = new QHBoxLayout(groupHeader);
        groupHeaderLayout->setContentsMargins(14, 12, 14, 12);
        groupHeaderLayout->setSpacing(10);

        auto *expandIcon = new QLabel("▼", groupHeader);
        expandIcon->setStyleSheet(
            "color: #6b7280; font-size: 10px; font-weight: bold;"
            "transform: rotate(-90deg);"
        );
        expandIcon->setProperty("expanded", false);
        groupHeaderLayout->addWidget(expandIcon);

        auto *gvLabel = new QLabel(title, groupHeader);
        gvLabel->setStyleSheet("font-size: 13px; font-weight: 700; color: #111827;");
        groupHeaderLayout->addWidget(gvLabel);

        int totalCount = 0;
        for (const QString &egv : orderedExactGvs) {
            totalCount += byExactGameVersion[egv].size();
        }
        auto *countLabel = new QLabel(QString("%1 个版本").arg(totalCount), groupHeader);
        countLabel->setStyleSheet("font-size: 12px; color: #9ca3af;");
        groupHeaderLayout->addWidget(countLabel);
        groupHeaderLayout->addStretch();

        auto *subVersions = new QWidget(groupFrame);
        subVersions->setObjectName("subVersions");
        subVersions->hide();
        auto *subLayout = new QVBoxLayout(subVersions);
        subLayout->setContentsMargins(14, 0, 14, 12);
        subLayout->setSpacing(6);

        for (const QString &egv : orderedExactGvs) {
            if (!byExactGameVersion.contains(egv)) continue;

            auto *gvTitle = new QFrame(subVersions);
            auto *ltLayout = new QHBoxLayout(gvTitle);
            ltLayout->setContentsMargins(0, 10, 0, 2);
            ltLayout->setSpacing(6);
            auto *gvTitleLab = new QLabel(QString("• Minecraft %1").arg(egv), gvTitle);
            gvTitleLab->setStyleSheet("font-size: 12px; font-weight: 600; color: #374151;");
            ltLayout->addWidget(gvTitleLab);
            auto *cnt = new QLabel(QString("%1 个").arg(byExactGameVersion[egv].size()), gvTitle);
            cnt->setStyleSheet("font-size: 11px; color: #9ca3af;");
            ltLayout->addWidget(cnt);
            ltLayout->addStretch();
            subLayout->addWidget(gvTitle);

            for (ModVersionEntry *e : byExactGameVersion[egv]) {
                subLayout->addWidget(makeVersionRow(e, egv));
            }
        }

        QLabel *expandPtr = expandIcon;
        QWidget *subPtr = subVersions;

        class GroupClickHandler : public QObject {
        public:
            QLabel *expandIcon;
            QWidget *subWidget;
            MainWindow *mainWin;
            bool expanded = false;
        protected:
            bool eventFilter(QObject *watched, QEvent *event) override {
                if (event->type() == QEvent::MouseButtonRelease) {
                    expanded = !expanded;
                    if (expandIcon) {
                        expandIcon->setStyleSheet(QString(
                            "color: #6b7280; font-size: 10px; font-weight: bold;"
                            "transform: rotate(%1deg);"
                        ).arg(expanded ? 0 : -90));
                    }
                    if (mainWin && subWidget) {
                        mainWin->animateExpand(subWidget, expanded, 200);
                    }
                    return true;
                }
                return QObject::eventFilter(watched, event);
            }
        };
        auto *handler = new GroupClickHandler();
        handler->expandIcon = expandPtr;
        handler->subWidget = subPtr;
        handler->mainWin = this;
        groupHeader->installEventFilter(handler);
        handler->setParent(groupHeader);

        groupLayout->addWidget(groupHeader);
        groupLayout->addWidget(subVersions);
        return groupFrame;
    };

    for (const QString &fam : familyKeys) {
        const auto pair = byFamilyReleaseSnapshot[fam];
        const QStringList releases = pair.first;
        const QStringList snapshots = pair.second;
        if (!releases.isEmpty()) {
            verContentLayout->addWidget(createGroupWithHeader(
                QString("🎮 Minecraft %1 (正式版)").arg(fam),
                releases
            ));
        }
        if (!snapshots.isEmpty()) {
            verContentLayout->addWidget(createGroupWithHeader(
                QString("🎮 Minecraft %1 (快照)").arg(fam),
                snapshots
            ));
        }
    }

    verContentLayout->addStretch();
    verScroll->setWidget(verContent);
    versionsCardLayout->addWidget(verScroll, 1);
    contentLayout->addWidget(versionsCard, 1);

    contentLayout->addStretch();
    scrollArea->setWidget(content);

    appendLog(QString("显示模组详情: %1").arg(name));
    m_modsSubStack->setCurrentWidget(m_modsDetailPage);
    m_modsBackButton->setVisible(true);
}

void MainWindow::showDownloadLocationDialog(const QJsonObject &versionObj, const QJsonObject &fileObj, const QString &targetType, QWidget *parent)
{
    QWidget *p = parent ? parent : this;
    auto *dialog = new QDialog(p);
    dialog->setObjectName("downloadLocationDialog");
    dialog->setWindowTitle("选择保存位置");
    dialog->resize(480, 420);
    dialog->setMinimumSize(440, 380);
    dialog->setStyleSheet(
        "#downloadLocationDialog { background-color: #f9fafb; }"
        "QLabel { color: #111827; }"
    );

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    QString fileName = fileObj.value("filename").toString(versionObj.value("name").toString("mod.jar"));
    qint64 fileSize = fileObj.value("size").toInteger();
    QString sizeStr;
    if (fileSize > 1024 * 1024) sizeStr = QString::number(fileSize / 1024.0 / 1024.0, 'f', 2) + " MB";
    else if (fileSize > 0) sizeStr = QString::number(fileSize / 1024.0, 'f', 1) + " KB";

    auto *infoFrame = new QFrame(dialog);
    infoFrame->setStyleSheet(
        "QFrame {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 12px;"
        "}"
    );
    auto *infoLayout = new QVBoxLayout(infoFrame);
    infoLayout->setContentsMargins(16, 14, 16, 14);
    infoLayout->setSpacing(4);
    auto *dlTitle = new QLabel("📥 下载确认", infoFrame);
    dlTitle->setStyleSheet("font-size: 16px; font-weight: 700; color: #111827;");
    infoLayout->addWidget(dlTitle);
    auto *fnLabel = new QLabel("文件: <b>" + fileName + "</b>", infoFrame);
    fnLabel->setStyleSheet("font-size: 13px; color: #4b5563;");
    infoLayout->addWidget(fnLabel);
    if (!sizeStr.isEmpty()) {
        auto *szLabel = new QLabel("大小: " + sizeStr, infoFrame);
        szLabel->setStyleSheet("font-size: 13px; color: #6b7280;");
        infoLayout->addWidget(szLabel);
    }
    layout->addWidget(infoFrame);

    auto *promptLabel = new QLabel("请选择保存方式:", dialog);
    promptLabel->setStyleSheet("font-size: 13px; font-weight: 600; color: #374151;");
    layout->addWidget(promptLabel);

    QButtonGroup *locGroup = new QButtonGroup(dialog);
    locGroup->setExclusive(true);

    QList<DownloadOption> options = {
        { 1, "🎯 保存到当前实例", "下载到当前选中版本的实例文件夹 (mods 目录)", "#3b82f6" },
        { 2, "🌐 保存到全局游戏", "下载到全局 mods 文件夹，所有实例共享", "#059669" },
        { 3, "📁 自定义文件夹", "选择一个自定义文件夹保存", "#d97706" }
    };

    QList<QPushButton *> optButtons;
    for (const auto &opt : options) {
        auto *btn = new QPushButton(dialog);
        btn->setCheckable(true);
        btn->setFixedHeight(64);
        btn->setStyleSheet(QString(
            "QPushButton {"
            "  text-align: left; padding-left: 16px;"
            "  background-color: #ffffff;"
            "  border: 2px solid #e5e7eb;"
            "  border-radius: 12px;"
            "}"
            "QPushButton:hover {"
            "  border-color: %1;"
            "  background-color: #f9fafb;"
            "}"
            "QPushButton:checked {"
            "  border-color: %1;"
            "  background-color: %2;"
            "}"
        ).arg(opt.accentColor,
            opt.id == 1 ? "#dbeafe" :
            opt.id == 2 ? "#d1fae5" : "#fef3c7"));

        auto *btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(0, 0, 16, 0);
        btnLayout->setSpacing(2);
        auto *t1 = new QLabel(opt.title, btn);
        t1->setStyleSheet("font-size: 13px; font-weight: 700; color: #111827;");
        auto *t2 = new QLabel(opt.description, btn);
        t2->setStyleSheet("font-size: 11px; color: #6b7280;");
        btnLayout->addWidget(t1);
        btnLayout->addWidget(t2);

        locGroup->addButton(btn, opt.id);
        optButtons << btn;
        layout->addWidget(btn);
    }
    optButtons.first()->setChecked(true);

    auto *customEdit = new QLineEdit(dialog);
    customEdit->setPlaceholderText("点击右侧按钮选择文件夹...");
    customEdit->setEnabled(false);
    customEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 10px 14px; border: 1px solid #d1d5db;"
        "  border-radius: 10px; background-color: #ffffff;"
        "  font-size: 13px; color: #1f2937;"
        "}"
        "QLineEdit:disabled { background-color: #f9fafb; color: #9ca3af; }"
        "QLineEdit:focus { border: 2px solid #3b82f6; }"
    );
    auto *browseBtn = new QPushButton("浏览...", dialog);
    browseBtn->setEnabled(false);
    browseBtn->setFixedWidth(80);
    browseBtn->setStyleSheet(
        "QPushButton {"
        "  padding: 9px 14px; background-color: #f3f4f6;"
        "  color: #374151; font-weight: 600; border: none;"
        "  border-radius: 10px; font-size: 12px;"
        "}"
        "QPushButton:hover:!disabled { background-color: #e5e7eb; }"
        "QPushButton:disabled { color: #9ca3af; background-color: #f3f4f6; }"
    );
    auto *customRow = new QHBoxLayout();
    customRow->setSpacing(10);
    customRow->addWidget(customEdit, 1);
    customRow->addWidget(browseBtn);
    layout->addLayout(customRow);

    connect(locGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this, locGroup, customEdit, browseBtn](QAbstractButton *) {
        int id = locGroup->checkedId();
        bool isCustom = (id == 3);
        customEdit->setEnabled(isCustom);
        browseBtn->setEnabled(isCustom);
    });

    connect(browseBtn, &QPushButton::clicked, this, [this, dialog, customEdit]() {
        QString dir = QFileDialog::getExistingDirectory(dialog, "选择保存文件夹", QDir::homePath());
        if (!dir.isEmpty()) customEdit->setText(dir);
    });

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->setSpacing(10);

    auto *cancelBtn = new QPushButton("取消", dialog);
    cancelBtn->setFixedWidth(100);
    cancelBtn->setStyleSheet(
        "QPushButton {"
        "  padding: 10px 16px; background-color: #f3f4f6;"
        "  color: #374151; font-weight: 600; border: none;"
        "  border-radius: 10px; font-size: 13px;"
        "}"
        "QPushButton:hover { background-color: #e5e7eb; }"
    );
    connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto *okBtn = new QPushButton("开始下载", dialog);
    okBtn->setFixedWidth(120);
    okBtn->setStyleSheet(
        "QPushButton {"
        "  padding: 10px 16px; background-color: #3b82f6;"
        "  color: #ffffff; font-weight: 600; border: none;"
        "  border-radius: 10px; font-size: 13px;"
        "}"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1d4ed8; }"
    );
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    connect(okBtn, &QPushButton::clicked, this, [this, dialog, locGroup, customEdit, versionObj, fileObj, targetType]() {
        int id = locGroup->checkedId();
        QString customDir = customEdit->text().trimmed();
        if (id == 3 && customDir.isEmpty()) {
            showError(dialog, "请选择自定义文件夹");
            return;
        }
        downloadModToLocation(versionObj, fileObj, targetType, id, customDir, dialog);
        dialog->accept();
    });

    dialog->exec();
}

void MainWindow::showVersionInstallDialog(const QJsonObject &versionObj, QWidget *parent)
{
    QWidget *p = parent ? parent : this;
    auto *dialog = new QDialog(p);
    dialog->setObjectName("versionInstallDialog");
    dialog->setWindowTitle("安装版本");
    dialog->resize(480, 320);
    dialog->setStyleSheet("#versionInstallDialog { background-color: #f9fafb; }");

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(18);

    QString versionId = versionObj.value("id").toString(versionObj.value("version").toString("unknown"));
    QString verType = versionObj.value("type").toString("release");

    auto *infoFrame = new QFrame(dialog);
    infoFrame->setStyleSheet(
        "QFrame { background-color: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }"
    );
    auto *il = new QVBoxLayout(infoFrame);
    il->setContentsMargins(16, 14, 16, 14);
    il->setSpacing(6);
    auto *titleL = new QLabel("🎮 安装新版本", infoFrame);
    titleL->setStyleSheet("font-size: 16px; font-weight: 700; color: #111827;");
    il->addWidget(titleL);
    auto *idL = new QLabel("版本号: <b>" + versionId + "</b>", infoFrame);
    idL->setStyleSheet("font-size: 13px; color: #4b5563;");
    il->addWidget(idL);
    auto *typeL = new QLabel("类型: <b>" + verType + "</b>", infoFrame);
    typeL->setStyleSheet("font-size: 13px; color: #6b7280;");
    il->addWidget(typeL);
    layout->addWidget(infoFrame);

    auto *form = new QFormLayout();
    form->setSpacing(12);
    auto *nameEdit = new QLineEdit(dialog);
    nameEdit->setText(versionId);
    nameEdit->setPlaceholderText("请输入实例名称");
    nameEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 10px 14px; border: 1px solid #d1d5db;"
        "  border-radius: 10px; background-color: #ffffff;"
        "  font-size: 13px; color: #1f2937;"
        "}"
        "QLineEdit:focus { border: 2px solid #3b82f6; }"
    );
    auto *nameLabel = new QLabel("实例名称:", dialog);
    nameLabel->setStyleSheet("font-size: 13px; font-weight: 600; color: #374151;");
    form->addRow(nameLabel, nameEdit);
    layout->addLayout(form);

    auto *noteLabel = new QLabel("💡 下载完成后，游戏文件将保存到 .minecraft/versions/<实例名称>/", dialog);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("font-size: 12px; color: #6b7280; padding: 8px; background-color: #eff6ff; border-radius: 8px;");
    layout->addWidget(noteLabel);

    layout->addStretch();

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->setSpacing(10);

    auto *cancelBtn = new QPushButton("取消", dialog);
    cancelBtn->setFixedWidth(100);
    cancelBtn->setStyleSheet(
        "QPushButton { padding: 10px 16px; background-color: #f3f4f6; color: #374151; font-weight: 600; border: none; border-radius: 10px; font-size: 13px; }"
        "QPushButton:hover { background-color: #e5e7eb; }"
    );
    connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto *installBtn = new QPushButton("确认安装", dialog);
    installBtn->setFixedWidth(120);
    installBtn->setStyleSheet(
        "QPushButton { padding: 10px 16px; background-color: #3b82f6; color: #ffffff; font-weight: 600; border: none; border-radius: 10px; font-size: 13px; }"
        "QPushButton:hover { background-color: #2563eb; }"
    );
    btnRow->addWidget(installBtn);
    layout->addLayout(btnRow);

    connect(installBtn, &QPushButton::clicked, this, [this, dialog, nameEdit, versionObj]() {
        QString instName = nameEdit->text().trimmed();
        if (instName.isEmpty()) {
            showError(dialog, "请输入实例名称");
            return;
        }
        QString verId = versionObj.value("id").toString();
        appendLog(QString("开始安装版本 [%1] -> %2").arg(instName, verId));
        QString error;
        bool ok = m_core.installVanillaVersionWithDialog(verId, &error, dialog);
        if (!ok && !error.isEmpty()) {
            showError(dialog, error);
            return;
        }
        showInfo(dialog, QString("版本 %1 安装任务已启动，请查看日志进度").arg(instName));
        dialog->accept();
    });

    dialog->exec();
}

void MainWindow::downloadModToLocation(const QJsonObject &versionObj, const QJsonObject &fileObj, const QString &targetType, int locationMode, const QString &customDir, QWidget *parent)
{
    Q_UNUSED(targetType);
    QString fileUrl = fileObj.value("url").toString();
    QString fileName = fileObj.value("filename").toString(versionObj.value("name").toString("mod.jar"));
    QString versionNum = versionObj.value("version_number").toString(versionObj.value("id").toString("unknown"));
    if (fileUrl.isEmpty()) {
        showError(parent, "下载链接无效");
        return;
    }

    QString targetDir;
    QString modeDesc;

    QString gameDir = m_core.configStore()->config().minecraftDir;
    QString currentVer = currentManagedVersion();

    if (locationMode == 1) {
        if (currentVer.isEmpty()) {
            showError(parent, "当前没有选中的实例版本，请先在版本管理中选择一个实例");
            return;
        }
        const QString modsPath = m_core.versionModsDirectory(currentVer);
        if (!QDir().mkpath(modsPath)) {
            showError(parent, QString("无法创建模组目录: %1").arg(modsPath));
            return;
        }
        targetDir = QDir(modsPath).absolutePath();
        modeDesc = QString("当前实例 [%1]").arg(currentVer);
    } else if (locationMode == 2) {
        const QString modsPath = m_core.versionModsDirectory(QString());
        if (!QDir().mkpath(modsPath)) {
            showError(parent, QString("无法创建模组目录: %1").arg(modsPath));
            return;
        }
        targetDir = QDir(modsPath).absolutePath();
        modeDesc = "全局游戏";
    } else if (locationMode == 3) {
        if (customDir.isEmpty()) {
            showError(parent, "请指定自定义文件夹");
            return;
        }
        if (!QDir().mkpath(customDir)) {
            showError(parent, QString("无法创建自定义目录: %1").arg(customDir));
            return;
        }
        targetDir = QDir(customDir).absolutePath();
        modeDesc = "自定义文件夹";
    }

    QString error;
    QString fullPath = QDir(targetDir).filePath(fileName);
    appendLog(QString("下载模组[%1 %2] -> %3 (%4)").arg(fileName, versionNum, targetDir, modeDesc));

    auto *dlg = new DownloadProgressDialog(
        "下载中",
        QString("正在下载 %1\n保存到: %2 (%3)").arg(fileName, targetDir, modeDesc),
        parent ? parent : this
    );
    dlg->setBusy(true);
    dlg->show();

    DownloadManager *dm = new DownloadManager(m_core.networkManager(), this);
    connect(dm, &DownloadManager::downloadFinished, this, [this, dlg, dm, fileName, modeDesc, targetDir](bool success, const QString &err) {
        if (success) {
            dlg->setStatus(QString("✓ %1 下载完成\n保存到: %2 (%3)").arg(fileName, targetDir, modeDesc));
            appendLog(QString("✓ 下载完成: %1").arg(fileName));
            QTimer::singleShot(800, dlg, &QDialog::accept);
        } else {
            dlg->setStatus(QString("✗ 下载失败: %1").arg(err));
            appendLog(QString("✗ 下载失败: %1 - %2").arg(fileName, err));
        }
        dm->deleteLater();
    });

    dm->downloadFileSilent(QUrl(fileUrl), fullPath);
}

void MainWindow::refreshDownloadVersions()
{
    if (m_versionCenterStack) {
        m_versionCenterStack->setCurrentIndex(0);
    }

    QString error;
    m_allDownloadVersions = m_core.fetchVersionManifest(&error);
    if (!error.isEmpty()) {
        appendLog(QString("获取版本清单失败: %1").arg(error));
        if (m_versionCenterStack) {
            m_versionCenterStack->setCurrentIndex(1);
        }
        return;
    }

    populateDownloadVersionList();
    appendLog(QString("已获取版本清单，共 %1 个版本").arg(m_allDownloadVersions.size()));
}

QString MainWindow::versionTypeDisplayName(const QString &type)
{
    const QString t = type.trimmed().toLower();
    if (t == "release") return QStringLiteral("正式版");
    if (t == "snapshot") return QStringLiteral("快照版");
    if (t == "pending") return QStringLiteral("待定版");
    if (t == "old_beta") return QStringLiteral("远古 Beta");
    if (t == "old_alpha") return QStringLiteral("远古 Alpha");
    if (t.isEmpty()) return QStringLiteral("未知");
    return type;
}

QIcon MainWindow::versionIconFor(const QString &versionId, const QString &versionType)
{
    const QString id = versionId.toLower();
    const QString t = versionType.trimmed().toLower();

    static const QStringList aprilFoolsIds = {
        "15w14a", "16w14a", "1.14.3-pre3", "20w14infinite", "21w13a",
        "22w13oneblockatatime", "23w13a_or_b", "24w13a",
        "3d shareware v1.34", "snapshot 20w14infinite"
    };
    for (const QString &af : aprilFoolsIds) {
        if (id == af || id.contains(af)) {
            return QIcon(":/assets/version-icons/april_fools.png");
        }
    }
    if (id.contains("legacyfabric")) return QIcon(":/assets/version-icons/legacyfabric.png");
    if (id.contains("fabric")) return QIcon(":/assets/version-icons/fabric.png");
    if (id.contains("neoforge")) return QIcon(":/assets/version-icons/neoforge.png");
    if (id.contains("forge")) return QIcon(":/assets/version-icons/forge.png");
    if (id.contains("quilt")) return QIcon(":/assets/version-icons/quilt.png");
    if (id.contains("optifine")) return QIcon(":/assets/version-icons/optifine.png");
    if (id.contains("cleanroom")) return QIcon(":/assets/version-icons/cleanroom.png");

    if (t == "release") return QIcon(":/assets/version-icons/grass.png");
    if (t == "snapshot" || t == "pending") return QIcon(":/assets/version-icons/command.png");
    if (t == "old_beta" || t == "old_alpha") return QIcon(":/assets/version-icons/craft_table.png");
    return QIcon(":/assets/version-icons/chest.png");
}

void MainWindow::populateDownloadVersionList()
{
    if (!m_downloadVersionList) return;

    const QString selectedType = m_downloadVersionTypeCombo
        ? m_downloadVersionTypeCombo->currentData().toString()
        : QStringLiteral("all");
    const QString keyword = m_downloadVersionSearchEdit
        ? m_downloadVersionSearchEdit->text().trimmed()
        : QString();

    QString filterType = selectedType;
    if (selectedType == "old") {
        filterType = "all";
    }

    QJsonArray filteredVersions = m_core.filterVersionsByType(m_allDownloadVersions, filterType);

    if (selectedType == "old") {
        QJsonArray oldOnly;
        for (const QJsonValue &v : filteredVersions) {
            const QString t = v.toObject().value("type").toString();
            if (t == "old_beta" || t == "old_alpha") oldOnly.append(v);
        }
        filteredVersions = oldOnly;
    }

    const QSignalBlocker blocker(m_downloadVersionList);
    m_downloadVersionList->clear();

    int matchCount = 0;
    for (const QJsonValue &value : filteredVersions) {
        const QJsonObject object = value.toObject();
        const QString versionId = object.value("id").toString();
        if (versionId.isEmpty()) continue;
        if (!keyword.isEmpty() && !versionId.contains(keyword, Qt::CaseInsensitive)) continue;

        const QString versionType = object.value("type").toString();
        const QString releaseTime = object.value("releaseTime").toString();

        QString prettyTime = releaseTime;
        if (!prettyTime.isEmpty()) {
            prettyTime.replace("T", " ");
            if (prettyTime.endsWith("Z")) prettyTime.chop(1);
            if (prettyTime.size() > 16) prettyTime = prettyTime.left(16);
        }

        auto *item = new QListWidgetItem(m_downloadVersionList);
        item->setData(Qt::UserRole, QJsonDocument(object).toJson(QJsonDocument::Compact));
        item->setIcon(versionIconFor(versionId, versionType));
        item->setText(QString("%1\n%2 · %3")
                          .arg(versionId, versionTypeDisplayName(versionType), prettyTime));
        item->setSizeHint(QSize(0, 56));
        item->setToolTip(QString("%1 · %2 · %3")
                             .arg(versionId, versionTypeDisplayName(versionType), releaseTime));
        ++matchCount;
    }

    if (m_versionCenterStack) {
        m_versionCenterStack->setCurrentWidget(m_downloadVersionList);
    }

    appendLog(QString("已刷新下载版本列表，共 %1 个版本").arg(matchCount));
}

void MainWindow::populateDownloadVersionsTable()
{
    populateDownloadVersionList();
}

void MainWindow::downloadSelectedVersion()
{
    QJsonObject versionObj = m_selectedDownloadVersion;
    if (versionObj.isEmpty()) {
        if (auto *item = m_downloadVersionList ? m_downloadVersionList->currentItem() : nullptr) {
            versionObj = QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray()).object();
        }
    }
    if (versionObj.isEmpty()) {
        showError(this, "请先在列表中选择要安装的版本");
        return;
    }

    if (!saveUiToConfig()) {
        return;
    }

    showVersionInstallPage(versionObj);
}

QWidget *MainWindow::buildVersionManagementTab()
{
    auto *page = new QWidget(this);
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *navWidget = new QFrame(page);
    navWidget->setFixedWidth(220);
    navWidget->setObjectName("manageNavPanel");
    navWidget->setStyleSheet(
        "#manageNavPanel {"
        "  background-color: #f9fafb;"
        "  border-right: 1px solid #e5e7eb;"
        "}"
    );
    auto *navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(16, 20, 16, 20);
    navLayout->setSpacing(6);

    auto *navTitle = new QLabel("版本管理", navWidget);
    navTitle->setStyleSheet("font-size: 16px; font-weight: 700; margin-bottom: 8px;");
    navLayout->addWidget(navTitle);

    m_manageNavGroup = new QButtonGroup(this);
    m_manageNavGroup->setExclusive(true);
    m_manageNavButtons.clear();

    QStringList categories = {"版本", "配置", "模组", "世界", "加载器"};
    QStringList categoryIcons = {"🎮", "⚙️", "🧩", "🌍", "🔌"};
    for (int i = 0; i < categories.size(); ++i) {
        auto *btn = new QPushButton(categoryIcons[i] + "  " + categories[i], navWidget);
        btn->setCheckable(true);
        btn->setFixedHeight(44);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  text-align: left; padding-left: 14px;"
            "  background: transparent; border: none; border-radius: 8px;"
            "  font-size: 14px; font-weight: 500; color: #6b7280;"
            "}"
            "QPushButton:hover { background-color: rgba(59, 130, 246, 0.08); color: #3b82f6; }"
            "QPushButton:checked {"
            "  background-color: rgba(59, 130, 246, 0.12); color: #3b82f6;"
            "  font-weight: 600;"
            "}"
        );
        m_manageNavGroup->addButton(btn, i);
        m_manageNavButtons.append(btn);
        navLayout->addWidget(btn);
    }
    navLayout->addStretch();

    auto *rightContainer = new QWidget(page);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_manageStackedWidget = new QStackedWidget(rightContainer);
    m_manageStackedWidget->setFrameShape(QFrame::NoFrame);

    QSize buttonSize(120, 32);
    int comboBoxHeight = 30;
    int lineEditHeight = 30;
    int spinBoxHeight = 30;

    auto *versionBox = new QGroupBox("当前管理版本");
    versionBox->setMinimumHeight(80);
    auto *versionLayout = new QHBoxLayout(versionBox);
    versionLayout->addWidget(new QLabel("版本"));
    m_manageVersionCombo = new QComboBox(versionBox);
    m_manageVersionCombo->setMinimumHeight(comboBoxHeight);
    auto *refreshVersionsButton = new QPushButton("刷新版本列表", versionBox);
    refreshVersionsButton->setMinimumSize(buttonSize);
    auto *openInstanceButton = new QPushButton("打开实例目录", versionBox);
    openInstanceButton->setMinimumSize(buttonSize);
    auto *deleteVersionButton = new QPushButton("删除该版本", versionBox);
    deleteVersionButton->setMinimumSize(buttonSize);
    versionLayout->addWidget(m_manageVersionCombo, 1);
    versionLayout->addWidget(refreshVersionsButton);
    versionLayout->addWidget(openInstanceButton);
    versionLayout->addWidget(deleteVersionButton);

    auto *profileBox = new QGroupBox("版本隔离与启动配置");
    profileBox->setMinimumHeight(350);
    auto *profileLayout = new QFormLayout(profileBox);
    profileLayout->setVerticalSpacing(10);
    m_manageIsolationCheck = new QCheckBox("启用当前版本隔离", profileBox);
    m_manageJavaPathEdit = new QLineEdit(profileBox);
    m_manageJavaPathEdit->setMinimumHeight(lineEditHeight);
    m_manageJavaArgsEdit = new QLineEdit(profileBox);
    m_manageJavaArgsEdit->setMinimumHeight(lineEditHeight);
    m_manageGameArgsEdit = new QLineEdit(profileBox);
    m_manageGameArgsEdit->setMinimumHeight(lineEditHeight);
    m_manageMinMemorySpin = new QSpinBox(profileBox);
    m_manageMinMemorySpin->setMinimumHeight(spinBoxHeight);
    m_manageMaxMemorySpin = new QSpinBox(profileBox);
    m_manageMaxMemorySpin->setMinimumHeight(spinBoxHeight);
    m_manageInstanceDirLabel = new QLabel("-", profileBox);
    m_manageModsDirLabel = new QLabel("-", profileBox);
    m_manageWorldsDirLabel = new QLabel("-", profileBox);
    m_manageBaseVersionLabel = new QLabel("-", profileBox);
    auto *saveProfileButton = new QPushButton("保存当前版本配置", profileBox);
    saveProfileButton->setMinimumHeight(36);
    saveProfileButton->setMinimumWidth(160);

    m_manageMinMemorySpin->setRange(512, 65536);
    m_manageMaxMemorySpin->setRange(512, 65536);
    m_manageInstanceDirLabel->setWordWrap(true);
    m_manageModsDirLabel->setWordWrap(true);
    m_manageWorldsDirLabel->setWordWrap(true);
    m_manageBaseVersionLabel->setWordWrap(true);

    auto *javaRow = new QWidget(profileBox);
    auto *javaRowLayout = new QHBoxLayout(javaRow);
    javaRowLayout->setContentsMargins(0, 0, 0, 0);
    auto *browseJavaButton = new QPushButton("浏览", javaRow);
    browseJavaButton->setMinimumSize(80, lineEditHeight);
    auto *detectJavaButton = new QPushButton("自动探测", javaRow);
    detectJavaButton->setMinimumSize(100, lineEditHeight);
    javaRowLayout->addWidget(m_manageJavaPathEdit, 1);
    javaRowLayout->addWidget(browseJavaButton);
    javaRowLayout->addWidget(detectJavaButton);

    profileLayout->addRow("基础游戏版本", m_manageBaseVersionLabel);
    profileLayout->addRow("", m_manageIsolationCheck);
    profileLayout->addRow("实例目录", m_manageInstanceDirLabel);
    profileLayout->addRow("模组目录", m_manageModsDirLabel);
    profileLayout->addRow("世界目录", m_manageWorldsDirLabel);
    profileLayout->addRow("Java 路径", javaRow);
    profileLayout->addRow("JVM 参数", m_manageJavaArgsEdit);
    profileLayout->addRow("附加游戏参数", m_manageGameArgsEdit);
    profileLayout->addRow("最小内存 (MB)", m_manageMinMemorySpin);
    profileLayout->addRow("最大内存 (MB)", m_manageMaxMemorySpin);
    profileLayout->addRow("", saveProfileButton);

    auto *modsBox = new QGroupBox("模组管理");
    modsBox->setMinimumHeight(280);
    auto *modsLayout = new QVBoxLayout(modsBox);
    auto *modsButtonRow = new QHBoxLayout();
    auto *refreshModsButton = new QPushButton("刷新模组", modsBox);
    refreshModsButton->setMinimumSize(buttonSize);
    auto *openModsButton = new QPushButton("打开模组目录", modsBox);
    openModsButton->setMinimumSize(buttonSize);
    auto *toggleModButton = new QPushButton("启用/禁用", modsBox);
    toggleModButton->setMinimumSize(buttonSize);
    auto *removeModButton = new QPushButton("删除模组", modsBox);
    removeModButton->setMinimumSize(buttonSize);
    modsButtonRow->addWidget(refreshModsButton);
    modsButtonRow->addWidget(openModsButton);
    modsButtonRow->addWidget(toggleModButton);
    modsButtonRow->addWidget(removeModButton);
    modsButtonRow->addStretch();
    m_manageModsTable = new QTableWidget(modsBox);
    m_manageModsTable->setMinimumHeight(180);
    m_manageModsTable->setColumnCount(4);
    m_manageModsTable->setHorizontalHeaderLabels({"文件名", "状态", "作用域", "修改时间"});
    m_manageModsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_manageModsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_manageModsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_manageModsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_manageModsTable->setAlternatingRowColors(true);
    m_manageModsTable->verticalHeader()->setDefaultSectionSize(28);
    modsLayout->addLayout(modsButtonRow);
    modsLayout->addWidget(m_manageModsTable);

    auto *worldsBox = new QGroupBox("世界管理");
    worldsBox->setMinimumHeight(280);
    auto *worldsLayout = new QVBoxLayout(worldsBox);
    auto *worldsButtonRow = new QHBoxLayout();
    auto *refreshWorldsButton = new QPushButton("刷新世界", worldsBox);
    refreshWorldsButton->setMinimumSize(buttonSize);
    auto *openWorldsButton = new QPushButton("打开世界目录", worldsBox);
    openWorldsButton->setMinimumSize(buttonSize);
    auto *removeWorldButton = new QPushButton("删除世界", worldsBox);
    removeWorldButton->setMinimumSize(buttonSize);
    worldsButtonRow->addWidget(refreshWorldsButton);
    worldsButtonRow->addWidget(openWorldsButton);
    worldsButtonRow->addWidget(removeWorldButton);
    worldsButtonRow->addStretch();
    m_manageWorldsTable = new QTableWidget(worldsBox);
    m_manageWorldsTable->setMinimumHeight(180);
    m_manageWorldsTable->setColumnCount(3);
    m_manageWorldsTable->setHorizontalHeaderLabels({"世界名称", "最近修改", "目录"});
    m_manageWorldsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_manageWorldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_manageWorldsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_manageWorldsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_manageWorldsTable->setAlternatingRowColors(true);
    m_manageWorldsTable->verticalHeader()->setDefaultSectionSize(28);
    worldsLayout->addLayout(worldsButtonRow);
    worldsLayout->addWidget(m_manageWorldsTable);

    auto *loadersBox = new QGroupBox("加载器管理");
    loadersBox->setMinimumHeight(350);
    auto *loadersLayout = new QVBoxLayout(loadersBox);
    auto *loaderActionRow = new QHBoxLayout();
    loaderActionRow->addWidget(new QLabel("加载器类型:"));
    m_manageLoaderTypeCombo = new QComboBox(loadersBox);
    m_manageLoaderTypeCombo->setMinimumHeight(comboBoxHeight);
    m_manageLoaderTypeCombo->addItems({"fabric", "forge", "neoforge", "quilt"});
    m_manageFetchLoaderVersionsButton = new QPushButton("获取可安装版本", loadersBox);
    m_manageFetchLoaderVersionsButton->setMinimumSize(buttonSize);
    m_manageInstallLoaderButton = new QPushButton("安装选中加载器", loadersBox);
    m_manageInstallLoaderButton->setMinimumSize(buttonSize);
    m_manageReinstallLoaderButton = new QPushButton("重装并替换同类", loadersBox);
    m_manageReinstallLoaderButton->setMinimumSize(buttonSize);
    m_manageUninstallLoaderButton = new QPushButton("卸载已装加载器", loadersBox);
    m_manageUninstallLoaderButton->setMinimumSize(buttonSize);
    loaderActionRow->addWidget(m_manageLoaderTypeCombo);
    loaderActionRow->addWidget(m_manageFetchLoaderVersionsButton);
    loaderActionRow->addWidget(m_manageInstallLoaderButton);
    loaderActionRow->addWidget(m_manageReinstallLoaderButton);
    loaderActionRow->addWidget(m_manageUninstallLoaderButton);
    loaderActionRow->addStretch();

    auto *loaderTables = new QSplitter(Qt::Horizontal, loadersBox);
    loaderTables->setMinimumHeight(220);
    m_manageInstalledLoadersTable = new QTableWidget(loaderTables);
    m_manageInstalledLoadersTable->setColumnCount(3);
    m_manageInstalledLoadersTable->setHorizontalHeaderLabels({"版本 ID", "类型", "继承自"});
    m_manageInstalledLoadersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_manageInstalledLoadersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_manageInstalledLoadersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_manageInstalledLoadersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_manageInstalledLoadersTable->setAlternatingRowColors(true);
    m_manageInstalledLoadersTable->verticalHeader()->setDefaultSectionSize(28);

    m_manageAvailableLoadersTable = new QTableWidget(loaderTables);
    m_manageAvailableLoadersTable->setColumnCount(2);
    m_manageAvailableLoadersTable->setHorizontalHeaderLabels({"加载器版本", "支持的游戏版本"});
    m_manageAvailableLoadersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_manageAvailableLoadersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_manageAvailableLoadersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_manageAvailableLoadersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_manageAvailableLoadersTable->setAlternatingRowColors(true);
    m_manageAvailableLoadersTable->verticalHeader()->setDefaultSectionSize(28);

    loadersLayout->addLayout(loaderActionRow);
    loadersLayout->addWidget(loaderTables, 1);

    auto makeManagePage = [&](const QString &titleText, const QString &subtitle, QWidget *box) -> QWidget * {
        auto *pageRoot = new QWidget(m_manageStackedWidget);
        auto *root = new QVBoxLayout(pageRoot);
        root->setContentsMargins(0, 0, 0, 0);

        auto *scroll = new QScrollArea(pageRoot);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto *content = new QWidget(scroll);
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(24, 24, 24, 24);
        contentLayout->setSpacing(18);

        auto *title = new QLabel(titleText, content);
        title->setStyleSheet("font-size: 20px; font-weight: 700; margin-bottom: 4px;");
        contentLayout->addWidget(title);

        if (!subtitle.isEmpty()) {
            auto *sub = new QLabel(subtitle, content);
            sub->setWordWrap(true);
            sub->setStyleSheet("font-size: 13px; color: #6b7280; margin-bottom: 8px;");
            contentLayout->addWidget(sub);
        }

        box->setParent(content);
        box->setStyleSheet(groupBoxStyle());
        contentLayout->addWidget(box);
        contentLayout->addStretch();
        scroll->setWidget(content);
        root->addWidget(scroll);
        return pageRoot;
    };

    m_manageStackedWidget->addWidget(makeManagePage("版本管理", "选择要操作的实例版本，可查看、刷新、打开目录或删除已安装版本。", versionBox));
    m_manageStackedWidget->addWidget(makeManagePage("版本配置", "为当前版本单独配置 Java 路径、内存、参数以及版本隔离等选项。", profileBox));
    m_manageStackedWidget->addWidget(makeManagePage("模组管理", "查看、启用/禁用或删除当前版本下安装的模组文件。", modsBox));
    m_manageStackedWidget->addWidget(makeManagePage("世界管理", "管理当前版本下保存的世界存档，可查看或删除。", worldsBox));
    m_manageStackedWidget->addWidget(makeManagePage("加载器管理", "为当前版本安装、重装或卸载 Fabric / Forge / NeoForge / Quilt 模组加载器。", loadersBox));

    for (int i = 0; i < m_manageNavButtons.size(); ++i) {
        connect(m_manageNavButtons[i], &QPushButton::clicked, this, [this, i] {
            animateButton(m_manageNavButtons[i]);
            m_manageStackedWidget->setCurrentIndex(i);
        });
    }

    rightLayout->addWidget(m_manageStackedWidget, 1);

    pageLayout->addWidget(navWidget);
    pageLayout->addWidget(rightContainer, 1);

    if (!m_manageNavButtons.isEmpty()) {
        m_manageNavButtons.first()->setChecked(true);
        m_manageStackedWidget->setCurrentIndex(0);
    }

    connect(m_manageVersionCombo, &QComboBox::currentTextChanged, this, [this](const QString &versionId) {
        syncSelectedVersion(versionId, m_manageVersionCombo);
        refreshVersionManagementUi();
    });
    connect(refreshVersionsButton, &QPushButton::clicked, this, [this] {
        refreshInstalledVersionsUi();
        refreshVersionManagementUi();
    });
    connect(openInstanceButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        if (versionId.isEmpty()) {
            showError(this, "当前没有可管理的版本。");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_core.versionInstanceDirectory(versionId)));
    });
    connect(deleteVersionButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        if (versionId.isEmpty()) {
            showError(this, "请先选择要删除的版本。");
            return;
        }
        if (QMessageBox::question(this,
                                  "Kill",
                                  QString("确认删除版本 %1 以及对应实例目录吗？").arg(versionId))
            != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!m_core.uninstallVersion(versionId, &error)) {
            showError(this, error);
            return;
        }
        appendLog(QString("已删除版本: %1").arg(versionId));
        refreshInstalledVersionsUi();
        refreshVersionManagementUi();
    });

    connect(m_manageIsolationCheck, &QCheckBox::toggled, this, [this] {
        updateVersionProfileEditorState();
    });
    connect(browseJavaButton, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, "选择 javaw.exe", m_manageJavaPathEdit->text(), "Java (javaw.exe)");
        if (!file.isEmpty()) {
            m_manageJavaPathEdit->setText(file);
        }
    });
    connect(detectJavaButton, &QPushButton::clicked, this, [this] {
        const QStringList candidates = m_core.discoverJavaExecutables();
        if (candidates.isEmpty()) {
            showError(this, "没有探测到可用 Java。");
            return;
        }
        m_manageJavaPathEdit->setText(candidates.first());
    });
    connect(saveProfileButton, &QPushButton::clicked, this, [this] {
        if (!saveCurrentVersionProfile()) {
            return;
        }
        refreshVersionManagementUi();
        showInfo(this, "当前版本配置已保存。");
    });

    connect(refreshModsButton, &QPushButton::clicked, this, &MainWindow::refreshVersionModsUi);
    connect(openModsButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        if (versionId.isEmpty()) {
            showError(this, "请先选择版本。");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_core.versionModsDirectory(versionId)));
    });
    connect(toggleModButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        const int row = m_manageModsTable->currentRow();
        if (versionId.isEmpty() || row < 0) {
            showError(this, "请先选择要管理的模组。");
            return;
        }
        const QString fileName = m_manageModsTable->item(row, 0)->data(Qt::UserRole).toString();
        const bool enabled = m_manageModsTable->item(row, 1)->data(Qt::UserRole).toBool();
        QString error;
        if (!m_core.setModEnabled(versionId, fileName, !enabled, &error)) {
            showError(this, error);
            return;
        }
        refreshVersionModsUi();
    });
    connect(removeModButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        const int row = m_manageModsTable->currentRow();
        if (versionId.isEmpty() || row < 0) {
            showError(this, "请先选择要删除的模组。");
            return;
        }
        const QString fileName = m_manageModsTable->item(row, 0)->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, "Kill", QString("确认删除模组 %1 吗？").arg(fileName)) != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!m_core.removeModFile(versionId, fileName, &error)) {
            showError(this, error);
            return;
        }
        refreshVersionModsUi();
    });

    connect(refreshWorldsButton, &QPushButton::clicked, this, &MainWindow::refreshVersionWorldsUi);
    connect(openWorldsButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        if (versionId.isEmpty()) {
            showError(this, "请先选择版本。");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_core.versionWorldsDirectory(versionId)));
    });
    connect(removeWorldButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        const int row = m_manageWorldsTable->currentRow();
        if (versionId.isEmpty() || row < 0) {
            showError(this, "请先选择要删除的世界。");
            return;
        }
        const QString worldName = m_manageWorldsTable->item(row, 0)->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, "Kill", QString("确认删除世界 %1 吗？").arg(worldName)) != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!m_core.removeWorld(versionId, worldName, &error)) {
            showError(this, error);
            return;
        }
        refreshVersionWorldsUi();
    });

    connect(m_manageFetchLoaderVersionsButton, &QPushButton::clicked, this, &MainWindow::refreshAvailableVersionLoaders);
    connect(m_manageLoaderTypeCombo, &QComboBox::currentTextChanged, this, [this] {
        m_manageAvailableLoadersTable->setRowCount(0);
    });
    connect(m_manageInstallLoaderButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        const int row = m_manageAvailableLoadersTable->currentRow();
        if (versionId.isEmpty() || row < 0) {
            showError(this, "请先选择要安装的加载器版本。");
            return;
        }
        const QString baseVersion = m_core.versionBaseGameVersion(versionId);
        const QString loaderType = m_manageLoaderTypeCombo->currentText();
        const QString loaderVersion = m_manageAvailableLoadersTable->item(row, 0)->data(Qt::UserRole).toString();
        QString error;
        if (!m_core.installLoaderWithDialog(baseVersion, loaderType, loaderVersion, &error, this)) {
            if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                showError(this, error);
            }
            return;
        }
        refreshInstalledVersionsUi();
        refreshVersionManagementUi();
        showInfo(this, QString("已为 %1 安装 %2 %3。").arg(baseVersion, loaderType, loaderVersion));
    });
    connect(m_manageReinstallLoaderButton, &QPushButton::clicked, this, [this] {
        const QString versionId = currentManagedVersion();
        const int row = m_manageAvailableLoadersTable->currentRow();
        if (versionId.isEmpty() || row < 0) {
            showError(this, "请先选择要重装的加载器版本。");
            return;
        }
        const QString baseVersion = m_core.versionBaseGameVersion(versionId);
        const QString loaderType = m_manageLoaderTypeCombo->currentText();
        const QJsonArray installedLoaders = m_core.scanInstalledLoaders(versionId);
        for (const QJsonValue &value : installedLoaders) {
            const QJsonObject loader = value.toObject();
            if (loader.value("loaderType").toString() == loaderType) {
                QString removeError;
                const QString loaderJsonId = loader.value("id").toString();
                const QString target = QFileInfo(loaderJsonId).fileName().endsWith(".json", Qt::CaseInsensitive)
                                         ? loaderJsonId
                                         : QString();
                if (!target.isEmpty()) {
                    if (!m_core.uninstallLoaderFromVersion(baseVersion, loaderType, target, &removeError)) {
                        showError(this, removeError);
                        return;
                    }
                } else {
                    if (!m_core.uninstallVersion(loaderJsonId, &removeError)) {
                        showError(this, removeError);
                        return;
                    }
                }
            }
        }
        const QString loaderVersion = m_manageAvailableLoadersTable->item(row, 0)->data(Qt::UserRole).toString();
        QString error;
        if (!m_core.installLoaderWithDialog(baseVersion, loaderType, loaderVersion, &error, this)) {
            if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                showError(this, error);
            }
            return;
        }
        refreshInstalledVersionsUi();
        refreshVersionManagementUi();
        showInfo(this, QString("已重装 %1 %2。").arg(loaderType, loaderVersion));
    });
    connect(m_manageUninstallLoaderButton, &QPushButton::clicked, this, [this] {
        const int row = m_manageInstalledLoadersTable->currentRow();
        if (row < 0) {
            showError(this, "请先选择要卸载的加载器。");
            return;
        }
        const QString versionId = currentManagedVersion();
        const QString baseVersion = m_core.versionBaseGameVersion(versionId);
        const QString loaderJsonId = m_manageInstalledLoadersTable->item(row, 0)->data(Qt::UserRole).toString();
        const QString loaderType = m_manageInstalledLoadersTable->item(row, 0)->data(Qt::UserRole + 1).toString();
        if (QMessageBox::question(this, "Kill", QString("确认卸载加载器 %1 吗？").arg(loaderJsonId)) != QMessageBox::Yes) {
            return;
        }
        QString error;
        const bool isJson = QFileInfo(loaderJsonId).fileName().endsWith(".json", Qt::CaseInsensitive);
        bool ok = false;
        if (isJson) {
            ok = m_core.uninstallLoaderFromVersion(baseVersion, loaderType, loaderJsonId, &error);
        } else {
            ok = m_core.uninstallVersion(loaderJsonId, &error);
        }
        if (!ok) {
            showError(this, error);
            return;
        }
        refreshInstalledVersionsUi();
        refreshVersionManagementUi();
    });

    connect(m_manageInstalledLoadersTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0) return;
        const QString versionId = currentManagedVersion();
        const QString baseVersion = m_core.versionBaseGameVersion(versionId);
        const QString loaderJsonId = m_manageInstalledLoadersTable->item(row, 0)->data(Qt::UserRole).toString();
        const QString loaderType = m_manageInstalledLoadersTable->item(row, 0)->data(Qt::UserRole + 1).toString();
        const bool isJson = QFileInfo(loaderJsonId).fileName().endsWith(".json", Qt::CaseInsensitive);
        if (!isJson) return;
        QString error;
        if (m_core.setVersionSelectedLoader(baseVersion, loaderType, loaderJsonId, &error)) {
            refreshInstalledVersionsUi();
            refreshVersionManagementUi();
            showInfo(this, QString("已将 %1 设为 %2 的启动加载器。").arg(loaderJsonId, baseVersion));
        } else {
            showError(this, error);
        }
    });

    return page;
}

QWidget *MainWindow::buildInstallLoaderTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *filterBox = new QGroupBox("加载器选项", page);
    auto *filterLayout = new QHBoxLayout(filterBox);
    
    filterLayout->addWidget(new QLabel("游戏版本:"));
    m_loaderGameVersionCombo = new QComboBox(filterBox);
    m_loaderGameVersionCombo->setEditable(true);
    filterLayout->addWidget(m_loaderGameVersionCombo);
    
    filterLayout->addWidget(new QLabel("加载器类型:"));
    m_loaderTypeCombo = new QComboBox(filterBox);
    m_loaderTypeCombo->addItems({"fabric", "forge", "neoforge", "quilt"});
    filterLayout->addWidget(m_loaderTypeCombo);
    
    m_refreshLoadersButton = new QPushButton("获取版本", filterBox);
    filterLayout->addWidget(m_refreshLoadersButton);
    
    m_installLoaderButton = new QPushButton("安装选中加载器", filterBox);
    filterLayout->addWidget(m_installLoaderButton);
    
    layout->addWidget(filterBox);

    m_loaderVersionsTable = new QTableWidget(page);
    m_loaderVersionsTable->setColumnCount(2);
    m_loaderVersionsTable->setHorizontalHeaderLabels({"加载器版本", "支持的游戏版本"});
    m_loaderVersionsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_loaderVersionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_loaderVersionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_loaderVersionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_loaderVersionsTable->setAlternatingRowColors(true);
    
    layout->addWidget(m_loaderVersionsTable, 1);

    connect(m_refreshLoadersButton, &QPushButton::clicked, this, [this]() {
        refreshLoaderVersions();
    });
    
    connect(m_installLoaderButton, &QPushButton::clicked, this, [this] {
        installSelectedLoader();
    });

    return page;
}

void MainWindow::refreshLoaderVersions()
{
    const QString gameVersion = m_loaderGameVersionCombo->currentText();
    const QString loaderType = m_loaderTypeCombo->currentText();
    
    if (gameVersion.isEmpty()) {
        showError(this, "请先输入或选择游戏版本！");
        return;
    }
    
    QString error;
    m_allLoaderVersions = m_core.fetchLoaderVersions(gameVersion, loaderType, &error);
    if (!error.isEmpty()) {
        showError(this, error);
        return;
    }
    
    m_loaderVersionsTable->setRowCount(0);
    
    for (const QJsonValue &value : m_allLoaderVersions) {
        QString loaderVersion;
        QString supportedGameVersion = gameVersion;
        
        if (value.isObject()) {
            const QJsonObject obj = value.toObject();
            if (loaderType == "fabric" || loaderType == "quilt") {
                loaderVersion = obj.value("loader").toObject().value("version").toString();
            } else if (loaderType == "forge") {
                loaderVersion = obj.value("version").toString();
            } else if (loaderType == "neoforge") {
                loaderVersion = obj.value("version").toString();
            }
        } else if (value.isString()) {
            loaderVersion = value.toString();
        }
        
        if (loaderVersion.isEmpty()) continue;
        
        const int row = m_loaderVersionsTable->rowCount();
        m_loaderVersionsTable->insertRow(row);
        
        auto *versionItem = new QTableWidgetItem(loaderVersion);
        versionItem->setData(Qt::UserRole, loaderVersion);
        m_loaderVersionsTable->setItem(row, 0, versionItem);
        m_loaderVersionsTable->setItem(row, 1, new QTableWidgetItem(supportedGameVersion));
    }
    
    appendLog(QString("已获取 %1 个 %2 加载器版本").arg(m_loaderVersionsTable->rowCount()).arg(loaderType));
}

void MainWindow::installSelectedLoader()
{
    const int row = m_loaderVersionsTable->currentRow();
    if (row < 0) {
        showError(this, "请先选择要安装的加载器版本！");
        return;
    }
    
    const QString gameVersion = m_loaderGameVersionCombo->currentText();
    const QString loaderType = m_loaderTypeCombo->currentText();
    const QString loaderVersion = m_loaderVersionsTable->item(row, 0)->data(Qt::UserRole).toString();
    
    if (!saveUiToConfig()) {
        return;
    }
    
    QString error;
    if (!m_core.installLoaderWithDialog(gameVersion, loaderType, loaderVersion, &error, this)) {
        if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
            appendLog(QString("安装加载器失败 [%1 %2]: %3").arg(loaderType, loaderVersion, error));
            showError(this, error);
        }
        return;
    }
    
    showInfo(this, QString("加载器 %1 %2 安装成功！").arg(loaderType, loaderVersion));
    refreshInstalledVersionsUi();
}

void MainWindow::showResourceDetails()
{
    const int row = m_resourceResultsTable->currentRow();
    if (row < 0) {
        showError(this, "请先选中要查看的资源。");
        return;
    }

    const QString source = m_resourceResultsTable->item(row, 2)->text();
    if (source != "Modrinth") {
        showInfo(this, "目前仅支持查看 Modrinth 项目详情。");
        return;
    }

    const QJsonObject payload = QJsonDocument::fromJson(
        m_resourceResultsTable->item(row, 0)->data(Qt::UserRole).toByteArray()).object();
    const QString projectId = payload.value("project_id").toString(payload.value("id").toString());

    QString error;
    const QJsonObject projectDetails = m_core.getModrinthProjectDetails(projectId, &error);
    if (!error.isEmpty()) {
        appendLog(QString("读取 Modrinth 项目详情失败: %1").arg(error));
        showError(this, error);
        return;
    }

    const QJsonArray versions = m_core.getModrinthProjectVersions(projectId, &error);
    if (!error.isEmpty()) {
        appendLog(QString("读取 Modrinth 版本列表失败: %1").arg(error));
        showError(this, error);
        return;
    }

    auto dialog = new QDialog(this);
    dialog->setWindowTitle(QString("项目详情 - %1").arg(projectDetails.value("title").toString()));
    dialog->setMinimumSize(800, 600);

    auto layout = new QVBoxLayout(dialog);

    auto titleLabel = new QLabel(QString("<h1>%1</h1>").arg(projectDetails.value("title").toString()), dialog);
    layout->addWidget(titleLabel);

    auto descriptionText = new QTextEdit(dialog);
    descriptionText->setHtml(projectDetails.value("description").toString());
    descriptionText->setReadOnly(true);
    layout->addWidget(descriptionText);

    auto infoLayout = new QHBoxLayout();
    infoLayout->addWidget(new QLabel(QString("作者：%1").arg(projectDetails.value("author").toString())));
    infoLayout->addWidget(new QLabel(QString("下载量：%1").arg(QString::number(projectDetails.value("downloads").toInt()))));
    infoLayout->addWidget(new QLabel(QString("关注：%1").arg(QString::number(projectDetails.value("follows").toInt()))));
    infoLayout->addStretch();
    layout->addLayout(infoLayout);

    auto versionsLabel = new QLabel("<h3>可用版本</h3>", dialog);
    layout->addWidget(versionsLabel);

    auto versionsTable = new QTableWidget(dialog);
    versionsTable->setColumnCount(4);
    versionsTable->setHorizontalHeaderLabels({"版本号", "游戏版本", "加载器", "发布日期"});
    versionsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    versionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    versionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    versionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    versionsTable->setAlternatingRowColors(true);

    for (const QJsonValue &versionValue : versions) {
        const QJsonObject versionObj = versionValue.toObject();
        const int vRow = versionsTable->rowCount();
        versionsTable->insertRow(vRow);

        auto versionItem = new QTableWidgetItem(versionObj.value("name").toString());
        versionItem->setData(Qt::UserRole, QJsonDocument(versionObj).toJson(QJsonDocument::Compact));
        versionsTable->setItem(vRow, 0, versionItem);

        const QJsonArray gameVersions = versionObj.value("game_versions").toArray();
        QStringList versionNames;
        for (const QJsonValue &v : gameVersions) {
            versionNames << v.toString();
        }
        versionsTable->setItem(vRow, 1, new QTableWidgetItem(versionNames.join(", ")));

        const QJsonArray loaders = versionObj.value("loaders").toArray();
        QStringList loaderNames;
        for (const QJsonValue &l : loaders) {
            loaderNames << l.toString();
        }
        versionsTable->setItem(vRow, 2, new QTableWidgetItem(loaderNames.join(", ")));

        versionsTable->setItem(vRow, 3, new QTableWidgetItem(versionObj.value("date_published").toString()));
    }

    layout->addWidget(versionsTable);

    auto buttonLayout = new QHBoxLayout();
    auto cancelButton = new QPushButton("取消", dialog);
    auto downloadButton = new QPushButton("下载选中版本", dialog);

    buttonLayout->addWidget(cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(downloadButton);
    layout->addLayout(buttonLayout);

    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::close);
    connect(downloadButton, &QPushButton::clicked, this, [this, versionsTable, dialog] {
        const int selectedRow = versionsTable->currentRow();
        if (selectedRow < 0) {
            showError(dialog, "请先选择要下载的版本！");
            return;
        }

        const QJsonObject versionObj = QJsonDocument::fromJson(
            versionsTable->item(selectedRow, 0)->data(Qt::UserRole).toByteArray()).object();
        const QJsonArray files = versionObj.value("files").toArray();

        if (files.isEmpty()) {
            showError(dialog, "该版本没有可下载的文件！");
            return;
        }

        QJsonObject selectedFile = files[0].toObject();
        for (const QJsonValue &file : files) {
            if (file.toObject().value("primary").toBool()) {
                selectedFile = file.toObject();
                break;
            }
        }

        if (!saveUiToConfig()) {
            return;
        }

        QString error;
        if (!m_core.downloadModrinthVersionFileWithDialog(versionObj, selectedFile, m_resourceTargetTypeCombo->currentText(), &error, dialog)) {
            if (!error.contains("操作已取消") && !error.contains("下载已取消")) {
                appendLog(QString("下载 Modrinth 版本文件失败: %1").arg(error));
                showError(dialog, error);
            }
            return;
        }

        showInfo(dialog, "下载成功！");
        dialog->close();
    });

    dialog->exec();
}

void MainWindow::downloadJava()
{
    QDialog dialog(this);
    dialog.setWindowTitle("下载 Java");
    dialog.setMinimumSize(450, 200);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *infoLabel = new QLabel("请选择Java安装目录：", &dialog);
    layout->addWidget(infoLabel);

    QHBoxLayout *pathLayout = new QHBoxLayout();
    QLineEdit *pathEdit = new QLineEdit(&dialog);
    pathEdit->setText(QDir::homePath() + "/.kill/java");
    pathLayout->addWidget(pathEdit);

    QPushButton *browseButton = new QPushButton("浏览", &dialog);
    pathLayout->addWidget(browseButton);
    layout->addLayout(pathLayout);

    QLabel *versionLabel = new QLabel("选择Java版本：", &dialog);
    layout->addWidget(versionLabel);

    QComboBox *versionCombo = new QComboBox(&dialog);
    versionCombo->addItem("Java 8 (推荐)", 8);
    versionCombo->addItem("Java 11", 11);
    versionCombo->addItem("Java 17", 17);
    versionCombo->addItem("Java 21", 21);
    layout->addWidget(versionCombo);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *cancelButton = new QPushButton("取消", &dialog);
    QPushButton *downloadButton = new QPushButton("下载", &dialog);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(downloadButton);
    layout->addLayout(buttonLayout);

    connect(browseButton, &QPushButton::clicked, [&dialog, pathEdit] {
        const QString dir = QFileDialog::getExistingDirectory(&dialog, "选择Java安装目录", pathEdit->text());
        if (!dir.isEmpty()) {
            pathEdit->setText(dir);
        }
    });

    bool cancelled = false;
    connect(cancelButton, &QPushButton::clicked, [&dialog, &cancelled] {
        cancelled = true;
        dialog.reject();
    });

    connect(downloadButton, &QPushButton::clicked, [&dialog] {
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted || cancelled) {
        return;
    }

    const QString installDir = pathEdit->text();
    const int javaVersion = versionCombo->currentData().toInt();

    appendLog(QString("开始下载 Java %1 到 %2").arg(javaVersion).arg(installDir));

    QString downloadUrl;
    QString archiveName;

    if (javaVersion == 8) {
        downloadUrl = "https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u412-b08/OpenJDK8U-jre_x64_windows_hotspot_8u412b08.zip";
        archiveName = "OpenJDK8U-jre_x64_windows_hotspot_8u412b08.zip";
    } else if (javaVersion == 11) {
        downloadUrl = "https://github.com/adoptium/temurin11-binaries/releases/download/jdk-11.0.23%2B9/OpenJDK11U-jre_x64_windows_hotspot_11.0.23_9.zip";
        archiveName = "OpenJDK11U-jre_x64_windows_hotspot_11.0.23_9.zip";
    } else if (javaVersion == 17) {
        downloadUrl = "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.11%2B9/OpenJDK17U-jre_x64_windows_hotspot_17.0.11_9.zip";
        archiveName = "OpenJDK17U-jre_x64_windows_hotspot_17.0.11_9.zip";
    } else {
        downloadUrl = "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.3%2B9/OpenJDK21U-jre_x64_windows_hotspot_21.0.3_9.zip";
        archiveName = "OpenJDK21U-jre_x64_windows_hotspot_21.0.3_9.zip";
    }

    QString error;
    const QString archivePath = installDir + "/" + archiveName;

    DownloadProgressDialog progressDialog(QString("下载 Java %1").arg(javaVersion),
                                         QString("正在准备下载 Java %1...").arg(javaVersion),
                                         this);
    progressDialog.showOverallProgress(true);
    progressDialog.setOverallBusy(true);
    progressDialog.setStepLabel("准备中...");
    progressDialog.setBusy(true);
    progressDialog.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool downloadCancelled = false;
    bool currentDownloadDone = false;
    bool currentDownloadOk = false;
    QString currentDownloadError;

    connect(&progressDialog, &DownloadProgressDialog::cancelled, [&downloadCancelled]() {
        downloadCancelled = true;
    });

    auto pumpEvents = [&]() -> bool {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (downloadCancelled || progressDialog.isCancelled()) {
            return false;
        }
        return true;
    };

    if (!pumpEvents()) return;

    progressDialog.setStepLabel(QString("步骤 1/2: 下载 Java %1").arg(javaVersion));
    progressDialog.setOverallBusy(false);
    progressDialog.setOverallProgress(0, 2);
    progressDialog.setStatus("正在下载 Java 运行时...");
    if (!pumpEvents()) return;

    {
        DownloadManager *manager = new DownloadManager(m_core.networkManager(), this);
        connect(&progressDialog, &DownloadProgressDialog::cancelled, manager, &DownloadManager::cancel);
        connect(manager, &DownloadManager::downloadProgress,
                         [&progressDialog](qint64 received, qint64 total) {
            if (total > 0) {
                progressDialog.setProgress(static_cast<int>(received), static_cast<int>(total));
                progressDialog.setStatus(QString("已下载: %1 / %2 MB")
                    .arg(received / (1024 * 1024))
                    .arg(total / (1024 * 1024)));
            }
        });
        connect(manager, &DownloadManager::downloadFinished,
                         [&](bool success, const QString &err) {
            currentDownloadDone = true;
            currentDownloadOk = success;
            currentDownloadError = err;
        });

        manager->downloadFileSilent(QUrl(downloadUrl), archivePath, QString());

        while (!currentDownloadDone) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (downloadCancelled || progressDialog.isCancelled()) {
                manager->deleteLater();
                return;
            }
        }

        manager->deleteLater();

        if (!currentDownloadOk) {
            if (!currentDownloadError.contains("下载已取消")) {
                appendLog(QString("Java 下载失败: %1").arg(currentDownloadError));
                showError(this, currentDownloadError);
            }
            return;
        }
    }

    progressDialog.setOverallProgress(1, 2);
    progressDialog.setStepLabel("步骤 2/2: 解压 Java 运行时");
    progressDialog.setStatus("正在解压...");
    progressDialog.setBusy(true);
    if (!pumpEvents()) return;

    appendLog("Java下载完成，正在解压...");

    QDir().mkpath(installDir);

    QProcess extractProcess;
    extractProcess.setWorkingDirectory(installDir);
    QStringList args;
    args << "-Command" << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(archivePath).arg(installDir);
    
    extractProcess.start("powershell", args);
    if (!extractProcess.waitForFinished(120000)) {
        appendLog("Java 解压失败: 解压超时");
        showError(this, "解压超时！");
        return;
    }

    if (extractProcess.exitCode() != 0) {
        appendLog(QString("Java 解压失败: %1").arg(QString::fromLocal8Bit(extractProcess.readAllStandardError()).trimmed()));
        showError(this, "解压失败！");
        return;
    }

    QString javaPath;
    QDirIterator it(installDir, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        if (filePath.endsWith("/bin/javaw.exe")) {
            javaPath = filePath;
            break;
        }
    }

    if (javaPath.isEmpty()) {
        appendLog("Java 安装失败: 解压后未找到 javaw.exe");
        showError(this, "无法找到Java可执行文件！");
        return;
    }

    m_javaPathEdit->setText(javaPath);

    progressDialog.setOverallProgress(2, 2);
    progressDialog.setProgress(100, 100);
    progressDialog.setStatus("Java 安装完成！");
    progressDialog.setStepLabel("安装完成");
    progressDialog.setBusy(false);
    QTimer::singleShot(500, &progressDialog, &QDialog::accept);
    progressDialog.exec();

    showInfo(this, QString("Java %1 下载成功！\n路径: %2").arg(javaVersion).arg(javaPath));
    appendLog("Java下载和安装完成！");
}

void MainWindow::runMicrosoftDeviceLoginFlow()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Microsoft 设备码登录");
    dialog.setMinimumSize(520, 380);

    auto *layout = new QVBoxLayout(&dialog);

    auto *statusLabel = new QLabel("正在获取设备码，请稍候...", &dialog);
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto *codeBox = new QWidget(&dialog);
    auto *codeBoxLayout = new QVBoxLayout(codeBox);
    codeBoxLayout->setContentsMargins(0, 0, 0, 0);

    auto *codeTitleLabel = new QLabel("您的验证码：", codeBox);
    auto *codeValueLabel = new QLabel(codeBox);
    {
        QFont f = codeValueLabel->font();
        f.setPointSize(f.pointSize() + 10);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 4);
        codeValueLabel->setFont(f);
    }
    codeValueLabel->setAlignment(Qt::AlignCenter);
    codeValueLabel->setStyleSheet("background: #f5f5f5; padding: 16px; border-radius: 8px; color: #0078d4;");
    codeValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *copyButton = new QPushButton("复制验证码", codeBox);
    auto *openButton = new QPushButton("打开验证页面", codeBox);

    auto *btnRow1 = new QHBoxLayout();
    btnRow1->addStretch();
    btnRow1->addWidget(copyButton);
    btnRow1->addWidget(openButton);
    btnRow1->addStretch();

    auto *hintLabel = new QLabel(
        "步骤：\n"
        "1. 点击「打开验证页面」或手动在浏览器中打开下方地址\n"
        "2. 在网页中输入上面显示的验证码\n"
        "3. 完成 Microsoft 账号登录和授权\n"
        "4. 回到本窗口等待登录完成（会自动轮询检测状态）",
        codeBox);
    hintLabel->setWordWrap(true);

    auto *linkLabel = new QLabel(codeBox);
    linkLabel->setOpenExternalLinks(false);
    linkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    linkLabel->setStyleSheet("color: #0078d4;");

    codeBoxLayout->addWidget(codeTitleLabel);
    codeBoxLayout->addWidget(codeValueLabel);
    codeBoxLayout->addLayout(btnRow1);
    codeBoxLayout->addSpacing(8);
    codeBoxLayout->addWidget(hintLabel);
    codeBoxLayout->addSpacing(4);
    codeBoxLayout->addWidget(linkLabel);
    codeBox->hide();

    layout->addWidget(codeBox);

    auto *progressLabel = new QLabel(&dialog);
    progressLabel->setWordWrap(true);
    layout->addWidget(progressLabel);

    layout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    LauncherCore::MicrosoftDeviceCode deviceCode;
    auto *pollTimer = new QTimer(&dialog);
    pollTimer->setSingleShot(false);
    bool userCancelled = false;
    bool flowFinished = false;

    auto stopPolling = [&]() {
        if (pollTimer->isActive()) {
            pollTimer->stop();
        }
    };

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, [&] {
        userCancelled = true;
        stopPolling();
        dialog.reject();
    });

    connect(copyButton, &QPushButton::clicked, &dialog, [&] {
        QGuiApplication::clipboard()->setText(deviceCode.userCode);
        showInfo(this, "验证码已复制到剪贴板。");
    });

    connect(openButton, &QPushButton::clicked, &dialog, [&] {
        QDesktopServices::openUrl(QUrl(deviceCode.verificationUri));
    });

    connect(linkLabel, &QLabel::linkActivated, &dialog, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    QString startError;
    if (!m_core.startMicrosoftDeviceLogin(&deviceCode, &startError)) {
        statusLabel->setText("获取设备码失败：");
        progressLabel->setText(startError);
        progressLabel->setStyleSheet("color: #d13438;");
        return;
    }

    codeValueLabel->setText(deviceCode.userCode);
    const QString displayLink = deviceCode.verificationUri;
    linkLabel->setText(QString("验证地址：<a href=\"%1\" style=\"color:#0078d4;text-decoration:underline;\">%2</a>")
                       .arg(displayLink, displayLink));

    statusLabel->setText("请在浏览器中完成验证：");
    codeBox->show();

    int intervalMs = qMax(1, deviceCode.intervalSec) * 1000;
    pollTimer->setInterval(intervalMs);

    connect(pollTimer, &QTimer::timeout, &dialog, [&] {
        if (flowFinished || userCancelled) {
            stopPolling();
            return;
        }

        QString pollError, fatalError;
        const auto outcome = m_core.pollMicrosoftDeviceLogin(deviceCode, &pollError, &fatalError);

        switch (outcome) {
        case LauncherCore::DevicePollOutcome::Pending:
            progressLabel->setText(QString("等待用户授权中...（每 %1 秒检测一次）")
                                   .arg(deviceCode.intervalSec));
            progressLabel->setStyleSheet("");
            break;

        case LauncherCore::DevicePollOutcome::SlowDown:
            intervalMs = qMax(5000, intervalMs + 5000);
            pollTimer->setInterval(intervalMs);
            progressLabel->setText(QString("服务器要求降速，已调整轮询间隔为 %1 秒...")
                                   .arg(intervalMs / 1000));
            progressLabel->setStyleSheet("");
            break;

        case LauncherCore::DevicePollOutcome::Expired:
            stopPolling();
            flowFinished = true;
            statusLabel->setText("设备码已过期：");
            progressLabel->setText("本次设备码请求已超时，请重新点击「设备码登录」发起新的流程。");
            progressLabel->setStyleSheet("color: #d13438;");
            buttonBox->clear();
            buttonBox->setStandardButtons(QDialogButtonBox::Close);
            break;

        case LauncherCore::DevicePollOutcome::Authorized:
            stopPolling();
            flowFinished = true;
            statusLabel->setText("登录成功！");
            progressLabel->setText("已成功获取 Microsoft 正版账户凭证。");
            progressLabel->setStyleSheet("color: #107c10;");
            appendLog("Microsoft 正版账户通过 Device Code 登录成功。");
            refreshAccountsUi();
            QTimer::singleShot(800, &dialog, &QDialog::accept);
            break;

        case LauncherCore::DevicePollOutcome::Failed:
            stopPolling();
            flowFinished = true;
            statusLabel->setText("登录失败：");
            QString shown = !fatalError.isEmpty() ? fatalError : (!pollError.isEmpty() ? pollError : "未知错误");
            progressLabel->setText(shown);
            progressLabel->setStyleSheet("color: #d13438;");
            appendLog(QString("Microsoft Device Code 登录失败: %1").arg(shown));
            buttonBox->clear();
            buttonBox->setStandardButtons(QDialogButtonBox::Close);
            break;
        }
    });

    pollTimer->start();
    progressLabel->setText(QString("等待用户授权中...（每 %1 秒检测一次）").arg(deviceCode.intervalSec));

    dialog.exec();
    stopPolling();
}
