#include "download_progress_dialog.h"

DownloadProgressDialog::DownloadProgressDialog(const QString &title, const QString &description, QWidget *parent)
    : QDialog(parent), m_cancelled(false)
{
    setWindowTitle(title);
    setMinimumSize(450, 200);
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_titleLabel = new QLabel(description, this);
    layout->addWidget(m_titleLabel);

    m_stepLabel = new QLabel(this);
    m_stepLabel->setVisible(false);
    layout->addWidget(m_stepLabel);

    m_overallProgressBar = new QProgressBar(this);
    m_overallProgressBar->setMinimum(0);
    m_overallProgressBar->setMaximum(100);
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setVisible(false);
    layout->addWidget(m_overallProgressBar);

    m_statusLabel = new QLabel("准备中...", this);
    layout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    m_cancelButton = new QPushButton("取消", this);
    layout->addWidget(m_cancelButton);

    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadProgressDialog::onCancelClicked);
}

void DownloadProgressDialog::setProgress(int value, int max)
{
    m_progressBar->setMaximum(max);
    m_progressBar->setValue(value);
}

void DownloadProgressDialog::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void DownloadProgressDialog::setBusy(bool busy)
{
    if (busy) {
        m_progressBar->setRange(0, 0);
        return;
    }
    if (m_progressBar->maximum() == 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
    }
}

void DownloadProgressDialog::setOverallProgress(int value, int max)
{
    m_overallProgressBar->setMaximum(max);
    m_overallProgressBar->setValue(value);
}

void DownloadProgressDialog::setOverallBusy(bool busy)
{
    if (busy) {
        m_overallProgressBar->setRange(0, 0);
        return;
    }
    if (m_overallProgressBar->maximum() == 0) {
        m_overallProgressBar->setRange(0, 100);
        m_overallProgressBar->setValue(0);
    }
}

void DownloadProgressDialog::setStepLabel(const QString &text)
{
    m_stepLabel->setText(text);
}

void DownloadProgressDialog::showOverallProgress(bool show)
{
    m_stepLabel->setVisible(show);
    m_overallProgressBar->setVisible(show);
}

void DownloadProgressDialog::onCancelClicked()
{
    m_cancelled = true;
    emit cancelled();
    reject();
}
