#include "download_manager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QCryptographicHash>
#include <QMessageBox>
#include <QMetaObject>
#include <QMetaEnum>
#include <QRegularExpression>

DownloadManager::DownloadManager(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent), m_networkManager(manager)
{
}

static inline int clampToIntPercent(qint64 value, qint64 total) {
    if (total <= 0) return 0;
    qint64 pct = value * 100 / total;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return static_cast<int>(pct);
}

static QString formatBytes(qint64 bytes) {
    if (bytes < 0) return "?";
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

static QString win32ErrorMessage(unsigned long errorCode)
{
#ifdef Q_OS_WIN
    if (errorCode == 0) return QString();
    LPWSTR messageBuffer = nullptr;
    const size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);
    QString msg = QString::fromWCharArray(messageBuffer, static_cast<int>(size)).trimmed();
    LocalFree(messageBuffer);
    if (msg.endsWith('\n')) msg.chop(1);
    if (msg.endsWith('\r')) msg.chop(1);
    return msg;
#else
    Q_UNUSED(errorCode);
    return QString();
#endif
}

static QString ioContextForPath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    QString info = QString("路径=\"%1\", exists=%2").arg(native).arg(QFileInfo::exists(native));
    const QFileInfo fi(native);
    if (fi.exists()) {
        info += QString(", isDir=%1, isFile=%2, isWritable=%3, size=%4")
                    .arg(fi.isDir())
                    .arg(fi.isFile())
                    .arg(fi.isWritable())
                    .arg(fi.size());
    }
    const QString parent = QFileInfo(native).absolutePath();
    const QFileInfo pfi(parent);
    info += QString(" | parent=\"%1\", exists=%2, isDir=%3, isWritable=%4")
                .arg(QDir::toNativeSeparators(parent))
                .arg(pfi.exists())
                .arg(pfi.isDir())
                .arg(pfi.isWritable());
    return info;
}

DownloadManager::ChecksumAlgo DownloadManager::detectChecksumAlgo(const QString &checksum)
{
    if (checksum.isEmpty()) return ChecksumAlgo::None;
    const QString s = checksum.trimmed();
    static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]+$"));
    if (!hexRe.match(s).hasMatch()) return ChecksumAlgo::None;
    switch (s.size()) {
    case 40: return ChecksumAlgo::Sha1;
    case 64: return ChecksumAlgo::Sha256;
    default: return ChecksumAlgo::None;
    }
}

static QString computeFileChecksum(const QString &path,
                                   DownloadManager::ChecksumAlgo algo,
                                   bool *ok = nullptr)
{
    if (ok) *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash::Algorithm qtAlgo;
    const char *algoName = nullptr;
    switch (algo) {
    case DownloadManager::ChecksumAlgo::Sha1:
        qtAlgo = QCryptographicHash::Sha1;
        algoName = "SHA-1";
        break;
    case DownloadManager::ChecksumAlgo::Sha256:
        qtAlgo = QCryptographicHash::Sha256;
        algoName = "SHA-256";
        break;
    default:
        return QString();
    }
    Q_UNUSED(algoName);
    QCryptographicHash hash(qtAlgo);
    if (!hash.addData(&file)) {
        file.close();
        return QString();
    }
    file.close();
    if (ok) *ok = true;
    return QString::fromLatin1(hash.result().toHex());
}

