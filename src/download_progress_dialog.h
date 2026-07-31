#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QNetworkReply>

class DownloadProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DownloadProgressDialog(const QString &title, const QString &description, QWidget *parent = nullptr);

    void setProgress(int value, int max);
    void setStatus(const QString &text);
    void setBusy(bool busy);
    bool isCancelled() const { return m_cancelled; }

    void setOverallProgress(int value, int max);
    void setOverallBusy(bool busy);
    void setStepLabel(const QString &text);
    void showOverallProgress(bool show);

signals:
    void cancelled();

private slots:
    void onCancelClicked();

private:
    QLabel *m_titleLabel;
    QLabel *m_stepLabel;
    QLabel *m_statusLabel;
    QProgressBar *m_overallProgressBar;
    QProgressBar *m_progressBar;
    QPushButton *m_cancelButton;
    bool m_cancelled;
};
