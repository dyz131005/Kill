#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QNetworkReply>
#include <QPointer>

class QWidget;
class QShowEvent;
class QResizeEvent;
class QPaintEvent;

// Progress dialog usable in two modes:
//  - standalone top-level modal window (default)
//  - embedded overlay inside a host widget (HMCL-style: semi-transparent mask
//    + centered panel, only Cancel is interactive), created via the
//    DownloadProgressDialog(hostWidget, ...) constructor.
class DownloadProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DownloadProgressDialog(const QString &title, const QString &description, QWidget *parent = nullptr);

    // Embedded (in-window overlay) mode. `hostWidget` is the widget to cover
    // (usually the main window). The `title`/`description` are reused.
    DownloadProgressDialog(QWidget *hostWidget, const QString &title, const QString &description);

    void setProgress(int value, int max);
    void setStatus(const QString &text);
    void setBusy(bool busy);
    bool isCancelled() const { return m_cancelled; }

    void setOverallProgress(int value, int max);
    void setOverallBusy(bool busy);
    void setStepLabel(const QString &text);
    void showOverallProgress(bool show);

    bool isEmbedded() const { return m_embedded; }

    // In embedded mode, (re)attach and cover the host.
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

signals:
    void cancelled();

private slots:
    void onCancelClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi(const QString &title, const QString &description);
    void relayoutEmbedded();

    QLabel *m_titleLabel = nullptr;
    QLabel *m_stepLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_overallProgressBar = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelButton = nullptr;
    bool m_cancelled = false;

    bool m_embedded = false;
    QPointer<QWidget> m_host;
    QWidget *m_panel = nullptr;
};
