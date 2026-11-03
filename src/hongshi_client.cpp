#include "hongshi_client.h"

#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QTimer>
#include <QRegularExpression>

namespace {
const QString kApiBase = QStringLiteral("https://hongshi.site");
}

HongshiClient::HongshiClient(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

HongshiClient::~HongshiClient()
{
    stop();
}

QString HongshiClient::defaultDir(const QString &configDir)
{
    return configDir + QStringLiteral("/hongshi");
}

QString HongshiClient::executableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("hongshic-windows-amd64.exe");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("hongshic-macos-arm64");
#else
    return QStringLiteral("hongshic-linux-amd64");
#endif
}

void HongshiClient::setExecutable(const QString &exePath)
{
    m_exePath = exePath;
}

bool HongshiClient::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void HongshiClient::ensureKernel(const QString &dir)
{
    if (m_downloading) return;

    QDir().mkpath(dir);
    const QString dest = dir + "/" + executableName();
    if (QFileInfo::exists(dest) && QFileInfo(dest).size() > 0) {
        m_exePath = dest;
        emit logLine(QStringLiteral("[hongshi] 已存在内核: %1").arg(dest));
        emit downloadFinished(dest);
        return;
    }

    QString platform = QStringLiteral("windows");
    QString arch = QStringLiteral("amd64");
#ifdef Q_OS_MACOS
    platform = QStringLiteral("macos");
    arch = QStringLiteral("arm64");
#elif defined(Q_OS_LINUX)
    platform = QStringLiteral("linux");
#endif

    const QUrl url(QStringLiteral("%1/api/download/client?platform=%2&arch=%3")
                       .arg(kApiBase, platform, arch));
    m_exePath = dest;
    requestKernelDownload(url, dest);
}

void HongshiClient::requestKernelDownload(const QUrl &url, const QString &destPath, int redirectsLeft)
{
    m_downloading = true;
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);

    emit logLine(QStringLiteral("[hongshi] 下载内核: %1").arg(url.toString()));
    QNetworkReply *reply = m_net->get(req);
    m_downloadReply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit downloadProgress(received, total); });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, destPath, redirectsLeft]() {
        reply->deleteLater();
        if (m_downloadReply == reply) m_downloadReply = nullptr;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray location = reply->rawHeader("Location");
        if (status >= 300 && status < 400 && !location.isEmpty() && redirectsLeft > 0) {
            const QUrl next = reply->url().resolved(QUrl(QString::fromUtf8(location)));
            requestKernelDownload(next, destPath, redirectsLeft - 1);
            return;
        }

        m_downloading = false;

        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            if (status == 429) {
                err = QStringLiteral("下载被限流，请稍后重试");
            } else if (status > 0) {
                const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
                const QString expected = obj.value("expected_file").toString();
                err = QStringLiteral("HTTP %1: %2").arg(status).arg(
                    expected.isEmpty() ? reply->errorString() : expected);
            }
            emit logLine(QStringLiteral("[hongshi] 内核下载失败: %1").arg(err));
            emit failed(QStringLiteral("下载红石联机内核失败: %1").arg(err));
            return;
        }

        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            emit failed(QStringLiteral("下载红石联机内核失败: 空响应"));
            return;
        }

        QFile out(destPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit failed(QStringLiteral("无法写入联机内核: %1").arg(destPath));
            return;
        }
        out.write(bytes);
        out.close();
        emit logLine(QStringLiteral("[hongshi] 内核已下载: %1 (%2 字节)").arg(destPath).arg(bytes.size()));
        emit downloadFinished(destPath);
    });
}

void HongshiClient::fetchRelays()
{
    QNetworkRequest req(QUrl(kApiBase + QStringLiteral("/api/server/list")));
    req.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logLine(QStringLiteral("[hongshi] 获取中继列表失败: %1").arg(reply->errorString()));
            emit relaysReady({});
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_relays.clear();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString addr = it.value().toString();
            if (!addr.isEmpty()) m_relays.append({ it.key(), addr });
        }
        emit logLine(QStringLiteral("[hongshi] 中继节点: %1 个").arg(m_relays.size()));
        emit relaysReady(m_relays);
    });
}

