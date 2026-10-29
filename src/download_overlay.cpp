#include "download_overlay.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QResizeEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kPanelWidth = 620;
constexpr int kPanelMaxHeight = 520;
constexpr int kPanelMargin = 40;
}

DownloadOverlay::DownloadOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("downloadOverlay");
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);

    // Panel container
    m_panel = new QFrame(this);
    m_panel->setObjectName("downloadOverlayPanel");
    m_panel->setStyleSheet(
        "#downloadOverlayPanel {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e5e7eb;"
        "  border-radius: 16px;"
        "}"
    );

    auto *panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(24, 20, 24, 18);
    panelLayout->setSpacing(12);

    // Title row
    m_titleLabel = new QLabel(QStringLiteral("下载"), m_panel);
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #111827;");
    panelLayout->addWidget(m_titleLabel);

    m_phaseLabel = new QLabel(m_panel);
    m_phaseLabel->setStyleSheet("font-size: 13px; color: #6b7280;");
    m_phaseLabel->setWordWrap(true);
    panelLayout->addWidget(m_phaseLabel);

    // Overall progress row (thin bar + count + percent, HMCL-style)
    auto *overallRow = new QHBoxLayout();
    overallRow->setSpacing(10);
    m_overallBar = new QProgressBar(m_panel);
    m_overallBar->setRange(0, 100);
    m_overallBar->setValue(0);
    m_overallBar->setTextVisible(false);
    m_overallBar->setFixedHeight(10);
    m_overallBar->setStyleSheet(
        "QProgressBar { background-color: #f3f4f6; border: none; border-radius: 5px; }"
        "QProgressBar::chunk { background-color: #3b82f6; border-radius: 5px; }"
    );
    m_countLabel = new QLabel(m_panel);
    m_countLabel->setStyleSheet("font-size: 12px; color: #6b7280; font-weight: 600;");
    m_countLabel->setMinimumWidth(72);
    m_countLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    overallRow->addWidget(m_countLabel, 0);

    overallRow->addWidget(m_overallBar, 1);

    m_overallText = new QLabel(QStringLiteral("0%"), m_panel);
    m_overallText->setStyleSheet("font-size: 12px; color: #374151; font-weight: 600;");
    m_overallText->setMinimumWidth(48);
    m_overallText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    overallRow->addWidget(m_overallText, 0);
    panelLayout->addLayout(overallRow);

    // Task list (scrollable)
    m_taskContainer = new QWidget(m_panel);
    m_taskLayout = new QVBoxLayout(m_taskContainer);
    m_taskLayout->setContentsMargins(0, 0, 0, 0);
    m_taskLayout->setSpacing(8);
    m_taskLayout->addStretch();

    m_scroll = new QScrollArea(m_panel);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(m_taskContainer);
    m_scroll->setStyleSheet("QScrollArea { background: transparent; }");
    panelLayout->addWidget(m_scroll, 1);

    m_summaryLabel = new QLabel(m_panel);
    m_summaryLabel->setStyleSheet("font-size: 12px; color: #6b7280;");
    m_summaryLabel->setWordWrap(true);
    panelLayout->addWidget(m_summaryLabel);

    // Cancel button (only interactive control)
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    m_cancelButton = new QPushButton(QStringLiteral("取消"), m_panel);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setMinimumWidth(96);
    m_cancelButton->setFixedHeight(36);
    m_cancelButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f4f6; border: 1px solid #d1d5db; border-radius: 8px;"
        "  color: #374151; font-size: 14px; font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #e5e7eb; }"
        "QPushButton:pressed { background-color: #d1d5db; }"
        "QPushButton:disabled { color: #9ca3af; }"
    );
    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadOverlay::onCancelClicked);
    buttonRow->addWidget(m_cancelButton);
    panelLayout->addLayout(buttonRow);

    hide();
}

DownloadOverlay::~DownloadOverlay() = default;

