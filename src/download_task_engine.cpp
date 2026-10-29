#include "download_task_engine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {

bool computeSha1Matches(const QString &path, const QString &expected)
{
    if (expected.trimmed().isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&f)) {
        f.close();
        return false;
    }
    f.close();
    return QString::fromLatin1(hash.result().toHex()).compare(expected.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace

DownloadTaskEngine::DownloadTaskEngine(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
}

DownloadTaskEngine::~DownloadTaskEngine()
{
    cancel();
}

QList<DownloadTaskSpec> DownloadTaskEngine::filterPending(const QList<DownloadTaskSpec> &tasks,
                                                          bool verifyChecksums,
                                                          qint64 *skippedBytes)
{
    QList<DownloadTaskSpec> pending;
    qint64 skipped = 0;
    for (const DownloadTaskSpec &t : tasks) {
        if (t.filePath.isEmpty() || !t.url.isValid()) {
            // Invalid spec -> still enqueue so it surfaces as a failure.
            pending.append(t);
            continue;
        }
        if (QFileInfo::exists(t.filePath)) {
            if (!verifyChecksums || t.sha1.isEmpty() || computeSha1Matches(t.filePath, t.sha1)) {
                skipped += QFileInfo(t.filePath).size();
                continue;
            }
        }
        pending.append(t);
    }
    if (skippedBytes) *skippedBytes = skipped;
    return pending;
}

void DownloadTaskEngine::setConcurrency(int n)
{
    m_concurrency = qBound(1, n, 64);
}

void DownloadTaskEngine::start(const QList<DownloadTaskSpec> &tasks)
{
    if (m_running) return;

    m_pending = tasks;
    m_queue.clear();
    m_active.clear();
    m_taskReceived.clear();
    m_taskTotal.clear();
    m_completed = 0;
    m_nextIndex = 0;
    m_totalBytes = 0;
    m_doneBytes = 0;
    m_failed = false;
    m_cancelled = false;
    m_errorString.clear();
    m_running = true;

    for (int i = 0; i < m_pending.size(); ++i) {
        m_queue.enqueue(i);
    }

    emit overallRangeChanged(0, 0);
    pump();
}

void DownloadTaskEngine::cancel()
{
    if (!m_running && m_active.isEmpty()) {
        m_cancelled = true;
        return;
    }
    m_cancelled = true;
    m_queue.clear();

    const auto actives = m_active;
    for (Active *a : actives) {
        if (a->reply) {
            a->reply->abort();
        }
    }
}

void DownloadTaskEngine::pump()
{
    while (!m_cancelled && m_active.size() < m_concurrency && !m_queue.isEmpty()) {
        const int index = m_queue.dequeue();
        const DownloadTaskSpec spec = m_pending.at(index);

        // Prepare temp file path & directory.
        const QString dirPath = QFileInfo(spec.filePath).absolutePath();
        if (!QDir().mkpath(dirPath)) {
            m_failed = true;
            m_errorString = QString("无法创建目录: %1").arg(QDir::toNativeSeparators(dirPath));
            continue;
        }

        const QString tempPath = spec.filePath + ".part";
        QFile::remove(tempPath);
        QFile *file = new QFile(tempPath);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_failed = true;
            m_errorString = QString("无法写入临时文件: %1").arg(QDir::toNativeSeparators(tempPath));
            file->deleteLater();
            continue;
        }

        QNetworkRequest request(spec.url);
        for (const auto &h : spec.headers) {
            request.setRawHeader(h.first.toUtf8(), h.second.toUtf8());
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = m_manager->get(request);
        if (!reply) {
            m_failed = true;
            m_errorString = QStringLiteral("无法创建网络请求");
            file->close();
            file->deleteLater();
            continue;
        }

        auto *a = new Active;
        a->index = index;
        a->reply = reply;
        a->file = file;
        a->tempPath = tempPath;
        a->received = 0;
        a->total = 0;

        m_active.insert(reply, a);
        m_taskReceived[index] = 0;
        m_taskTotal[index] = 0;

        connect(reply, &QNetworkReply::readyRead, this, &DownloadTaskEngine::onReadyRead);
        connect(reply, &QNetworkReply::downloadProgress, this, &DownloadTaskEngine::onProgress);
        connect(reply, &QNetworkReply::finished, this, &DownloadTaskEngine::onFinished);

        emit taskStarted(index);
    }

    if (m_active.isEmpty() && (m_queue.isEmpty() || m_cancelled)) {
        // Batch complete.
        if (m_running) {
            m_running = false;
            emitOverall();
            const bool success = !m_cancelled && !m_failed && m_completed == m_pending.size();
            emit allFinished(success, m_cancelled ? QStringLiteral("下载已取消") : m_errorString);
        }
    }
}

void DownloadTaskEngine::onReadyRead()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    Active *a = m_active.value(reply, nullptr);
    if (!a || !a->file) return;

    const QByteArray chunk = reply->readAll();
    if (!chunk.isEmpty()) {
        if (a->file->write(chunk) != chunk.size()) {
            // Write error surfaces on finished.
        }
        a->received += chunk.size();
        m_taskReceived[a->index] = a->received;
        m_doneBytes += chunk.size();
    }
    emit taskProgress(a->index, a->received, a->total);
    emitOverall();
}

