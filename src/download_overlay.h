#pragma once

#include <QWidget>
#include <QHash>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class QScrollArea;

// HMCL-style in-window download overlay.
// A semi-transparent mask covers the whole parent (blocking clicks),
// with a centered rounded panel showing overall progress + a per-file task list.
// Only the Cancel button is interactive.
class DownloadOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadOverlay(QWidget *parent = nullptr);
    ~DownloadOverlay() override;

    // Cover and center inside the given host widget (usually the main window).
    void attachTo(QWidget *host);

    void setTitle(const QString &title);
    void setPhaseLabel(const QString &text);

    // Overall progress across all tasks.
    void setOverallRange(qint64 totalBytes);
    void setOverallValue(qint64 doneBytes);
    void setOverallIndeterminate(bool indeterminate);

    // Per-file task rows. Ids are opaque handles chosen by the caller.
    void clearTasks();
    void setTaskTotal(int total);
    void addTask(qint64 id, const QString &label);
    void setTaskProgress(qint64 id, qint64 received, qint64 total);
    void setTaskStatus(qint64 id, const QString &status);
    void setTaskDone(qint64 id, bool ok);

    void setSummaryText(const QString &text);

    bool isCancelled() const { return m_cancelled; }

signals:
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct TaskRow {
        QWidget *row = nullptr;
        QLabel *label = nullptr;
        QProgressBar *bar = nullptr;
        QLabel *status = nullptr;
    };

    void relayout();
    void updateOverallText();
    void updateCountText();
    void onCancelClicked();

    QWidget *m_host = nullptr;
    QWidget *m_panel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_phaseLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_overallText = nullptr;
    QProgressBar *m_overallBar = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_taskContainer = nullptr;
    QVBoxLayout *m_taskLayout = nullptr;

    QHash<qint64, TaskRow> m_rows;
    qint64 m_overallTotal = 0;
    qint64 m_overallDone = 0;
    bool m_overallIndeterminate = true;
    int m_totalTasks = 0;
    int m_doneTasks = 0;
    bool m_cancelled = false;
};