bool DownloadManager::downloadFile(const QUrl &url,
                                   const QString &filePath,
                                   const QString &title,
                                   const QString &description,
                                   QString *error,
                                   QWidget *parentWidget,
                                   const QString &expectedChecksum,
                                   const QList<QPair<QString, QString>> &headers)
{
    m_silentMode = false;
    m_filePath = filePath;
    m_expectedChecksum = expectedChecksum.trimmed();
    m_checksumAlgo = detectChecksumAlgo(m_expectedChecksum);
    m_cancelled = false;
    m_errorString.clear();

    if (error) {
        error->clear();
    }

    if (QFile::exists(filePath)) {
        if (m_checksumAlgo == ChecksumAlgo::None) {
            return true;
        }
        bool ok = false;
        const QString actual = computeFileChecksum(filePath, m_checksumAlgo, &ok);
        if (ok && actual.compare(m_expectedChecksum, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    m_dialog = new DownloadProgressDialog(title, description, parentWidget);
    connect(m_dialog, &DownloadProgressDialog::cancelled, this, &DownloadManager::onCancel);

    if (!startDownload(url, filePath, expectedChecksum, headers)) {
        if (error) *error = m_errorString;
        m_dialog->deleteLater();
        m_dialog = nullptr;
        return false;
    }

    const int dialogResult = m_dialog->exec();
    const bool success = (dialogResult == QDialog::Accepted);

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        m_file->deleteLater();
        m_file = nullptr;
    }
    m_dialog->deleteLater();
    m_dialog = nullptr;

    if (m_cancelled || !success) {
        QFile::remove(filePath + ".tmp");
        if (error) {
            *error = m_cancelled ? "下载已取消" : m_errorString;
        }
        return false;
    }

    return true;
}

void DownloadManager::downloadFileSilent(const QUrl &url,
                                         const QString &filePath,
                                         const QString &expectedChecksum,
                                         const QList<QPair<QString, QString>> &headers)
{
    m_silentMode = true;
    m_filePath = filePath;
    m_expectedChecksum = expectedChecksum.trimmed();
    m_checksumAlgo = detectChecksumAlgo(m_expectedChecksum);
    m_cancelled = false;
    m_errorString.clear();

    if (QFile::exists(filePath)) {
        if (m_checksumAlgo == ChecksumAlgo::None) {
            emit downloadFinished(true, QString());
            return;
        }
        bool ok = false;
        const QString actual = computeFileChecksum(filePath, m_checksumAlgo, &ok);
        if (ok && actual.compare(m_expectedChecksum, Qt::CaseInsensitive) == 0) {
            emit downloadFinished(true, QString());
            return;
        }
    }

    if (!startDownload(url, filePath, expectedChecksum, headers)) {
        emit downloadFinished(false, m_errorString);
    }
}

void DownloadManager::cancel()
{
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort();
    }
}

bool DownloadManager::startDownload(const QUrl &url,
                                    const QString &filePath,
                                    const QString &expectedChecksum,
                                    const QList<QPair<QString, QString>> &headers)
{
    Q_UNUSED(expectedChecksum);
    const QString tempPath = filePath + ".tmp";

    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(dirPath)) {
#ifdef Q_OS_WIN
        const unsigned long lastErr = GetLastError();
#else
        const unsigned long lastErr = 0;
#endif
        QString errMsg = QString("无法创建目录: %1").arg(QDir::toNativeSeparators(dirPath));
        errMsg += QString(" | IO上下文: %1").arg(ioContextForPath(dirPath));
        if (lastErr != 0) {
            const QString desc = win32ErrorMessage(lastErr);
            errMsg += QString(" | Win32错误码=%1 (0x%2), 描述: %3")
                          .arg(lastErr)
                          .arg(lastErr, 0, 16)
                          .arg(desc.isEmpty() ? QString("(无系统描述)") : desc);
        }
        m_errorString = errMsg;
        return false;
    }

    if (QFile::exists(tempPath)) {
        QFile::remove(tempPath);
    }

    m_file = new QFile(tempPath, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
#ifdef Q_OS_WIN
        const unsigned long lastErr = GetLastError();
#else
        const unsigned long lastErr = 0;
#endif
        QString errMsg = QString("无法创建临时文件: %1").arg(QDir::toNativeSeparators(tempPath));
        errMsg += QString(" | IO上下文: %1").arg(ioContextForPath(tempPath));
        if (lastErr != 0) {
            const QString desc = win32ErrorMessage(lastErr);
            errMsg += QString(" | Win32错误码=%1 (0x%2), 描述: %3")
                          .arg(lastErr)
                          .arg(lastErr, 0, 16)
                          .arg(desc.isEmpty() ? QString("(无系统描述)") : desc);
        }
        errMsg += QString(" | QFile.error=%1, QFile.errorString=%2")
                      .arg(m_file->error())
                      .arg(m_file->errorString());
        m_errorString = errMsg;
        m_file->deleteLater();
        m_file = nullptr;
        return false;
    }

    QNetworkRequest request(url);
    for (const auto &header : headers) {
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_networkManager->get(request);
    if (!m_reply) {
        m_errorString = "无法创建网络请求";
        m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
        return false;
    }

    connect(m_reply, &QNetworkReply::downloadProgress, this, &DownloadManager::onDownloadProgress);
    connect(m_reply, &QNetworkReply::readyRead, this, &DownloadManager::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadManager::onFinished);

    return true;
}

void DownloadManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    emit downloadProgress(bytesReceived, bytesTotal);
    if (!m_silentMode && m_dialog) {
        const int percent = clampToIntPercent(bytesReceived, bytesTotal);
        m_dialog->setProgress(percent, 100);
        if (bytesTotal > 0) {
            m_dialog->setStatus(QString("已下载: %1 / %2")
                                    .arg(formatBytes(bytesReceived), formatBytes(bytesTotal)));
        } else {
            m_dialog->setStatus(QString("已下载: %1").arg(formatBytes(bytesReceived)));
        }
    }
}

void DownloadManager::onReadyRead()
{
    if (!m_reply || !m_file) return;
    const QByteArray data = m_reply->readAll();
    if (data.isEmpty()) return;
    m_file->write(data);
}