void HongshiClient::start(const QString &relay, int gamePort)
{
    if (isRunning()) {
        emit logLine(QStringLiteral("[hongshi] 内核已在运行"));
        return;
    }
    if (m_exePath.isEmpty() || !QFileInfo::exists(m_exePath)) {
        emit failed(QStringLiteral("找不到红石联机内核 hongshic"));
        return;
    }

    QString target = relay;
    if (target.isEmpty() && !m_relays.isEmpty()) target = m_relays.first().second;
    if (target.isEmpty()) {
        emit failed(QStringLiteral("没有可用的中继节点"));
        return;
    }

    m_relay = target;
    m_endpoint.clear();
    m_stdoutBuffer.clear();
    m_ready = false;

    m_process = new QProcess(this);
    m_process->setProgram(m_exePath);
    QStringList args{ "-t", target };
    if (gamePort > 0 && gamePort != 25565) {
        args << "-p" << QString::number(gamePort);
    }
    m_process->setArguments(args);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyRead, this, [this]() {
        if (!m_process) return;
        handleStdout(m_process->readAll());
    });
    // The Go kernel logs with a colour-capable logger that writes to stderr on
    // Windows. With MergedChannels the generic readyRead may not fire reliably
    // for stderr-only output, so read both channels explicitly.
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process) return;
        handleStdout(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process) return;
        handleStdout(m_process->readAllStandardError());
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        if (m_process) {
            const QByteArray tail = m_process->readAll();
            if (!tail.isEmpty()) handleStdout(tail);
        }
        emit logLine(QStringLiteral("[hongshi] 进程已退出, code=%1").arg(code));

        const bool wasReady = m_ready;
        m_ready = false;
        m_endpoint.clear();

        if (!wasReady) {
            emit failed(code == 1
                ? QStringLiteral("未能建立隧道：中继不可达或被拒绝，请换一个节点重试")
                : QStringLiteral("联机内核提前退出 (code=%1)").arg(code));
        }
        emit processExited(code);
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            emit failed(QStringLiteral("联机内核启动失败: %1")
                            .arg(m_process ? m_process->errorString() : QString()));
        }
    });

    emit logLine(QStringLiteral("[hongshi] 启动: %1 %2").arg(m_exePath, args.join(' ')));
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        emit failed(QStringLiteral("联机内核启动失败: %1").arg(m_process->errorString()));
        m_process->deleteLater();
        m_process = nullptr;
        return;
    }

    // The relay handshake normally completes in a few seconds. If no endpoint
    // shows up within 30s, surface an error instead of leaving the UI stuck on
    // "正在启动内核并申请隧道...".
    m_startTimeout = new QTimer(this);
    m_startTimeout->setSingleShot(true);
    connect(m_startTimeout, &QTimer::timeout, this, [this]() {
        if (m_ready) return;
        emit logLine(QStringLiteral("[hongshi] 等待隧道超时（30s）"));
        emit failed(QStringLiteral("等待中继分配隧道超时，请换一个中继节点重试"));
    });
    m_startTimeout->start(30000);
}

void HongshiClient::handleStdout(const QByteArray &chunk)
{
    QString text = QString::fromLocal8Bit(chunk);
    // The kernel logs through a colour-capable logger, so lines may contain
    // ANSI SGR sequences (ESC [ ... m). Strip them before parsing.
    static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-9;]*[A-Za-z]"));
    text.remove(ansi);

    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        emit logLine(QStringLiteral("[hongshi-stdout] %1").arg(trimmed));

        const int idx = trimmed.indexOf(QStringLiteral("endpoint="));
        if (idx >= 0 && !m_ready) {
            const QString rest = trimmed.mid(idx + 9);
            const QString addr = rest.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts).value(0).trimmed();
            if (!addr.isEmpty()) {
                m_endpoint = addr;
                m_ready = true;
                if (m_startTimeout) {
                    m_startTimeout->stop();
                    m_startTimeout->deleteLater();
                    m_startTimeout = nullptr;
                }
                emit logLine(QStringLiteral("[hongshi] 隧道已建立: %1").arg(addr));
                emit ready(addr);
            }
        }
    }
}

void HongshiClient::stop()
{
    m_ready = false;
    m_endpoint.clear();

    if (m_startTimeout) {
        m_startTimeout->stop();
        m_startTimeout->deleteLater();
        m_startTimeout = nullptr;
    }

    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply = nullptr;
    }
    m_downloading = false;

    if (m_process) {
        m_process->disconnect();
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(2000)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}
