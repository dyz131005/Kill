#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QPointer>

class QProcess;
class QNetworkAccessManager;
class QNetworkReply;

class QTimer;
// Drives the external Hongshi (红石联机 / hongshi2) kernel process.
//
// Per the official API doc (https://hongshi.site/api.html) a launcher only has to:
//   1. download `hongshic` for the player's platform
//   2. pick a relay from GET /api/server/list
//   3. spawn `hongshic -t <relay> [-p <port>]` and read `endpoint=` from stdout
//   4. show that endpoint verbatim to the player
// When the process exits the room is over; relaunch for a new one.
class HongshiClient : public QObject
{
    Q_OBJECT

public:
    explicit HongshiClient(QObject *parent = nullptr);
    ~HongshiClient() override;

    // Directory the kernel binary lives in (config dir + "/hongshi").
    static QString defaultDir(const QString &configDir);
    static QString executableName();

    void setExecutable(const QString &exePath);
    QString executable() const { return m_exePath; }

    bool isRunning() const;
    bool isReady() const { return m_ready; }
    QString endpoint() const { return m_endpoint; }
    QString currentRelay() const { return m_relay; }

    // Downloads the kernel for the current platform if it is missing.
    // Emits downloadProgress()/downloadFinished() or failed().
    void ensureKernel(const QString &dir);

    // Fetch the relay list (GET /api/server/list). Emits relaysReady().
    void fetchRelays();

    // Spawn the kernel against `relay` and wait for `endpoint=` on stdout.
    // If `relay` is empty the first entry of the last fetched list is used.
    void start(const QString &relay = QString(), int gamePort = 25565);
    void stop();

signals:
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &exePath);
    // region -> relay address, exactly as returned by the API (keys are opaque).
    void relaysReady(const QList<QPair<QString, QString>> &relays);
    void ready(const QString &endpoint);
    void failed(const QString &error);
    // Room ended normally (exit code 0) or the kernel could not get a tunnel (1).
    void processExited(int code);
    void logLine(const QString &message);

private:
    void handleStdout(const QByteArray &chunk);
    void requestKernelDownload(const QUrl &url, const QString &destPath, int redirectsLeft = 5);

    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    QString m_exePath;
    QString m_relay;
    QString m_endpoint;
    QString m_stdoutBuffer;
    QTimer *m_startTimeout = nullptr;
    QList<QPair<QString, QString>> m_relays;
    bool m_ready = false;
    bool m_downloading = false;
};
