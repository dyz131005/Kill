#include "crash_window.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

CrashWindow::CrashWindow(const QString &gameLog, QWidget *parentMainWindow, QWidget *parent)
    : QDialog(parent), m_gameLog(gameLog)
{
    setWindowTitle(QStringLiteral("游戏崩溃了 :("));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(880, 640);
    setStyleSheet("QDialog { background-color: #f7f8fa; }");

    m_mainWindow = parentMainWindow;
    if (m_mainWindow) {
        m_mainWasHidden = !m_mainWindow->isVisible();
        m_mainWindow->hide();
    }

    buildUi(gameLog);
}

CrashWindow::~CrashWindow() = default;

void CrashWindow::buildUi(const QString &gameLog)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("游戏崩溃了 :("), this);
    title->setStyleSheet("font-size: 24px; font-weight: 800; color: #dc2626;");
    root->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("游戏进程异常退出，下面是可能的原因和日志。你可以导出报告反馈给开发者。"), this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("font-size: 13px; color: #4b5563;");
    root->addWidget(subtitle);

    auto *reasonCard = new QFrame(this);
    reasonCard->setObjectName(QStringLiteral("crashReasonCard"));
    reasonCard->setStyleSheet(QStringLiteral(
        "#crashReasonCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }"));
    auto *reasonLayout = new QVBoxLayout(reasonCard);
    reasonLayout->setContentsMargins(16, 14, 16, 14);
    auto *reasonTitle = new QLabel(QStringLiteral("可能的原因"), reasonCard);
    reasonTitle->setStyleSheet("font-size: 14px; font-weight: 700; color: #111827;");
    reasonLayout->addWidget(reasonTitle);

    QStringList tags;
    const QString reason = analyzeReason(gameLog, &tags);
    m_reasonLabel = new QLabel(reason, reasonCard);
    m_reasonLabel->setWordWrap(true);
    m_reasonLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reasonLabel->setStyleSheet("font-size: 13px; color: #374151;");
    reasonLayout->addWidget(m_reasonLabel);

    root->addWidget(reasonCard);

    auto *logTitle = new QLabel(QStringLiteral("日志片段"), this);
    logTitle->setStyleSheet("font-size: 14px; font-weight: 700; color: #111827;");
    root->addWidget(logTitle);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setPlainText(extractExcerpt(gameLog, 200));
    m_logView->setStyleSheet(
        "QPlainTextEdit { background: #0f172a; color: #e5e7eb; border: 1px solid #1e293b;"
        "  border-radius: 10px; font-family: Consolas, monospace; font-size: 12px; padding: 10px; }"
    );
    root->addWidget(m_logView, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    auto *returnBtn = new QPushButton(QStringLiteral("返回启动器"), this);
    returnBtn->setCursor(Qt::PointingHandCursor);
    returnBtn->setStyleSheet(
        "QPushButton { background: #2563eb; color: #fff; border: none; border-radius: 10px;"
        "  padding: 10px 22px; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }"
    );
    connect(returnBtn, &QPushButton::clicked, this, &CrashWindow::onReturnToLauncher);

    auto makeBtn = [this](const QString &text) {
        auto *b = new QPushButton(text, this);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            "QPushButton { background: #ffffff; color: #374151; border: 1px solid #d1d5db;"
            "  border-radius: 10px; padding: 10px 18px; font-size: 13px; font-weight: 600; }"
            "QPushButton:hover { background: #f3f4f6; }"
        );
        return b;
    };

    auto *exportBtn = makeBtn(QStringLiteral("导出崩溃报告"));
    connect(exportBtn, &QPushButton::clicked, this, &CrashWindow::onExportReport);
    auto *copyBtn = makeBtn(QStringLiteral("复制日志"));
    connect(copyBtn, &QPushButton::clicked, this, &CrashWindow::onCopyLog);
    auto *folderBtn = makeBtn(QStringLiteral("打开日志文件夹"));
    connect(folderBtn, &QPushButton::clicked, this, &CrashWindow::onOpenLogFolder);

    btnRow->addWidget(returnBtn);
    btnRow->addStretch();
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(folderBtn);
    btnRow->addWidget(exportBtn);
    root->addLayout(btnRow);
}

