#include "terracotta_client.h"

#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDateTime>
#include <QSharedPointer>

TerracottaClient::TerracottaClient(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &TerracottaClient::pollState);
}

TerracottaClient::~TerracottaClient()
{
    stop();
}

void TerracottaClient::setExecutable(const QString &exePath)
{
    m_exePath = exePath;
}

bool TerracottaClient::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString TerracottaClient::baseUrl() const
{
    return QString("http://127.0.0.1:%1").arg(m_port);
}

void TerracottaClient::start(const QString &playerName)
{
    // Already attached/ready (possibly to an external primary instance)?
    if (m_ready || isRunning()) {
        emit ready();
        return;
    }
    if (m_exePath.isEmpty() || !QFileInfo::exists(m_exePath)) {
        emit failed(QStringLiteral("找不到联机核心 terracotta.exe"));
        return;
    }

    m_playerName = playerName.isEmpty() ? QStringLiteral("Player") : playerName;
    m_lastIndex = -1000000;
    m_ready = false;
    m_port = -1;
    m_secondaryPort = -1;
    m_state.clear();

    // Port-transfer file: terracotta writes {"port": N} here when ready.
    // Mirror HMCL: use a fresh unique temp directory and a file named "http" so
    // a stale file from a previous run can never be mistaken for the new port.
    m_portFile.clear();
    QDir baseTemp(QDir::tempPath());
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString subdir = QString("kill-terracotta-%1").arg(stamp);
    if (baseTemp.mkpath(subdir)) {
        m_portFile = baseTemp.filePath(subdir + QStringLiteral("/http"));
    } else {
        m_portFile = baseTemp.filePath(
            QString("kill-terracotta-%1.json").arg(QCoreApplication::applicationPid()));
    }
    QFile::remove(m_portFile);

    m_process = new QProcess(this);
    m_process->setProgram(m_exePath);
    m_process->setArguments(QStringList{ "--hmcl", m_portFile });
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this]() {
        if (!m_process) return;
        const QByteArray out = m_process->readAll();
        if (out.isEmpty()) return;
        const QString text = QString::fromLocal8Bit(out);
        for (const QString &ln : text.split('\n', Qt::SkipEmptyParts)) {
            const QString trimmed = ln.trimmed();
            emit logLine(QString("[terracotta-stdout] %1").arg(trimmed));
            // Secondary mode: another (primary) Terracotta instance already owns the
            // global mutex and is listening on this port. The freshly spawned process
            // will hand off and exit; we must connect to the primary instead.
            static const QRegularExpression reSecondary(
                QStringLiteral("secondary mode,\\s*port\\s*=\\s*(\\d+)"));
            const QRegularExpressionMatch m = reSecondary.match(trimmed);
            if (m.hasMatch()) {
                const int existingPort = m.captured(1).toInt();
                if (existingPort > 0) m_secondaryPort = existingPort;
            }
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        emit logLine(QString("[terracotta] 进程已退出, code=%1").arg(code));

        // Drain any output still buffered at exit time and re-scan for the
        // "secondary mode, port = N" hint (it may only be delivered here if the
        // process exited before the readyRead slot could run).
        if (m_process) {
            const QByteArray tail = m_process->readAll();
            if (!tail.isEmpty()) {
                const QString text = QString::fromLocal8Bit(tail);
                for (const QString &ln : text.split('\n', Qt::SkipEmptyParts)) {
                    const QString trimmed = ln.trimmed();
                    if (trimmed.isEmpty()) continue;
                    emit logLine(QString("[terracotta-stdout] %1").arg(trimmed));
                    static const QRegularExpression reSecondary(
                        QStringLiteral("secondary mode,\\s*port\\s*=\\s*(\\d+)"));
                    const QRegularExpressionMatch m = reSecondary.match(trimmed);
                    if (m.hasMatch()) {
                        const int existingPort = m.captured(1).toInt();
                        if (existingPort > 0) m_secondaryPort = existingPort;
                    }
                }
            }
        }

        // If we are attached to an external (primary) instance, our own child
        // exiting is expected and must NOT tear down the active connection.
        if (m_ready && m_secondaryPort > 0) {
            return;
        }
        if (m_ready) {
            m_pollTimer->stop();
            m_ready = false;
            m_port = -1;
        }
    });
    emit logLine(QString("[terracotta] 启动: %1 --hmcl %2").arg(m_exePath, m_portFile));
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        emit failed(QStringLiteral("联机核心启动失败: %1").arg(m_process->errorString()));
        m_process->deleteLater();
        m_process = nullptr;
        return;
    }

    // Poll for the port-transfer file. Mirrors HMCL's TerracottaManager: the
    // freshly spawned process writes {"port": N} when it becomes the primary
    // instance. If another Terracotta instance already owns the global mutex,
    // the new process runs in "secondary mode" and exits immediately, in which
    // case we must attach to that existing (primary) instance.
    auto *deadline = new QTimer(this);
    deadline->setInterval(200);
    struct PollState {
        int elapsedMs = 0;
        qint64 deadSince = -1;
        bool done = false;
    };
    auto poll = QSharedPointer<PollState>::create();
    auto finishReady = [this, deadline, poll](int port) {
        if (poll->done) return;
        poll->done = true;
        deadline->stop();
        deadline->deleteLater();
        m_port = port;
        m_ready = true;
        if (QFileInfo::exists(m_portFile)) QFile::remove(m_portFile);
        emit logLine(QString("[terracotta] 已连接联机核心, port=%1").arg(port));
        m_pollTimer->start();
        emit ready();
        pollState();
    };
    connect(deadline, &QTimer::timeout, this, [this, deadline, poll, finishReady]() {
        if (poll->done) return;
        poll->elapsedMs += 200;

        // 1) Prefer the port advertised by an already-running primary instance.
        if (m_secondaryPort > 0) {
            finishReady(m_secondaryPort);
            return;
        }

        // 2) Otherwise try to read the port file our own process wrote.
        if (QFileInfo::exists(m_portFile)) {
            QFile f(m_portFile);
            if (f.open(QIODevice::ReadOnly)) {
                const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
                f.close();
                const int port = obj.value("port").toInt(-1);
                if (port > 0) {
                    finishReady(port);
                    return;
                }
            }
        }

        // 3) Track how long the process has been dead. HMCL waits 10s after exit
        //    before failing, giving a secondary instance time to hand off.
        const bool dead = (m_process == nullptr || m_process->state() == QProcess::NotRunning);
        if (dead) {
            if (poll->deadSince < 0) poll->deadSince = QDateTime::currentMSecsSinceEpoch();
            const qint64 deadMs = QDateTime::currentMSecsSinceEpoch() - poll->deadSince;
            if (deadMs >= 10000) {
                poll->done = true;
                deadline->stop();
                deadline->deleteLater();
                emit failed(QStringLiteral("联机核心未能在预期时间内就绪（请确认没有残留的 terracotta.exe 占用端口，或稍后重试）"));
            }
            return;
        }

        // Safety net: absolute upper bound even if the process stays alive.
        if (poll->elapsedMs > 20000) {
            poll->done = true;
            deadline->stop();
            deadline->deleteLater();
            emit failed(QStringLiteral("联机核心未能在预期时间内就绪"));
        }
    });
    deadline->start();
}

