#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QFile>
#include <QDir>

#include "download_progress_dialog.h"

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
};