QString CrashWindow::analyzeReason(const QString &log, QStringList *matchedTags)
{
    struct Rule { const char *pattern; const char *explain; };
    static const Rule rules[] = {
        { "java\\.lang\\.UnsupportedClassVersionError", "Java 版本过低：游戏或模组需要更高版本的 Java，请在设置里更换对应版本的 Java。" },
        { "java\\.lang\\.OutOfMemoryError|Out of Memory Error|insufficient memory|Too small maximum heap", "内存不足：请在设置里调大最大内存，或关闭占用内存的程序。" },
        { "Could not reserve enough space for .*KB object heap|Invalid maximum heap size|The specified size exceeds", "分配的内存超出限制：请降低最大内存或改用 64 位 Java。" },
        { "Pixel format not accelerated|Couldn't set pixel format|The driver does not appear to support OpenGL|GLXBadFBConfig|org\\.lwjgl\\.LWJGLException", "显卡/OpenGL 驱动问题：请更新显卡驱动，或在设置中勾选「强制软件 OpenGL」。" },
        { "1282: Invalid operation|lower resolution resourcepack", "资源包/着色器导致的图形错误：尝试降低分辨率资源包或移除光影。" },
        { "MixinApplyError|Mixin apply failed|mixin\\.injection\\.throwables", "模组冲突或 Mixin 应用失败：可能是模组之间不兼容，请检查最近安装的模组。" },
    };

    for (const Rule &r : rules) {
        QRegularExpression re(QString::fromUtf8(r.pattern), QRegularExpression::CaseInsensitiveOption);
        if (re.match(log).hasMatch()) {
            if (matchedTags) matchedTags->append(QString::fromUtf8(r.explain));
            return QString::fromUtf8(r.explain);
        }
    }

    QRegularExpression descRe(QStringLiteral("Description:\\s*(.+)"));
    auto dm = descRe.match(log);
    if (dm.hasMatch())
        return QStringLiteral("游戏崩溃。崩溃报告描述：") + dm.captured(1).trimmed();

    return QStringLiteral("未能自动识别具体原因。请导出崩溃报告反馈给开发者。常见原因：Java 版本不匹配、内存不足、模组冲突、显卡驱动问题。");
}

QString CrashWindow::extractExcerpt(const QString &log, int maxLines)
{
    if (log.trimmed().isEmpty())
        return QStringLiteral("（没有捕获到游戏日志输出）");

    const QStringList lines = log.split('\n', Qt::SkipEmptyParts);
    if (lines.size() <= maxLines)
        return lines.join('\n');

    QStringList out;
    out << lines.mid(0, qMin(5, lines.size()));
    out << QStringLiteral("……（省略 %1 行）……").arg(lines.size() - maxLines);
    out << lines.mid(lines.size() - (maxLines - 5));
    return out.join('\n');
}

void CrashWindow::restoreMainWindow()
{
    if (m_mainWindow) {
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
    }
}

void CrashWindow::onReturnToLauncher()
{
    close();
}

void CrashWindow::onCopyLog()
{
    QApplication::clipboard()->setText(m_gameLog);
    QMessageBox::information(this, QStringLiteral("复制日志"), QStringLiteral("已复制完整游戏日志到剪贴板。"));
}

void CrashWindow::closeEvent(QCloseEvent *event)
{
    restoreMainWindow();
    QDialog::closeEvent(event);
}

void CrashWindow::onExportReport()
{
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString defaultName = defaultDir + "/kill-crash-" +
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt";
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出崩溃报告"),
        defaultName, QStringLiteral("文本文件 (*.txt)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入文件：") + path);
        return;
    }
    QTextStream ts(&f);
    ts << "===== Kill 启动器 崩溃报告 =====\n";
    ts << "时间: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    ts << "分析结论: " << (m_reasonLabel ? m_reasonLabel->text() : QString()) << "\n\n";
    ts << "===== 游戏日志 =====\n";
    ts << m_gameLog;
    f.close();

    QMessageBox::information(this, QStringLiteral("导出成功"),
        QStringLiteral("崩溃报告已保存到：\n") + QDir::toNativeSeparators(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void CrashWindow::onOpenLogFolder()
{
    const QString logsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDesktopServices::openUrl(QUrl::fromLocalFile(logsDir));
}