void TerracottaClient::stop()
{
    if (m_pollTimer) m_pollTimer->stop();
    m_ready = false;
    m_port = -1;
    m_secondaryPort = -1;
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

void TerracottaClient::pollState()
{
    if (!m_ready || m_port <= 0) return;
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/state")));
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logLine(QString("轮询 /state 失败: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray body = reply->readAll();
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.isEmpty()) {
            emit logLine(QString("轮询 /state 返回空/非JSON: %1").arg(QString::fromUtf8(body.left(200))));
            return;
        }
        const int index = obj.value("index").toInt(-1);
        const QString state = obj.value("state").toString();
        // Terracotta can keep the same index across waiting->scanning transitions,
        // so we must update when EITHER the state string changes or index advances.
        const bool changed = (state != m_state) || (index > m_lastIndex);
        if (!changed) return;
        emit logLine(QString("[terracotta] 状态: %1 (index=%2) %3")
                         .arg(state).arg(index)
                         .arg(QString::fromUtf8(body).left(300)));
        m_lastIndex = index;
        m_state = state;
        emit stateChanged(obj);
    });
}

void TerracottaClient::postAction(const QString &path, const QList<QPair<QString, QString>> &extraQuery)
{
    if (!m_ready || m_port <= 0) return;
    QUrl url(baseUrl() + path);
    QUrlQuery query;
    query.addQueryItem("player", m_playerName);
    for (const auto &kv : extraQuery) query.addQueryItem(kv.first, kv.second);
    for (const QString &node : m_nodes) query.addQueryItem("public_nodes", node);
    url.setQuery(query);
    QNetworkRequest req(url);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void TerracottaClient::setWaiting()
{
    if (!m_ready || m_port <= 0) return;
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/state/ide")));
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void TerracottaClient::host()
{
    postAction(QStringLiteral("/state/scanning"));
}

void TerracottaClient::guest(const QString &roomCode)
{
    postAction(QStringLiteral("/state/guesting"), { { "room", roomCode } });
}

void TerracottaClient::ensureNodes()
{
    if (m_nodesFetched) {
        if (!m_nodes.isEmpty()) emit nodesReady(m_nodes);
        return;
    }
    QNetworkRequest req(QUrl(QStringLiteral("https://terracotta.glavo.site/nodes")));
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_nodesFetched = true;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            for (const QJsonValue &v : arr) {
                const QString u = v.toObject().value("url").toString();
                if (!u.isEmpty()) m_nodes.append(u);
            }
        }
        emit nodesReady(m_nodes);
    });
}
