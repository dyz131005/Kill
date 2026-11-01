#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QPointer>

class QProcess;
class QNetworkAccessManager;
class QTimer;

// Drives the external Terracotta (陶瓦联机) core process over its local HTTP API.
// Mirrors HMCL's TerracottaManager:
//   - launches `terracotta.exe --hmcl <portFile>`
//   - the process writes {"port": N} to <portFile>; we read it to learn the port
//   - polls GET http://127.0.0.1:<port>/state for the state machine
//   - host: /state/ide then /state/scanning ; guest: /state/guesting
class TerracottaClient : public QObject
{
    Q_OBJECT

public:
    explicit TerracottaClient(QObject *parent = nullptr);
    ~TerracottaClient() override;

    // Absolute path to terracotta.exe (must already exist).
    void setExecutable(const QString &exePath);
    bool isRunning() const;
    // True once the core is launched/attached and the HTTP port is known.
    bool isReady() const { return m_ready && m_port > 0; }

    // Launch the core process and begin polling. `playerName` is sent to the
    // core so the room shows a name. Emits ready()/failed().
    void start(const QString &playerName);
    void stop();

    // Actions (valid only after ready()):
    void setWaiting();                                   // /state/ide
    void host();                                         // /state/scanning
    void guest(const QString &roomCode);                 // /state/guesting

    // Public relay nodes (https://terracotta.glavo.site/nodes). Fetched lazily.
    void ensureNodes();

    QString currentState() const { return m_state; }

signals:
    void ready();                                        // core launched, port known
    void failed(const QString &error);
    // Emitted whenever /state returns a (new) state object.
    // Fields: state, index, room (host-ok), url (guest-ok), difficulty, profiles.
    void stateChanged(const QJsonObject &stateObj);
    void nodesReady(const QStringList &nodeUrls);
    void logLine(const QString &message);

private slots:
    void pollState();

private:
    void postAction(const QString &path, const QList<QPair<QString, QString>> &extraQuery = {});
    QString baseUrl() const;

    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_pollTimer = nullptr;
    QString m_exePath;
    QString m_portFile;
    QString m_playerName;
    int m_port = -1;
    int m_lastIndex = -1000000;
    QString m_state;
    QStringList m_nodes;
    bool m_nodesFetched = false;
    bool m_ready = false;
    // Port advertised by an already-running (primary) Terracotta instance when the
    // newly spawned process reports "secondary mode, port = N". When set, we attach
    // to that existing instance instead of waiting for our own process to serve.
    int m_secondaryPort = -1;
};
