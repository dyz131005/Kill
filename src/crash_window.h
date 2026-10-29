#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QPointer>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QMainWindow;
class QCloseEvent;

// HMCL-style game crash window.
// Shown when the game exits abnormally. Hides the main window while open,
// restores it when closed via the "返回启动器" button (or window close).
class CrashWindow : public QDialog
{
    Q_OBJECT

public:
    // `gameLog` is the raw captured game stdout/stderr used for analysis.
    // `parentMainWindow` (optional) is hidden while this window is shown and
    // restored when this window is destroyed.
    explicit CrashWindow(const QString &gameLog,
                         QWidget *parentMainWindow = nullptr,
                         QWidget *parent = nullptr);
    ~CrashWindow() override;

private slots:
    void onReturnToLauncher();
    void onExportReport();
    void onCopyLog();
    void onOpenLogFolder();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi(const QString &gameLog);
    // Analyze the log with regex rules; returns a human-readable reason.
    static QString analyzeReason(const QString &log, QStringList *matchedTags);
    static QString extractExcerpt(const QString &log, int maxLines);
    void restoreMainWindow();

    QPointer<QWidget> m_mainWindow;
    QString m_gameLog;
    QLabel *m_reasonLabel = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    bool m_mainWasHidden = false;
};