void DownloadOverlay::attachTo(QWidget *host)
{
    if (!host) return;
    m_host = host;
    setParent(host);
    // Install event filter on host to keep us covering it on resize.
    host->installEventFilter(this);
    relayout();
    raise();
    show();
}

void DownloadOverlay::setTitle(const QString &title)
{
    if (m_titleLabel) m_titleLabel->setText(title);
}

void DownloadOverlay::setPhaseLabel(const QString &text)
{
    if (m_phaseLabel) m_phaseLabel->setText(text);
}

void DownloadOverlay::setOverallRange(qint64 totalBytes)
{
    m_overallTotal = totalBytes;
    m_overallIndeterminate = (totalBytes <= 0);
    if (m_overallBar) {
        if (m_overallIndeterminate) {
            m_overallBar->setRange(0, 0);
        } else {
            m_overallBar->setRange(0, 1000);
        }
    }
    updateOverallText();
}

void DownloadOverlay::setOverallValue(qint64 doneBytes)
{
    m_overallDone = doneBytes;
    if (m_overallBar && !m_overallIndeterminate && m_overallTotal > 0) {
        int v = static_cast<int>((doneBytes * 1000) / m_overallTotal);
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        m_overallBar->setValue(v);
    }
    updateOverallText();
}

void DownloadOverlay::setOverallIndeterminate(bool indeterminate)
{
    m_overallIndeterminate = indeterminate;
    if (m_overallBar) {
        if (indeterminate) {
            m_overallBar->setRange(0, 0);
        } else {
            m_overallBar->setRange(0, 1000);
        }
    }
    updateOverallText();
}

void DownloadOverlay::updateOverallText()
{
    if (!m_overallText) return;
    if (m_overallIndeterminate || m_overallTotal <= 0) {
        m_overallText->setText(QStringLiteral("--"));
        return;
    }
    int pct = static_cast<int>((m_overallDone * 100) / m_overallTotal);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    m_overallText->setText(QString("%1%").arg(pct));
}

void DownloadOverlay::updateCountText()
{
    if (!m_countLabel) return;
    if (m_totalTasks <= 0) {
        m_countLabel->setText(QString());
        return;
    }
    m_countLabel->setText(QString("%1/%2").arg(m_doneTasks).arg(m_totalTasks));
}

void DownloadOverlay::clearTasks()
{
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        if (it.value().row) {
            m_taskLayout->removeWidget(it.value().row);
            it.value().row->deleteLater();
        }
    }
    m_rows.clear();
    m_totalTasks = 0;
    m_doneTasks = 0;
    updateCountText();
}

void DownloadOverlay::addTask(qint64 id, const QString &label)
{
    if (!m_taskLayout) return;

    TaskRow row;
    row.row = new QWidget(m_taskContainer);
    auto *rowLayout = new QVBoxLayout(row.row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(3);

    // Top line: file name (left) + status/percentage (right)
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);
    row.label = new QLabel(label, row.row);
    row.label->setStyleSheet("font-size: 12px; color: #374151;");
    row.label->setWordWrap(false);
    topRow->addWidget(row.label, 1);

    row.status = new QLabel(QStringLiteral("等待中"), row.row);
    row.status->setStyleSheet("font-size: 11px; color: #9ca3af;");
    row.status->setMinimumWidth(60);
    row.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topRow->addWidget(row.status, 0);
    rowLayout->addLayout(topRow);

    // Bottom line: thin progress bar (HMCL style, very thin)
    row.bar = new QProgressBar(row.row);
    row.bar->setRange(0, 1000);
    row.bar->setValue(0);
    row.bar->setTextVisible(false);
    row.bar->setFixedHeight(4);
    row.bar->setStyleSheet(
        "QProgressBar { background-color: #eef2ff; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background-color: #60a5fa; border-radius: 2px; }"
    );
    rowLayout->addWidget(row.bar);

    // Insert before the trailing stretch.
    int idx = m_taskLayout->count();
    if (idx > 0) idx -= 1;
    m_taskLayout->insertWidget(idx, row.row);

    m_rows.insert(id, row);

    // Keep the newest (active) task visible, HMCL-like auto-scroll.
    if (m_scroll) {
        QPointer<QScrollArea> guard(m_scroll);
        QTimer::singleShot(0, this, [guard]() {
            if (guard && guard->verticalScrollBar())
                guard->verticalScrollBar()->setValue(guard->verticalScrollBar()->maximum());
        });
    }
}