void DownloadTaskEngine::onProgress()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    Active *a = m_active.value(reply, nullptr);
    if (!a) return;

    const qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (total > 0 && a->total == 0) {
        a->total = total;
        m_taskTotal[a->index] = total;
        m_totalBytes += total;
        emit overallRangeChanged(m_totalBytes, m_doneBytes);
    }
    emit taskProgress(a->index, a->received, a->total);
}

void DownloadTaskEngine::onFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    Active *a = m_active.value(reply, nullptr);
    if (!a) {
        reply->deleteLater();
        return;
    }

    const bool netOk = (reply->error() == QNetworkReply::NoError);
    QString err;
    if (!netOk) {
        if (m_cancelled || reply->error() == QNetworkReply::OperationCanceledError) {
            err = QStringLiteral("下载已取消");
        } else {
            err = reply->errorString();
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus >= 400) {
                err += QString(" (HTTP %1)").arg(httpStatus);
            }
        }
    }

    bool ok = netOk;

    // Flush any remaining buffered data.
    if (a->file) {
        if (ok) {
            const QByteArray rest = reply->readAll();
            if (!rest.isEmpty()) {
                if (a->file->write(rest) != rest.size()) {
                    ok = false;
                    err = QStringLiteral("写入文件失败");
                } else {
                    a->received += rest.size();
                    m_doneBytes += rest.size();
                }
            }
        }
        a->file->flush();
        a->file->close();
    }

    // Checksum verification.
    if (ok && !m_pending.at(a->index).sha1.trimmed().isEmpty()) {
        if (!computeSha1Matches(a->tempPath, m_pending.at(a->index).sha1)) {
            ok = false;
            err = QStringLiteral("校验失败 (SHA-1 不匹配)");
        }
    }

    finishActive(a, ok, err);
}

void DownloadTaskEngine::finishActive(Active *a, bool ok, const QString &error)
{
    const int index = a->index;
    const QString tempPath = a->tempPath;
    const QString finalPath = m_pending.at(index).filePath;

    if (a->reply) {
        m_active.remove(a->reply);
        a->reply->deleteLater();
        a->reply = nullptr;
    }
    if (a->file) {
        a->file->deleteLater();
        a->file = nullptr;
    }
    delete a;

    if (ok) {
        QFile::remove(finalPath);
        if (!QFile::rename(tempPath, finalPath)) {
            ok = false;
            QFile::remove(tempPath);
        }
    } else {
        QFile::remove(tempPath);
        if (!m_cancelled) {
            m_failed = true;
            if (m_errorString.isEmpty() && !error.isEmpty()) {
                m_errorString = QString("%1: %2").arg(m_pending.at(index).label, error);
            }
        }
    }

    ++m_completed;

    emit taskFinished(index, ok, error);
    emitOverall();
    pump();
}

void DownloadTaskEngine::emitOverall()
{
    emit overallProgress(m_doneBytes, m_totalBytes);
}
