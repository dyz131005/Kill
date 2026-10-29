#include "download_progress_dialog.h"

#include <QColor>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>

DownloadProgressDialog::DownloadProgressDialog(const QString &title, const QString &description, QWidget *parent)
    : QDialog(parent), m_cancelled(false), m_embedded(false)
{
    setWindowTitle(title);
    setMinimumSize(450, 200);
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    buildUi(title, description);

    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(this->layout());
    Q_UNUSED(layout);
}

DownloadProgressDialog::DownloadProgressDialog(QWidget *hostWidget, const QString &title, const QString &description)
    : QDialog(hostWidget), m_cancelled(false), m_embedded(true)
{
    m_host = hostWidget;

    // Frameless, transparent overlay child of the host.
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);

    buildUi(title, description);

    // Restyle for embedded look: white rounded panel instead of plain dialog.
    if (auto *vl = qobject_cast<QVBoxLayout *>(this->layout())) {
        vl->setContentsMargins(0, 0, 0, 0);
    }

    m_panel = new QFrame(this);
    m_panel->setObjectName("downloadEmbeddedPanel");
    m_panel->setStyleSheet(
        "#downloadEmbeddedPanel {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 16px;"
        "}"
    );

    // Move existing child widgets into the panel with a fresh layout.
    auto *panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(24, 20, 24, 18);
    panelLayout->setSpacing(12);

    // Reparent the widgets built by buildUi into the panel layout.
    const QList<QWidget *> kids = { m_titleLabel, m_stepLabel, m_overallProgressBar,
                                    m_statusLabel, m_progressBar };
    for (QWidget *w : kids) {
        if (!w) continue;
        w->setParent(m_panel);
        panelLayout->addWidget(w);
    }

    m_titleLabel->setStyleSheet("font-size: 17px; font-weight: 700; color: #111827;");
    m_statusLabel->setStyleSheet("font-size: 13px; color: #374151;");
    m_stepLabel->setStyleSheet("font-size: 12px; color: #6b7280;");

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    // Reparent cancel button into its own row for HMCL look.
    m_cancelButton->setParent(m_panel);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setMinimumWidth(96);
    m_cancelButton->setFixedHeight(36);
    m_cancelButton->setStyleSheet(
        "QPushButton { background-color: #f3f4f6; border: 1px solid #d1d5db; border-radius: 8px;"
        "  color: #374151; font-size: 14px; font-weight: 600; }"
        "QPushButton:hover { background-color: #e5e7eb; }"
        "QPushButton:pressed { background-color: #d1d5db; }"
    );
    btnRow->addWidget(m_cancelButton);
    panelLayout->addLayout(btnRow);

    // Make cancel the only interactive element; the overlay swallows other input.
    setFocusPolicy(Qt::StrongFocus);

    if (m_host) {
        m_host->installEventFilter(this);
    }
    hide();
}

void DownloadProgressDialog::buildUi(const QString &title, const QString &description)
{
    Q_UNUSED(title);
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

void DownloadProgressDialog::relayoutEmbedded()
{
    if (!m_embedded || !m_host || !m_panel) return;
    setGeometry(m_host->rect());

    const int w = qMin(560, qMax(320, m_host->width() - 80));
    int h = m_panel->sizeHint().height();
    const int maxH = qMax(220, m_host->height() - 80);
    if (h > maxH) h = maxH;
    if (h < 200) h = 200;

    const int x = (m_host->width() - w) / 2;
    const int y = (m_host->height() - h) / 2;
    m_panel->setGeometry(x, y, w, h);
    raise();
    if (m_panel) m_panel->raise();
}

void DownloadProgressDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_embedded) {
        if (m_host) {
            setParent(m_host);
        }
        relayoutEmbedded();
    }
}

void DownloadProgressDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (m_embedded) relayoutEmbedded();
}

void DownloadProgressDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!m_embedded) {
        QDialog::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.fillRect(rect(), QColor(15, 23, 42, 120));
}

bool DownloadProgressDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (m_embedded && watched == m_host &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        relayoutEmbedded();
    }
    return QDialog::eventFilter(watched, event);
}

void DownloadProgressDialog::onCancelClicked()
{
    m_cancelled = true;
    emit cancelled();
    if (m_embedded) {
        hide();
    } else {
        reject();
    }
}
