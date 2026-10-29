#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QHash>
#include <QQueue>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// A single file to download.
struct DownloadTaskSpec {
    QUrl url;
    QString filePath;
    QString sha1;        // expected checksum (optional)
    QString label;       // short display name
    QList<QPair<QString, QString>> headers;
};

// HMCL-style parallel download engine.
// Runs up to `concurrency` transfers at once over the shared
// QNetworkAccessManager. Emits fine-grained per-task progress and
// coarse overall progress. Supports cancellation.
class DownloadTaskEngine : public QObject
{
    Q_OBJECT

public:
    explicit DownloadTaskEngine(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~DownloadTaskEngine() override;

    // Validate files up-front (existing + checksum match are skipped).
    // Returns the list of tasks that actually need downloading.
    // `skippedBytes` accumulates the size of files already present.
    static QList<DownloadTaskSpec> filterPending(const QList<DownloadTaskSpec> &tasks,
                                                 bool verifyChecksums,
                                                 qint64 *skippedBytes);

    void setConcurrency(int n);
    void start(const QList<DownloadTaskSpec> &tasks);
    void cancel();

    bool isRunning() const { return m_running; }
    bool wasCancelled() const { return m_cancelled; }
    QString errorString() const { return m_errorString; }

signals:
    // Total bytes known after all HEAD-less transfers begin; may be 0 if unknown.
    void overallRangeChanged(qint64 totalBytes, qint64 alreadyDoneBytes);
    void overallProgress(qint64 doneBytes, qint64 totalBytes);

    void taskStarted(int index);
    void taskProgress(int index, qint64 received, qint64 total);
    void taskFinished(int index, bool ok, const QString &error);

    // Emitted once when the whole batch is done (success iff all tasks ok).
    void allFinished(bool success, const QString &error);

private slots:
    void onReadyRead();
    void onProgress();
    void onFinished();

private:
    struct Active {
        int index = -1;
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
        qint64 received = 0;
        qint64 total = 0;
        QString tempPath;
    };

    void pump();
    void finishActive(Active *a, bool ok, const QString &error);
    void emitOverall();

    QNetworkAccessManager *m_manager = nullptr;
    QList<DownloadTaskSpec> m_pending;
    QQueue<int> m_queue;
    QHash<QNetworkReply *, Active *> m_active;

    int m_concurrency = 8;
    int m_nextIndex = 0;
    int m_completed = 0;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_failed = false;
    QString m_errorString;

    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;
    QHash<int, qint64> m_taskReceived;
    QHash<int, qint64> m_taskTotal;
};
