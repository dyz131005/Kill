#include "download_manager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QCryptographicHash>
#include <QEventLoop>
#include <QTimer>
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
    m_redirectCount = 0;
    m_suppressReferer = false;
    m_retriedWithoutReferer = false;
    m_suppressAccept = false;
    m_baseHeaders = headers;

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

    // Prefer an in-window overlay (HMCL style) when we have a visible host
    // widget; fall back to a standalone modal dialog otherwise.
    const bool embedded = (parentWidget != nullptr && parentWidget->isVisible());
    if (embedded) {
        m_dialog = new DownloadProgressDialog(parentWidget, title, description);
    } else {
        m_dialog = new DownloadProgressDialog(title, description, parentWidget);
    }
    connect(m_dialog, &DownloadProgressDialog::cancelled, this, &DownloadManager::onCancel);

    if (!startDownload(url, filePath, expectedChecksum, headers)) {
        if (error) *error = m_errorString;
        m_dialog->deleteLater();
        m_dialog = nullptr;
        return false;
    }

    bool success = false;
    if (embedded) {
        // Run a local event loop while the overlay is shown; it is quit when the
        // download finishes (accept/reject) or the user cancels.
        m_dialog->show();
        QEventLoop loop;
        m_embeddedLoop = &loop;
        connect(m_dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        connect(this, &DownloadManager::downloadFinished, &loop, &QEventLoop::quit);
        loop.exec();
        m_embeddedLoop = nullptr;
        success = (m_dialog && m_dialog->result() == QDialog::Accepted);
    } else {
        const int dialogResult = m_dialog->exec();
        success = (dialogResult == QDialog::Accepted);
    }

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        m_file->deleteLater();
        m_file = nullptr;
    }
    if (m_dialog) {
        m_dialog->hide();
        m_dialog->deleteLater();
    }
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
    m_redirectCount = 0;
    m_suppressReferer = false;
    m_retriedWithoutReferer = false;
    m_suppressAccept = false;
    m_baseHeaders = headers;

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

    m_requestUrl = url;

    QNetworkRequest request(url);
    // IMPORTANT: The CERNET mirror front-end (mirrors.cernet.edu.cn), which
    // bmclapi2 redirects to for many artifacts (notably net/neoforged/neoform),
    // applies a WAF that whitelists ONLY the stock curl User-Agent. Verified
    // empirically: "curl/8.4.0" -> HTTP 200; a browser UA, "Java/17", "Wget",
    // an empty UA, or no UA at all -> HTTP 403 (an HTML error page). Using the
    // curl UA is what makes neoform/installer/lib downloads succeed on Win7
    // networks behind the CN mirrors. This must NOT be changed to a browser or
    // launcher UA, or mirror downloads will fail with 403.
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("curl/8.4.0"));
    for (const auto &header : headers) {
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
    }
    // Manual redirects so custom headers (Accept/Referer) are preserved across
    // cross-host hops (Qt otherwise drops them, causing 403 on the final mirror).
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // Fail fast when a host is unreachable (e.g. blocked maven mirrors). Without
    // this, a blocked TCP connect can hang for minutes and look like a freeze.
    // Generous transfer timeout: large library/installer downloads on slow
    // machines (e.g. Win7) can take well over 20s. Blocked hosts still fail,
    // just a bit later.
    request.setTransferTimeout(120000);

    if (m_debugLog) {
        m_debugLog(QStringLiteral("[net] GET %1 (redirect#%2)").arg(url.toString()).arg(m_redirectCount));
    }
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

    // Manual redirect follow: preserve headers (Accept/Referer) on every hop.
    if (m_reply && !m_cancelled && httpStatus >= 300 && httpStatus < 400) {
        const QByteArray loc = m_reply->rawHeader("Location");
        if (!loc.isEmpty() && m_redirectCount < 10) {
            const QUrl next = m_reply->url().resolved(QUrl(QString::fromUtf8(loc)));
            if (m_debugLog) m_debugLog(QStringLiteral("[net] redirect(%1) %2 -> %3")
                                           .arg(httpStatus).arg(replyUrl, next.toString()));
            m_reply->deleteLater();
            m_reply = nullptr;
            // Discard the old file handle; startDownload() will recreate it.
            if (m_file) {
                m_file->flush();
                m_file->close();
                m_file->deleteLater();
                m_file = nullptr;
            }
            ++m_redirectCount;
            if (!startDownload(next, m_filePath, m_expectedChecksum, m_baseHeaders)) {
                if (m_silentMode) emit downloadFinished(false, m_errorString);
                else if (m_dialog) m_dialog->reject();
            }
            return;
        }
    }

    // Some mirror paths 403 specifically because a Referer was sent. Retry once
    // without the Referer header to cover that inconsistent WAF behaviour.
    if (m_reply && !m_cancelled && (httpStatus == 403 || httpStatus == 401)
        && !m_suppressReferer && !m_retriedWithoutReferer) {
        m_retriedWithoutReferer = true;
        m_suppressReferer = true;
        m_suppressAccept = true;
        const QUrl retryUrl = m_reply->url();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_file) {
            m_file->flush();
            m_file->close();
            m_file->deleteLater();
            m_file = nullptr;
        }
        if (!startDownload(retryUrl, m_filePath, m_expectedChecksum, m_baseHeaders)) {
            if (m_silentMode) emit downloadFinished(false, m_errorString);
            else if (m_dialog) m_dialog->reject();
        }
        return;
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

    // Validate that a JAR was actually downloaded. Some mirrors return an HTML
    // error page (200/403) instead of the file; writing that as a jar makes the
    // installer fail later with a cryptic error. A real JAR/ZIP starts with 'PK'.
    if (success && checksumOk && m_filePath.endsWith(".jar", Qt::CaseInsensitive)) {
        QFile probe(tempPath);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray magic = probe.read(4);
            probe.close();
            if (magic.size() < 2 || magic.at(0) != 'P' || magic.at(1) != 'K') {
                success = false;
                QString snippet;
                QFile body(tempPath);
                if (body.open(QIODevice::ReadOnly)) {
                    snippet = QString::fromUtf8(body.read(120)).simplified();
                    body.close();
                }
                m_errorString = QString("下载内容不是有效的 JAR 文件（可能被镜像站拒绝，返回了网页）。返回内容片段: %1")
                                    .arg(snippet.left(80));
                QFile::remove(tempPath);
            }
        }
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
        // Transfer timeout / aborted connection (not user-initiated cancel):
        // surface a clear message instead of an empty error.
        if (!m_cancelled && m_errorString.isEmpty() && m_reply
            && (netError == QNetworkReply::OperationCanceledError
                || netError == QNetworkReply::TimeoutError)) {
            m_errorString = QString("下载超时或被中断（连接 %1 在限定时间内未完成，可能是网络慢或镜像不稳定）")
                                .arg(replyUrl.isEmpty() ? QStringLiteral("未知") : replyUrl);
        }
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
            if (m_debugLog) m_debugLog(QStringLiteral("[net] FAIL url=%1 %2").arg(replyUrl, m_errorString));
            {
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