void DownloadManager::onFinished()
{
    QNetworkReply::NetworkError netError = QNetworkReply::UnknownNetworkError;
    int httpStatus = 0;
    QString httpReason;
    QString replyUrl;
    QString contentLength;
    qint64 fileSize = -1;

    if (m_reply) {
        netError = m_reply->error();
        httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        replyUrl = m_reply->url().toString();
        const QVariant clHeader = m_reply->header(QNetworkRequest::ContentLengthHeader);
        if (clHeader.isValid()) {
            contentLength = clHeader.toString();
        }
        const QVariant reasonVar = m_reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
        if (reasonVar.isValid()) {
            httpReason = reasonVar.toString();
        }
    }

    if (m_file) {
        m_file->flush();
        m_file->close();
    }

    const QString tempPath = m_filePath + ".tmp";
    QFileInfo tempInfo(tempPath);
    if (tempInfo.exists()) {
        fileSize = tempInfo.size();
    }

    bool success = (m_reply && netError == QNetworkReply::NoError && !m_cancelled);
    bool checksumOk = true;
    QString checksumDetail;

    if (success && m_checksumAlgo != ChecksumAlgo::None) {
        bool readOk = false;
        const QString actual = computeFileChecksum(tempPath, m_checksumAlgo, &readOk);
        const QString algoLabel = (m_checksumAlgo == ChecksumAlgo::Sha1) ? "SHA-1" : "SHA-256";
        if (!readOk) {
            checksumOk = false;
            checksumDetail = QString("无法读取文件进行%1校验").arg(algoLabel);
        } else if (actual.compare(m_expectedChecksum, Qt::CaseInsensitive) != 0) {
            checksumOk = false;
            checksumDetail = QString("%1 不匹配: expected=%2 actual=%3")
                                 .arg(algoLabel,
                                      m_expectedChecksum.left(16) + "...",
                                      actual.left(16) + "...");
        } else {
            checksumDetail = QString("%1 OK").arg(algoLabel);
        }
        if (!checksumOk) {
            success = false;
            m_errorString = QString("文件校验失败: %1").arg(checksumDetail);
        }
    } else if (success && m_checksumAlgo == ChecksumAlgo::None) {
        checksumDetail = "(未提供校验值，跳过)";
    }

    if (success && checksumOk) {
        QFile::remove(m_filePath);
        if (!QFile::rename(tempPath, m_filePath)) {
            m_errorString = QString("无法重命名临时文件: %1 -> %2").arg(tempPath, m_filePath);
            QFile::remove(tempPath);
            if (m_silentMode) {
                emit downloadFinished(false, m_errorString);
            } else if (m_dialog) {
                m_dialog->reject();
            }
            return;
        }
        if (m_silentMode) {
            emit downloadFinished(true, QString());
        } else if (m_dialog) {
            m_dialog->accept();
        }
    } else {
        if (m_reply && netError != QNetworkReply::NoError
                   && netError != QNetworkReply::OperationCanceledError
                   && m_errorString.isEmpty()) {
            QString errDetail;
            const QMetaObject &mo = QNetworkReply::staticMetaObject;
            const int enumIdx = mo.indexOfEnumerator("NetworkError");
            if (enumIdx >= 0) {
                QMetaEnum me = mo.enumerator(enumIdx);
                errDetail = QString("NetworkError=%1 (%2)").arg(me.valueToKey(netError)).arg(static_cast<int>(netError));
            } else {
                errDetail = QString("NetworkError=%1").arg(static_cast<int>(netError));
            }
            if (httpStatus > 0) {
                errDetail += QString(", HTTP %1").arg(httpStatus);
                if (!httpReason.isEmpty()) errDetail += QString(" %2").arg(httpReason);
            }
            if (!replyUrl.isEmpty() && replyUrl != m_reply->request().url().toString()) {
                errDetail += QString(", finalURL=%1").arg(replyUrl);
            }
            errDetail += QString(", fileSize=%1").arg(formatBytes(fileSize));
            if (!contentLength.isEmpty()) errDetail += QString(", Content-Length=%1").arg(contentLength);
            if (!checksumDetail.isEmpty()) errDetail += QString(", %1").arg(checksumDetail);

            const QString rawErr = m_reply ? m_reply->errorString() : QString();
            if (rawErr.isEmpty() || rawErr.compare("Unknown error", Qt::CaseInsensitive) == 0) {
                m_errorString = QString("下载失败[%1]").arg(errDetail);
            } else {
                m_errorString = QString("%1 [%2]").arg(rawErr, errDetail);
            }
        } else if (m_cancelled && m_errorString.isEmpty()) {
            m_errorString = "下载已取消";
        }
        QFile::remove(tempPath);
        if (m_silentMode) {
            emit downloadFinished(false, m_cancelled ? "下载已取消" : m_errorString);
        } else if (m_dialog) {
            m_dialog->reject();
        }
    }
}

void DownloadManager::onCancel()
{
    cancel();
}