void DownloadOverlay::setTaskTotal(int total)
{
    m_totalTasks = total;
    updateCountText();
}

void DownloadOverlay::setTaskProgress(qint64 id, qint64 received, qint64 total)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end()) return;
    TaskRow &row = it.value();
    if (!row.bar) return;
    if (total > 0) {
        int v = static_cast<int>((received * 1000) / total);
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        if (row.bar->maximum() == 0) row.bar->setRange(0, 1000);
        row.bar->setValue(v);
    } else {
        if (row.bar->maximum() != 0) row.bar->setRange(0, 0);
    }
}

void DownloadOverlay::setTaskStatus(qint64 id, const QString &status)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end()) return;
    if (it.value().status) it.value().status->setText(status);
}

void DownloadOverlay::setTaskDone(qint64 id, bool ok)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end()) return;
    TaskRow row = it.value();

    if (row.bar) {
        if (row.bar->maximum() == 0) row.bar->setRange(0, 1000);
        row.bar->setValue(ok ? 1000 : row.bar->value());
    }

    if (ok) {
        ++m_doneTasks;
        updateCountText();
        if (row.status) {
            row.status->setText(QStringLiteral("\u2713"));
            row.status->setStyleSheet("font-size: 12px; color: #16a34a; font-weight: 700;");
        }
        // HMCL-style: completed per-file rows fade out and disappear.
        m_rows.remove(id);
        QWidget *rowWidget = row.row;
        if (rowWidget) {
            QPointer<QWidget> guard(rowWidget);
            QTimer::singleShot(400, this, [guard]() {
                if (!guard) return;
                guard->setMaximumHeight(0);
            });
            QTimer::singleShot(550, this, [guard]() {
                if (!guard) return;
                guard->setParent(nullptr);
                guard->deleteLater();
            });
        }
    } else {
        if (row.status) {
            row.status->setText(QStringLiteral("\u2717"));
            row.status->setStyleSheet("font-size: 12px; color: #dc2626; font-weight: 700;");
        }
    }
}

void DownloadOverlay::setSummaryText(const QString &text)
{
    if (m_summaryLabel) m_summaryLabel->setText(text);
}

void DownloadOverlay::onCancelClicked()
{
    m_cancelled = true;
    if (m_cancelButton) {
        m_cancelButton->setEnabled(false);
        m_cancelButton->setText(QStringLiteral("取消中..."));
    }
    if (m_phaseLabel) m_phaseLabel->setText(QStringLiteral("正在取消..."));
    emit cancelled();
}

void DownloadOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Semi-transparent mask over the whole host.
    painter.fillRect(rect(), QColor(15, 23, 42, 120));
}

void DownloadOverlay::relayout()
{
    if (!m_host || !m_panel) return;
    setGeometry(m_host->rect());

    const int w = qMin(kPanelWidth, qMax(320, m_host->width() - kPanelMargin * 2));
    const int maxH = qMax(240, m_host->height() - kPanelMargin * 2);
    int h = m_panel->sizeHint().height();
    if (h > maxH) h = maxH;
    if (h > kPanelMaxHeight) h = kPanelMaxHeight;
    if (h < 240) h = 240;

    const int x = (m_host->width() - w) / 2;
    const int y = (m_host->height() - h) / 2;
    m_panel->setGeometry(x, y, w, h);
    raise();
    if (m_panel) m_panel->raise();
}

void DownloadOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

bool DownloadOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        relayout();
    }
    return QWidget::eventFilter(watched, event);
}
