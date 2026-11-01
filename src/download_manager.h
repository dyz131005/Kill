#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <functional>

#include "download_progress_dialog.h"

class QEventLoop;

class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QNetworkAccessManager *manager, QObject *parent = nullptr);

    enum class ChecksumAlgo { None, Sha1, Sha256 };
    static ChecksumAlgo detectChecksumAlgo(const QString &checksum);

    bool downloadFile(const QUrl &url,
                      const QString &filePath,
                      const QString &title,
                      const QString &description,
                      QString *error,
                      QWidget *parentWidget = nullptr,
                      const QString &expectedChecksum = QString(),
                      const QList<QPair<QString, QString>> &headers = {});

    void downloadFileSilent(const QUrl &url,
                            const QString &filePath,
                            const QString &expectedChecksum = QString(),
                            const QList<QPair<QString, QString>> &headers = {});

    bool isCancelled() const { return m_cancelled; }
    QString errorString() const { return m_errorString; }
    // Optional debug sink to trace each network hop (used for Win7 diagnosis).
    void setDebugLog(std::function<void(const QString &)> fn) { m_debugLog = std::move(fn); }

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(bool success, const QString &error);

public slots:
    void cancel();

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onReadyRead();
    void onFinished();
    void onCancel();

private:
    bool startDownload(const QUrl &url,
                       const QString &filePath,
                       const QString &expectedChecksum,
                       const QList<QPair<QString, QString>> &headers);

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QString m_filePath;
    DownloadProgressDialog *m_dialog = nullptr;
    bool m_cancelled = false;
    QString m_expectedChecksum;
    ChecksumAlgo m_checksumAlgo = ChecksumAlgo::None;
    QString m_errorString;
    bool m_silentMode = false;
    // Manual redirect handling: some CN mirrors drop custom headers (Accept/
    // Referer) on cross-host redirects, causing 403. We follow redirects
    // ourselves and re-add headers with an origin Referer on every hop.
    QList<QPair<QString, QString>> m_baseHeaders;
    QUrl m_requestUrl;
    int m_redirectCount = 0;
    // Mirror WAFs are inconsistent: some paths 403 when a Referer is sent,
    // others 403 when it isn't. When true, the next attempt omits Referer.
    bool m_suppressReferer = false;
    bool m_retriedWithoutReferer = false;
    // Some mirror hops (CERNET) reject browser-like Accept headers (403), while
    // others (USTC) require them. On a 403 retry we also drop the fancy Accept.
    bool m_suppressAccept = false;
    // Embedded (in-window) progress: when parentWidget is usable we show the
    // overlay dialog and pump a local event loop here instead of exec()'ing a
    // separate modal window.
    QEventLoop *m_embeddedLoop = nullptr;
    std::function<void(const QString &)> m_debugLog;
};
