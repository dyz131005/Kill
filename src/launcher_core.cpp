#include "launcher_core.h"

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>
#include <QWidget>

#include "download_manager.h"
#include "download_progress_dialog.h"
#include "download_overlay.h"
#include "download_task_engine.h"
#include "zip_utils.h"

namespace {
QString osName()
{
    return "windows";
}

QString classpathSeparator()
{
    return ";";
}

QString sanitizeFileName(QString name)
{
    static const QString invalid = "\\/:*?\"<>|";
    for (QChar ch : invalid) {
        name.replace(ch, '_');
    }
    return name;
}

bool pumpDialogEvents(DownloadProgressDialog &dialog, QString *error)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (dialog.isCancelled()) {
        if (error) {
            *error = "操作已取消";
        }
        return false;
    }
    return true;
}

QString processErrorToString(QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart:
        return "启动失败";
    case QProcess::Crashed:
        return "进程崩溃";
    case QProcess::Timedout:
        return "操作超时";
    case QProcess::ReadError:
        return "读取进程输出失败";
    case QProcess::WriteError:
        return "写入进程输入失败";
    case QProcess::UnknownError:
    default:
        return "未知错误";
    }
}

QString win32ErrorMessage(unsigned long errorCode)
{
#ifdef Q_OS_WIN
    if (errorCode == 0) {
        return QString();
    }
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

QString ioContextForPath(const QString &path)
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

bool runProcessWithDialog(const QString &title,
                          const QString &description,
                          const QString &program,
                          const QStringList &arguments,
                          const QString &workingDirectory,
                          QString *error)
{
    DownloadProgressDialog dialog(title, description);
    dialog.setBusy(true);
    dialog.show();
    if (!pumpDialogEvents(dialog, error)) {
        return false;
    }

    QProcess process;
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        if (error) {
            *error = QString("无法启动进程: %1").arg(program);
        }
        dialog.close();
        return false;
    }

    QObject::connect(&dialog, &DownloadProgressDialog::cancelled, &process, [&process]() {
        process.kill();
    });

    while (process.state() != QProcess::NotRunning) {
        if (!pumpDialogEvents(dialog, error)) {
            process.kill();
            process.waitForFinished(3000);
            dialog.close();
            return false;
        }
        process.waitForFinished(100);
    }

    dialog.setBusy(false);
    dialog.setProgress(100, 100);
    dialog.close();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString output = QString::fromLocal8Bit(process.readAllStandardError() + process.readAllStandardOutput()).trimmed();
            *error = output.isEmpty() ? "外部进程执行失败" : output;
        }
        return false;
    }
    return true;
}
}

LauncherCore::LauncherCore(QObject *parent)
    : QObject(parent)
    , m_configStore(this)
    , m_networkManager(new QNetworkAccessManager(this))
{
    ensureDirectory(m_configStore.config().minecraftDir, nullptr);
    ensureDirectory(librariesRoot(), nullptr);
    ensureDirectory(assetsRoot(), nullptr);
    ensureDirectory(versionsRoot(), nullptr);
    ensureDirectory(instancesRoot(), nullptr);
    ensureDirectory(modpacksRoot(), nullptr);
}

LauncherCore::~LauncherCore() = default;

ConfigStore *LauncherCore::configStore()
{
    return &m_configStore;
}

const ConfigStore *LauncherCore::configStore() const
{
    return &m_configStore;
}

QString LauncherCore::appDataRoot() const
{
    return QFileInfo(m_configStore.configFilePath()).absolutePath();
}

QString LauncherCore::librariesRoot() const
{
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir) + "/libraries";
}

QString LauncherCore::assetsRoot() const
{
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir) + "/assets";
}

QString LauncherCore::versionsRoot() const
{
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir) + "/versions";
}

QString LauncherCore::instancesRoot() const
{
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir) + "/instances";
}

QString LauncherCore::modpacksRoot() const
{
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir) + "/modpacks";
}

void LauncherCore::log(const QString &message) const
{
    emit const_cast<LauncherCore *>(this)->logMessage(message);
}

void LauncherCore::logTlsEnvironment() const
{
    log(QStringLiteral("===== TLS 环境诊断开始 ====="));
    log(QStringLiteral("[TLS] 运行时后端: %1")
            .arg(QSslSocket::activeBackend().isEmpty() ? QStringLiteral("(空)") : QSslSocket::activeBackend()));
    log(QStringLiteral("[TLS] 可用后端: [%1]")
            .arg(QSslSocket::availableBackends().join(QStringLiteral(", "))));
    log(QStringLiteral("[TLS] supportsSsl: %1")
            .arg(QSslSocket::supportsSsl() ? QStringLiteral("true") : QStringLiteral("false")));
    log(QStringLiteral("[TLS] 编译期库版本: %1").arg(QSslSocket::sslLibraryBuildVersionString()));
    log(QStringLiteral("[TLS] 运行时库版本: %1")
            .arg(QSslSocket::sslLibraryVersionString().isEmpty()
                     ? QStringLiteral("(空)")
                     : QSslSocket::sslLibraryVersionString()));
    log(QStringLiteral("[TLS] 系统 CA 证书数量: %1")
            .arg(QSslConfiguration::defaultConfiguration().caCertificates().size()));

    // 实际建立一次 TLS 连接，暴露握手阶段的真实错误（TLS 自检）
    const QString host = QStringLiteral("bmclapi2.bangbang93.com");
    const quint16 port = 443;
    log(QStringLiteral("[TLS] 开始握手测试: %1:%2").arg(host).arg(port));

    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket.connectToHostEncrypted(host, port);
    if (!socket.waitForConnected(10000)) {
        log(QStringLiteral("[TLS] TCP 连接失败: %1").arg(socket.errorString()));
        log(QStringLiteral("===== TLS 环境诊断结束 ====="));
        return;
    }
    log(QStringLiteral("[TLS] TCP 已连接，等待握手..."));

    if (!socket.waitForEncrypted(15000)) {
        log(QStringLiteral("[TLS] 握手失败: %1 (code=%2)")
                .arg(socket.errorString())
                .arg(static_cast<int>(socket.error())));
        const QList<QSslError> errs = socket.sslHandshakeErrors();
        log(QStringLiteral("[TLS] 握手错误数量: %1").arg(errs.size()));
        for (const QSslError &e : errs) {
            log(QStringLiteral("[TLS]   错误[%1]: %2")
                    .arg(static_cast<int>(e.error()))
                    .arg(e.errorString()));
        }
        log(QStringLiteral("[TLS] 对端证书: %1")
                .arg(socket.peerCertificate().isNull()
                         ? QStringLiteral("(无)")
                         : socket.peerCertificate().subjectDisplayName()));
        socket.abort();
        log(QStringLiteral("===== TLS 环境诊断结束 ====="));
        return;
    }

    log(QStringLiteral("[TLS] 握手成功"));
    QString protocolName;
    switch (socket.sessionProtocol()) {
    case QSsl::TlsV1_3:
        protocolName = QStringLiteral("TLS 1.3");
        break;
    case QSsl::TlsV1_2:
        protocolName = QStringLiteral("TLS 1.2");
        break;
    case QSsl::TlsV1_1:
        protocolName = QStringLiteral("TLS 1.1");
        break;
    case QSsl::TlsV1_0:
        protocolName = QStringLiteral("TLS 1.0");
        break;
    default:
        protocolName = QStringLiteral("其他(%1)").arg(static_cast<int>(socket.sessionProtocol()));
        break;
    }
    log(QStringLiteral("[TLS] 协议版本: %1").arg(protocolName));
    log(QStringLiteral("[TLS] 加密套件: %1").arg(socket.sessionCipher().name()));
    log(QStringLiteral("[TLS] 对端证书: %1")
            .arg(socket.peerCertificate().subjectDisplayName()));
    socket.disconnectFromHost();
    log(QStringLiteral("===== TLS 环境诊断结束 ====="));
}


void LauncherCore::flushProcessLogBuffer(const QString &source,
                                         QByteArray &buffer,
                                         const QByteArray &chunk,
                                         bool flushRemaining) const
{
    buffer.append(chunk);
    int newlineIndex = buffer.indexOf('\n');
    while (newlineIndex >= 0) {
        QByteArray line = buffer.left(newlineIndex);
        buffer.remove(0, newlineIndex + 1);
        line.replace('\r', "");
        const QString text = QString::fromLocal8Bit(line).trimmed();
        if (!text.isEmpty()) {
            log(QString("[%1] %2").arg(source, text));
        }
        newlineIndex = buffer.indexOf('\n');
    }

    if (flushRemaining && !buffer.trimmed().isEmpty()) {
        QByteArray remaining = buffer;
        remaining.replace('\r', "");
        const QString text = QString::fromLocal8Bit(remaining).trimmed();
        if (!text.isEmpty()) {
            log(QString("[%1] %2").arg(source, text));
        }
        buffer.clear();
    }
}

static QString normalizedMirrorMode(const QString &mode)
{
    const QString s = mode.trimmed().toLower();
    if (s == "official") return "official";
    if (s == "mirror") return "mirror";
    return "auto";
}

static bool shouldUseMirrorForMode(const QString &mode)
{
    const QString m = normalizedMirrorMode(mode);
    if (m == "official") return false;
    return true;
}

QString LauncherCore::mirrorMetaBase() const
{
    return shouldUseMirrorForMode(m_configStore.config().mirrorMode)
        ? "https://bmclapi2.bangbang93.com"
        : "https://piston-meta.mojang.com";
}

QString LauncherCore::mirrorDataBase() const
{
    return shouldUseMirrorForMode(m_configStore.config().mirrorMode)
        ? "https://bmclapi2.bangbang93.com"
        : "https://piston-data.mojang.com";
}

QString LauncherCore::rewriteUrl(const QString &original) const
{
    QString url = original;
    if (!shouldUseMirrorForMode(m_configStore.config().mirrorMode)) {
        return url;
    }

    url.replace("https://piston-meta.mojang.com", "https://bmclapi2.bangbang93.com");
    url.replace("https://launchermeta.mojang.com", "https://bmclapi2.bangbang93.com");
    url.replace("https://piston-data.mojang.com", "https://bmclapi2.bangbang93.com");
    url.replace("https://launcher.mojang.com", "https://bmclapi2.bangbang93.com");
    url.replace("https://libraries.minecraft.net", "https://bmclapi2.bangbang93.com/maven");
    url.replace("https://resources.download.minecraft.net", "https://bmclapi2.bangbang93.com/assets");

    url.replace("https://meta.fabricmc.net", "https://bmclapi2.bangbang93.com/fabric-meta");
    url.replace("https://maven.fabricmc.net", "https://bmclapi2.bangbang93.com/maven");

    url.replace("https://meta.quiltmc.org", "https://bmclapi2.bangbang93.com/quilt-meta");
    url.replace("https://maven.quiltmc.org", "https://bmclapi2.bangbang93.com/maven");

    url.replace("https://files.minecraftforge.net/maven", "https://bmclapi2.bangbang93.com/maven");
    url.replace("https://maven.minecraftforge.net", "https://bmclapi2.bangbang93.com/maven");

    url.replace("https://maven.neoforged.net", "https://bmclapi2.bangbang93.com/maven");

    url.replace("https://bmclapi2.bangbang93.com/fabric-meta", "https://bmclapi2.bangbang93.com/fabric-meta");

    {
        const QString modrinthApiPrefix = QStringLiteral("https://api.modrinth.com");
        if (url.startsWith(modrinthApiPrefix)) {
            url = QStringLiteral("https://mod.mcimirror.top/modrinth") + url.mid(modrinthApiPrefix.size());
        }
    }
    url.replace("https://cdn.modrinth.com", "https://mod.mcimirror.top");

    {
        const QString curseApiPrefix = QStringLiteral("https://api.curseforge.com");
        if (url.startsWith(curseApiPrefix)) {
            url = QStringLiteral("https://mod.mcimirror.top/curseforge") + url.mid(curseApiPrefix.size());
        }
    }
    url.replace("https://edge.forgecdn.net", "https://mod.mcimirror.top");

    return url;
}

bool LauncherCore::shouldVerify() const
{
    return m_configStore.config().verifyFiles;
}

QJsonDocument LauncherCore::getJson(const QUrl &url, QString *error)
{
    const QByteArray bytes = getBytes(rewriteUrl(url.toString()), error);
    if (!error->isEmpty()) {
        log(QString("请求 JSON 失败: %1").arg(*error));
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QString("JSON 解析失败: %1").arg(parseError.errorString());
        log(QString("JSON 解析失败: %1 -> %2").arg(url.toString(), parseError.errorString()));
        return {};
    }
    log(QString("请求 JSON 成功: %1").arg(url.toString()));
    return doc;
}

QByteArray LauncherCore::getBytes(const QUrl &url,
                                  QString *error,
                                  const QList<QPair<QString, QString>> &headers)
{
    if (error) {
        error->clear();
    }

    // 首次网络请求时输出一次完整 TLS 环境诊断，便于排查 https 失败
    static bool tlsEnvLogged = false;
    if (!tlsEnvLogged) {
        tlsEnvLogged = true;
        logTlsEnvironment();
    }

    const QString finalUrl = rewriteUrl(url.toString());
    log(QString("HTTP GET: %1").arg(finalUrl));
    QNetworkRequest request{QUrl(finalUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "KillLauncher/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &header : headers) {
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
    }

    QNetworkReply *reply = m_networkManager->get(request);
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, reply, [&timedOut, reply]() {
        if (reply && reply->isRunning()) {
            timedOut = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &timeoutTimer, &QTimer::stop);
    QObject::connect(reply, &QNetworkReply::downloadProgress, &timeoutTimer, [&timeoutTimer](qint64, qint64) {
        if (timeoutTimer.isActive()) {
            timeoutTimer.start(120000);
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(120000);
    loop.exec();

    const QByteArray body = reply->readAll();
    const auto netErr = reply->error();
    if (netErr != QNetworkReply::NoError) {
        QString errMsg;
        const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        log(QStringLiteral("HTTP 诊断: url=%1").arg(url.toString()));
        log(QStringLiteral("HTTP 诊断: error=%1 (%2)").arg(static_cast<int>(netErr)).arg(reply->errorString()));
        log(QStringLiteral("HTTP 诊断: httpCode=%1").arg(httpCode));
        log(QStringLiteral("HTTP 诊断: timedOut=%1").arg(timedOut ? QStringLiteral("是") : QStringLiteral("否")));
        log(QStringLiteral("HTTP 诊断: 当前 TLS 后端=%1, supportsSsl=%2")
                .arg(QSslSocket::activeBackend().isEmpty() ? QStringLiteral("(空)") : QSslSocket::activeBackend())
                .arg(QSslSocket::supportsSsl() ? QStringLiteral("是") : QStringLiteral("否")));

        if (timedOut) {
            errMsg = QString("HTTP 超时(120s无活动): %1").arg(url.toString());
        } else if (netErr == QNetworkReply::SslHandshakeFailedError) {
            errMsg = QString("%1 -> SSL 握手失败（后端=%2, supportsSsl=%3）: %4")
                         .arg(url.toString())
                         .arg(QSslSocket::activeBackend())
                         .arg(QSslSocket::supportsSsl() ? QStringLiteral("是") : QStringLiteral("否"))
                         .arg(reply->errorString());
        } else if (netErr == QNetworkReply::ProtocolUnknownError) {
            errMsg = QString("%1 -> 协议不支持（后端=%2）: %3")
                         .arg(url.toString())
                         .arg(QSslSocket::activeBackend().isEmpty() ? QStringLiteral("(无可用后端)") : QSslSocket::activeBackend())
                         .arg(reply->errorString());
        } else if (httpCode > 0) {
            errMsg = QString("%1 -> HTTP %2: %3").arg(url.toString()).arg(httpCode).arg(reply->errorString());
        } else {
            errMsg = QString("%1 -> %2 (code=%3)")
                         .arg(url.toString())
                         .arg(reply->errorString())
                         .arg(static_cast<int>(netErr));
        }
        if (error) {
            *error = errMsg;
        }
        log(QString("HTTP 请求失败: %1").arg(errMsg));
        reply->deleteLater();
        return {};
    }
    log(QString("HTTP 请求成功: %1 (%2 字节)").arg(url.toString()).arg(body.size()));
    reply->deleteLater();
    return body;
}

bool LauncherCore::downloadToFile(const QUrl &url,
                                  const QString &filePath,
                                  QString *error,
                                  const QString &expectedSha1,
                                  const QString &expectedSha256,
                                  const QList<QPair<QString, QString>> &headers)
{
    const QString nativePath = QDir::toNativeSeparators(filePath);
    bool existsAndValid = QFileInfo::exists(filePath);
    if (existsAndValid && !expectedSha1.isEmpty() && shouldVerify()) {
        existsAndValid = verifySha1(filePath, expectedSha1);
    }
    if (existsAndValid && !expectedSha256.isEmpty() && shouldVerify()) {
        existsAndValid = verifySha256(filePath, expectedSha256);
    }
    if (existsAndValid) {
        log(QString("跳过下载，文件已存在: %1").arg(nativePath));
        return true;
    }

    log(QString("开始下载文件: %1 <- %2").arg(nativePath, rewriteUrl(url.toString())));
    const QByteArray bytes = getBytes(url, error, headers);
    if (!error->isEmpty()) {
        log(QString("下载失败: %1 -> %2").arg(nativePath, *error));
        return false;
    }

    if (shouldVerify()) {
        if (!expectedSha1.isEmpty()) {
            const QString digest = sha1ForBytes(bytes);
            if (digest.compare(expectedSha1, Qt::CaseInsensitive) != 0) {
                *error = QString("SHA1 不匹配: %1").arg(url.toString());
                log(QString("下载校验失败: %1 -> 期望 SHA1 %2, 实际 %3").arg(nativePath, expectedSha1, digest));
                return false;
            }
        }
        if (!expectedSha256.isEmpty()) {
            const QString digest = sha256ForBytes(bytes);
            if (digest.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
                *error = QString("SHA256 不匹配: %1").arg(url.toString());
                log(QString("下载校验失败: %1 -> 期望 SHA256 %2, 实际 %3").arg(nativePath, expectedSha256, digest));
                return false;
            }
        }
    }

    ensureDirectory(QFileInfo(filePath).absolutePath(), error);
    if (!error->isEmpty()) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
#ifdef Q_OS_WIN
        const unsigned long lastErr = GetLastError();
#else
        const unsigned long lastErr = 0;
#endif
        const QString native = QDir::toNativeSeparators(filePath);
        QString errMsg = QString("无法写入文件: %1").arg(native);
        errMsg += QString(" | IO上下文: %1").arg(ioContextForPath(filePath));
        if (lastErr != 0) {
            const QString desc = win32ErrorMessage(lastErr);
            errMsg += QString(" | Win32错误码=%1 (0x%2), 描述: %3")
                          .arg(lastErr)
                          .arg(lastErr, 0, 16)
                          .arg(desc.isEmpty() ? QString("(无系统描述)") : desc);
        }
        errMsg += QString(" | QFile.error=%2, QFile.errorString=%3")
                      .arg(file.error())
                      .arg(file.errorString());
        *error = errMsg;
        log(QString("文件写入失败: %1").arg(errMsg));
        return false;
    }
    file.write(bytes);
    file.close();
    log(QString("下载完成: %1 (%2 字节)").arg(nativePath).arg(bytes.size()));
    return true;
}

bool LauncherCore::downloadToFileWithProgress(const QUrl &url,
                                              const QString &filePath,
                                              const QString &fileDescription,
                                              QString *error,
                                              const QString &expectedSha1,
                                              const QString &expectedSha256,
                                              const QList<QPair<QString, QString>> &headers)
{
    const QString nativePath = QDir::toNativeSeparators(filePath);
    bool existsAndValid = QFileInfo::exists(filePath);
    if (existsAndValid && !expectedSha1.isEmpty() && shouldVerify()) {
        existsAndValid = verifySha1(filePath, expectedSha1);
    }
    if (existsAndValid && !expectedSha256.isEmpty() && shouldVerify()) {
        existsAndValid = verifySha256(filePath, expectedSha256);
    }
    if (existsAndValid) {
        log(QString("跳过下载，文件已存在: %1").arg(nativePath));
        emit progressStarted(QString("已存在: %1").arg(fileDescription));
        emit progressUpdated(100, 100, QString("已存在: %1").arg(fileDescription));
        emit progressFinished(QString("已存在: %1").arg(fileDescription));
        return true;
    }

    log(QString("开始下载: %1 <- %2").arg(nativePath, rewriteUrl(url.toString())));
    emit progressStarted(QString("开始下载: %1").arg(fileDescription));
    DownloadManager manager(m_networkManager, this);
    const QString checksumForDownload =
        shouldVerify()
            ? (!expectedSha256.isEmpty() ? expectedSha256 : expectedSha1)
            : QString();
    const bool ok = manager.downloadFile(QUrl(rewriteUrl(url.toString())),
                                         filePath,
                                         "下载文件",
                                         fileDescription,
                                         error,
                                         nullptr,
                                         checksumForDownload,
                                         headers);
    if (!ok) {
        log(QString("下载失败: %1 -> %2").arg(nativePath, error ? *error : QString("未知错误")));
        return false;
    }

    log(QString("下载完成: %1").arg(nativePath));
    emit progressUpdated(100, 100, QString("完成: %1").arg(fileDescription));
    emit progressFinished(QString("完成: %1").arg(fileDescription));
    return true;
}

bool LauncherCore::ensureDirectory(const QString &path, QString *error) const
{
    if (path.isEmpty()) {
        return true;
    }

#ifdef Q_OS_WIN
    QString native = QDir::toNativeSeparators(QDir::fromNativeSeparators(path));
    const QStringList parts = native.split('\\', Qt::SkipEmptyParts);

    QStringList segments;
    QString accum;
    for (const QString &p : parts) {
        if (accum.isEmpty()) {
            if (p.endsWith(':')) accum = p + "\\";
            else accum = p;
        } else {
            accum += "\\" + p;
        }
        segments.append(accum);
    }

    QString diagnose;
    DWORD firstCreateFail = 0;
    QString firstCreateFailSegment;
    bool allOk = true;

    for (const QString &seg : segments) {
        const std::wstring wseg = seg.toStdWString();
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        const BOOL attrOk = GetFileAttributesExW(wseg.c_str(), GetFileExInfoStandard, &fad);
        if (attrOk && (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            diagnose += QString("\n    ✅ [%1] 存在, DIR").arg(seg);
            continue;
        }
        if (attrOk && !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            diagnose += QString("\n    ❌ [%1] 已存在但不是目录（同名文件占位）").arg(seg);
            allOk = false;
            break;
        }

        const BOOL cdOk = CreateDirectoryW(wseg.c_str(), nullptr);
        const DWORD cdErr = cdOk ? 0 : GetLastError();
        if (cdOk) {
            diagnose += QString("\n    ✅ [%1] CreateDirectoryW 成功").arg(seg);
            continue;
        }
        if (cdErr == ERROR_ALREADY_EXISTS) {
            diagnose += QString("\n    ✅ [%1] CreateDirectoryW 报告已存在（并发竞态）").arg(seg);
            continue;
        }
        diagnose += QString("\n    ❌ [%1] CreateDirectoryW 失败: err=%2 (0x%3) %4")
                       .arg(seg)
                       .arg(cdErr)
                       .arg(cdErr, 0, 16)
                       .arg(win32ErrorMessage(cdErr));
        if (firstCreateFail == 0) {
            firstCreateFail = cdErr;
            firstCreateFailSegment = seg;
        }
        allOk = false;
        break;
    }

    if (allOk) {
        return true;
    }

    QString errMsg = QString("无法创建目录: %1").arg(native);
    errMsg += QString(" | IO上下文: %1").arg(ioContextForPath(path));
    if (firstCreateFail != 0) {
        errMsg += QString(" | 首次失败段=\"%1\" | Win32错误码=%2 (0x%3), 描述: %4")
                      .arg(firstCreateFailSegment)
                      .arg(firstCreateFail)
                      .arg(firstCreateFail, 0, 16)
                      .arg(win32ErrorMessage(firstCreateFail).isEmpty()
                               ? QString("(无系统描述)")
                               : win32ErrorMessage(firstCreateFail));
    }
    errMsg += QString("\n  🧭 目录逐级诊断 (CreateDirectoryW): %1").arg(diagnose);

    if (firstCreateFail == ERROR_ACCESS_DENIED || firstCreateFail == 2) {
        const QString parentDir = QFileInfo(native).absolutePath();
        const QString parentHint = parentDir.startsWith("C:\\Users\\", Qt::CaseInsensitive) ||
                                   parentDir.startsWith("C:\\Test\\", Qt::CaseInsensitive)
            ? QString("⚠️  检测到目标位于 C 盘（%1），PowerShell/资源管理器可能能正常创建，但本进程（Kill.exe）被 Windows 系统策略单独拦截。通常这是 Windows Defender 攻击面减少(ASR)/ Exploit Protection/ 受控文件夹访问或残留的 Minifilter 驱动做的进程级拦截（不是 NTFS ACL 问题）。")
                  .arg(parentDir.left(30) + "...")
            : QString();
        const QString fallbackHint = QString("💡 建议：在【设置】里将游戏目录/库根目录改到 D 盘（例如程序同目录 D:\\Projects\\Kill\\dist\\Kill\\libraries），已验证程序目录下 CreateFileW/CreateDirectoryW 全部能成功。");
        if (!parentHint.isEmpty()) errMsg += "\n  " + parentHint;
        errMsg += "\n  " + fallbackHint;
    }

    log(errMsg);
    if (error) {
        *error = errMsg;
    }
    return false;
#else
    if (QDir().mkpath(path)) {
        return true;
    }
    const QString native = QDir::toNativeSeparators(path);
    QString errMsg = QString("无法创建目录: %1").arg(native);
    errMsg += QString(" | IO上下文: %1").arg(ioContextForPath(path));
    log(errMsg);
    if (error) {
        *error = errMsg;
    }
    return false;
#endif
}

bool LauncherCore::verifySha1(const QString &filePath, const QString &expected)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file)) {
        file.close();
        return false;
    }
    file.close();
    const QString digest = QString::fromLatin1(hash.result().toHex());
    return digest.compare(expected, Qt::CaseInsensitive) == 0;
}

bool LauncherCore::verifySha256(const QString &filePath, const QString &expected)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        file.close();
        return false;
    }
    file.close();
    const QString digest = QString::fromLatin1(hash.result().toHex());
    return digest.compare(expected, Qt::CaseInsensitive) == 0;
}

QString LauncherCore::sha1ForBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

QString LauncherCore::sha256ForBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject LauncherCore::findVersionMetadata(const QString &versionId, QString *error)
{
    QJsonArray versions = fetchVersionManifest(error);
    if (!error->isEmpty()) {
        return {};
    }
    for (const QJsonValue &value : versions) {
        const QJsonObject object = value.toObject();
        if (object.value("id").toString() == versionId) {
            return object;
        }
    }
    *error = QString("未找到版本: %1").arg(versionId);
    return {};
}

QJsonArray LauncherCore::fetchVersionManifest(QString *error)
{
    const QString url = mirrorMetaBase() + "/mc/game/version_manifest_v2.json";
    log(QString("获取版本清单: %1").arg(url));
    const QJsonDocument doc = getJson(QUrl(url), error);
    return doc.object().value("versions").toArray();
}

QJsonArray LauncherCore::filterVersionsByType(const QJsonArray &versions, const QString &type) const
{
    if (type == "all") {
        return versions;
    }

    QJsonArray result;
    for (const QJsonValue &value : versions) {
        const QJsonObject object = value.toObject();
        const QString versionType = object.value("type").toString();
        const QString versionId = object.value("id").toString();

        if (type == "release" && versionType == "release") {
            result.append(value);
        } else if (type == "snapshot" && (versionType == "snapshot" || versionType == "pending")) {
            result.append(value);
        } else if (type == "old_beta" && versionType == "old_beta") {
            result.append(value);
        } else if (type == "old_alpha" && versionType == "old_alpha") {
            result.append(value);
        } else if (type == "april_fools") {
            if (versionId.contains("15w14a") || 
                versionId.contains("16w14a") || 
                versionId.contains("1.14.3-pre3") ||
                versionId.contains("20w14infinite") ||
                versionId.contains("21w13a") ||
                versionId.contains("22w13oneblockatatime") ||
                versionId.contains("23w13a_or_b") ||
                versionId.contains("24w13a") ||
                versionId.contains("3D Shareware v1.34") ||
                versionId.contains("Snapshot 20w14infinite")) {
                result.append(value);
            }
        }
    }
    return result;
}

QStringList LauncherCore::listInstalledVersions() const
{
    QStringList result;
    const QDir root(versionsRoot());
    for (const QFileInfo &entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString jsonFile = entry.absoluteFilePath() + "/" + entry.fileName() + ".json";
        if (QFileInfo::exists(jsonFile)) {
            result.append(entry.fileName());
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

LauncherVersionProfile LauncherCore::effectiveVersionProfile(const QString &versionId) const
{
    const LauncherConfig &config = m_configStore.config();
    LauncherVersionProfile profile;
    profile.versionIsolation = config.versionIsolation;
    profile.javaPath = config.javaPath;
    profile.javaArgs = config.javaArgs;
    profile.extraGameArgs = config.extraGameArgs;
    profile.minMemoryMb = config.minMemoryMb;
    profile.maxMemoryMb = config.maxMemoryMb;
    profile.softwareOpenGL = config.softwareOpenGL ? 1 : 0;

    const auto it = config.versionProfiles.constFind(versionId);
    if (it != config.versionProfiles.constEnd()) {
        const LauncherVersionProfile &stored = it.value();
        profile.versionIsolation = stored.versionIsolation;
        profile.javaPath = stored.javaPath.trimmed();
        profile.javaArgs = stored.javaArgs.trimmed();
        profile.extraGameArgs = stored.extraGameArgs.trimmed();
        if (stored.minMemoryMb > 0) {
            profile.minMemoryMb = stored.minMemoryMb;
        }
        if (stored.maxMemoryMb > 0) {
            profile.maxMemoryMb = stored.maxMemoryMb;
        }
        if (stored.softwareOpenGL != -1) {
            profile.softwareOpenGL = stored.softwareOpenGL;
        }
    }

    profile.minMemoryMb = std::max(512, profile.minMemoryMb);
    profile.maxMemoryMb = std::max(profile.minMemoryMb, profile.maxMemoryMb);
    return profile;
}

bool LauncherCore::ensureVersionProfileStored(const QString &versionId, QString *error)
{
    if (versionId.trimmed().isEmpty()) {
        if (error) {
            *error = "版本 ID 不能为空";
        }
        return false;
    }

    LauncherConfig &config = m_configStore.config();
    if (!config.versionProfiles.contains(versionId)) {
        config.versionProfiles.insert(versionId, effectiveVersionProfile(versionId));
        return saveConfig(error);
    }
    return true;
}

bool LauncherCore::saveVersionProfile(const QString &versionId, const LauncherVersionProfile &profile, QString *error)
{
    if (versionId.trimmed().isEmpty()) {
        if (error) {
            *error = "版本 ID 不能为空";
        }
        return false;
    }

    LauncherVersionProfile normalized = profile;
    normalized.javaPath = normalized.javaPath.trimmed();
    normalized.javaArgs = normalized.javaArgs.trimmed();
    normalized.extraGameArgs = normalized.extraGameArgs.trimmed();
    normalized.minMemoryMb = std::max(512, normalized.minMemoryMb);
    normalized.maxMemoryMb = std::max(normalized.minMemoryMb, normalized.maxMemoryMb);

    LauncherConfig &config = m_configStore.config();
    config.versionProfiles.insert(versionId, normalized);
    return saveConfig(error);
}

QString LauncherCore::versionJsonPath(const QString &baseVersionId, const QString &jsonName) const
{
    const QString dir = versionsRoot() + "/" + baseVersionId;
    if (jsonName.isEmpty()) {
        return dir + "/" + baseVersionId + ".json";
    }
    const QString name = jsonName.endsWith(".json", Qt::CaseInsensitive) ? jsonName : (jsonName + ".json");
    return dir + "/" + name;
}

QJsonObject LauncherCore::loadVersionJson(const QString &versionId, bool resolve, QString *error) const
{
    return loadVersionJsonWithOverride(versionId, {}, resolve, error);
}

QJsonObject LauncherCore::loadVersionJsonWithOverride(const QString &baseVersionId, const QString &loaderJsonName, bool resolve, QString *error) const
{
    const QString versionFile = loaderJsonName.isEmpty()
                                   ? versionJsonPath(baseVersionId)
                                   : versionJsonPath(baseVersionId, loaderJsonName);
    QFile file(versionFile);
    if (!file.open(QIODevice::ReadOnly)) {
        if (!loaderJsonName.isEmpty()) {
            const QString fallback = versionJsonPath(baseVersionId);
            QFile fb(fallback);
            if (fb.open(QIODevice::ReadOnly)) {
                QJsonObject fallbackJson = QJsonDocument::fromJson(fb.readAll()).object();
                if (resolve) {
                    QString resolveError;
                    if (!resolveInheritance(fallbackJson, &resolveError)) {
                        if (error) *error = resolveError;
                        return {};
                    }
                }
                if (error) error->clear();
                return fallbackJson;
            }
        }
        if (error) {
            *error = QString("无法读取版本信息: %1 (%2)").arg(baseVersionId, loaderJsonName.isEmpty() ? QStringLiteral("vanilla") : loaderJsonName);
        }
        return {};
    }

    QJsonObject versionJson = QJsonDocument::fromJson(file.readAll()).object();
    if (resolve) {
        QString resolveError;
        if (!resolveInheritance(versionJson, &resolveError)) {
            if (error) *error = resolveError;
            return {};
        }
    }
    if (error) error->clear();
    return versionJson;
}

QString LauncherCore::detectLoaderType(const QString &versionId, const QJsonObject &versionJson) const
{
    const QString loweredId = versionId.toLower();
    const QString mainClass = versionJson.value("mainClass").toString().toLower();
    if (loweredId.contains("neoforge") || mainClass.contains("neoforge")) {
        return "neoforge";
    }
    if (loweredId.contains("fabric") || mainClass.contains("fabricmc")) {
        return "fabric";
    }
    if (loweredId.contains("quilt") || mainClass.contains("quiltmc")) {
        return "quilt";
    }
    if (loweredId.contains("forge") || mainClass.contains("minecraftforge")) {
        return "forge";
    }

    const QJsonArray libraries = versionJson.value("libraries").toArray();
    for (const QJsonValue &value : libraries) {
        const QString libraryName = value.toObject().value("name").toString().toLower();
        if (libraryName.contains("net.neoforged:neoforge")) {
            return "neoforge";
        }
        if (libraryName.contains("net.fabricmc:fabric-loader")) {
            return "fabric";
        }
        if (libraryName.contains("org.quiltmc:quilt-loader")) {
            return "quilt";
        }
        if (libraryName.contains("net.minecraftforge:forge")) {
            return "forge";
        }
    }
    return "vanilla";
}

QString LauncherCore::versionBaseGameVersion(const QString &versionId) const
{
    QString error;
    QJsonObject versionJson = loadVersionJson(versionId, false, &error);
    if (versionJson.isEmpty()) {
        return versionId;
    }

    QString currentVersion = versionId;
    while (versionJson.contains("inheritsFrom")) {
        const QString parentId = versionJson.value("inheritsFrom").toString();
        if (parentId.isEmpty() || parentId == currentVersion) {
            break;
        }
        currentVersion = parentId;
        versionJson = loadVersionJson(currentVersion, false, &error);
        if (versionJson.isEmpty()) {
            break;
        }
    }
    return currentVersion;
}

bool LauncherCore::versionMatchesBase(const QString &candidateVersionId, const QString &baseVersionId) const
{
    return versionBaseGameVersion(candidateVersionId) == baseVersionId;
}

QString LauncherCore::versionInstanceDirectory(const QString &versionId) const
{
    return instanceDirectoryForVersion(versionId);
}

QString LauncherCore::versionModsDirectory(const QString &versionId) const
{
    const LauncherVersionProfile profile = effectiveVersionProfile(versionId);
    if (profile.versionIsolation && !versionId.isEmpty()) {
        return instancesRoot() + "/" + versionId + "/mods";
    }
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir) + "/mods";
}

QString LauncherCore::versionWorldsDirectory(const QString &versionId) const
{
    return instanceDirectoryForVersion(versionId) + "/saves";
}

QJsonArray LauncherCore::listVersionMods(const QString &versionId) const
{
    QJsonArray result;
    const QDir modsDir(versionModsDirectory(versionId));
    const QFileInfoList files = modsDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files) {
        const QString fileName = info.fileName();
        const bool disabled = fileName.endsWith(".disabled", Qt::CaseInsensitive);
        QString displayName = fileName;
        if (disabled) {
            displayName.chop(QString(".disabled").size());
        }
        result.append(QJsonObject{
            {"fileName", fileName},
            {"displayName", displayName},
            {"enabled", !disabled},
            {"scope", effectiveVersionProfile(versionId).versionIsolation ? "version" : "global"},
            {"path", QDir::toNativeSeparators(info.absoluteFilePath())},
            {"modified", info.lastModified().toString(Qt::ISODate)}
        });
    }
    return result;
}

bool LauncherCore::setModEnabled(const QString &versionId, const QString &fileName, bool enabled, QString *error)
{
    const QString directory = versionModsDirectory(versionId);
    const QString sourcePath = directory + "/" + fileName;
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        if (error) {
            *error = "模组文件不存在";
        }
        return false;
    }

    QString targetName = fileName;
    if (enabled && targetName.endsWith(".disabled", Qt::CaseInsensitive)) {
        targetName.chop(QString(".disabled").size());
    } else if (!enabled && !targetName.endsWith(".disabled", Qt::CaseInsensitive)) {
        targetName += ".disabled";
    } else {
        if (error) {
            error->clear();
        }
        return true;
    }

    const QString targetPath = directory + "/" + targetName;
    if (QFileInfo::exists(targetPath)) {
        if (error) {
            *error = "目标文件已存在，请先手动处理重名文件";
        }
        return false;
    }
    if (!QFile::rename(sourcePath, targetPath)) {
        if (error) {
            *error = "模组状态切换失败";
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool LauncherCore::removeModFile(const QString &versionId, const QString &fileName, QString *error)
{
    const QString filePath = versionModsDirectory(versionId) + "/" + fileName;
    if (!QFileInfo::exists(filePath)) {
        if (error) {
            *error = "模组文件不存在";
        }
        return false;
    }
    if (!QFile::remove(filePath)) {
        if (error) {
            *error = "删除模组失败";
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QString LauncherCore::versionResourcepacksDirectory(const QString &versionId) const
{
    return instanceDirectoryForVersion(versionId) + "/resourcepacks";
}

QJsonArray LauncherCore::listVersionResourcepacks(const QString &versionId) const
{
    QJsonArray result;
    const QDir dir(versionResourcepacksDirectory(versionId));
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << ".zip" << ".jar",
        QDir::Files,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files) {
        const qint64 bytes = info.size();
        QString sizeText;
        if (bytes >= 1024LL * 1024LL) {
            sizeText = QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + " MB";
        } else if (bytes >= 1024) {
            sizeText = QString::number(bytes / 1024.0, 'f', 1) + " KB";
        } else {
            sizeText = QString::number(bytes) + " B";
        }
        result.append(QJsonObject{
            {"displayName", info.completeBaseName()},
            {"fileName", info.fileName()},
            {"sizeText", sizeText},
            {"path", QDir::toNativeSeparators(info.absoluteFilePath())},
            {"modified", info.lastModified().toString(Qt::ISODate)}
        });
    }
    return result;
}

bool LauncherCore::removeResourcepack(const QString &versionId, const QString &fileName, QString *error)
{
    const QString path = versionResourcepacksDirectory(versionId) + "/" + fileName;
    QFile file(path);
    if (!file.exists()) {
        if (error) *error = "资源包文件不存在";
        return false;
    }
    if (!file.remove()) {
        if (error) *error = "删除资源包失败";
        return false;
    }
    if (error) error->clear();
    return true;
}

QJsonArray LauncherCore::listVersionWorlds(const QString &versionId) const
{
    QJsonArray result;
    const QDir savesDir(versionWorldsDirectory(versionId));
    const QFileInfoList directories = savesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : directories) {
        result.append(QJsonObject{
            {"name", info.fileName()},
            {"path", QDir::toNativeSeparators(info.absoluteFilePath())},
            {"modified", info.lastModified().toString(Qt::ISODate)}
        });
    }
    return result;
}

bool LauncherCore::removeWorld(const QString &versionId, const QString &worldName, QString *error)
{
    const QString worldPath = versionWorldsDirectory(versionId) + "/" + worldName;
    QDir worldDir(worldPath);
    if (!worldDir.exists()) {
        if (error) {
            *error = "世界目录不存在";
        }
        return false;
    }
    if (!worldDir.removeRecursively()) {
        if (error) {
            *error = "删除世界失败";
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QJsonArray LauncherCore::scanInstalledLoaders(const QString &versionId) const
{
    const QString base = versionBaseGameVersion(versionId);
    return scanLoadersForBaseVersion(base);
}

QJsonArray LauncherCore::scanLoadersForBaseVersion(const QString &baseVersionId) const
{
    QJsonArray result;
    const QString baseDir = versionsRoot() + "/" + baseVersionId;
    const QDir dir(baseDir);
    if (!dir.exists()) return result;

    const QString vanillaJson = baseVersionId + ".json";
    const auto entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString fileName = entry.fileName();
        if (fileName.compare(vanillaJson, Qt::CaseInsensitive) == 0) continue;
        QString err;
        const QJsonObject json = loadVersionJsonWithOverride(baseVersionId, fileName, false, &err);
        if (json.isEmpty()) continue;
        const QString loaderType = detectLoaderType(fileName, json);
        if (loaderType == "vanilla") continue;
        const QString inherits = json.value("inheritsFrom").toString();
        result.append(QJsonObject{
            {"id", fileName},
            {"loaderType", loaderType},
            {"baseVersion", baseVersionId},
            {"inheritsFrom", inherits.isEmpty() ? baseVersionId : inherits},
            {"path", QDir::toNativeSeparators(entry.absoluteFilePath())}
        });
    }

    const QStringList installed = listInstalledVersions();
    for (const QString &candidate : installed) {
        if (candidate.compare(baseVersionId, Qt::CaseInsensitive) == 0) continue;
        if (!versionMatchesBase(candidate, baseVersionId)) continue;
        QString err;
        const QJsonObject json = loadVersionJson(candidate, false, &err);
        if (json.isEmpty()) continue;
        const QString loaderType = detectLoaderType(candidate, json);
        if (loaderType == "vanilla") continue;
        bool alreadyListed = false;
        for (const QJsonValue &v : result) {
            if (v.toObject().value("id").toString().compare(candidate, Qt::CaseInsensitive) == 0) {
                alreadyListed = true;
                break;
            }
        }
        if (alreadyListed) continue;
        result.append(QJsonObject{
            {"id", candidate},
            {"loaderType", loaderType},
            {"baseVersion", baseVersionId},
            {"inheritsFrom", json.value("inheritsFrom").toString()},
            {"path", QDir::toNativeSeparators(versionsRoot() + "/" + candidate)}
        });
    }
    return result;
}

bool LauncherCore::setVersionSelectedLoader(const QString &baseVersionId, const QString &loaderType, const QString &loaderJson, QString *error)
{
    if (baseVersionId.trimmed().isEmpty()) {
        if (error) *error = "版本 ID 不能为空";
        return false;
    }
    LauncherConfig &config = m_configStore.config();
    ensureVersionProfileStored(baseVersionId, nullptr);
    LauncherVersionProfile &profile = config.versionProfiles[baseVersionId];
    profile.selectedLoaderType = loaderType.trimmed();
    profile.selectedLoaderJson = loaderJson.trimmed();
    if (config.selectedVersion.compare(baseVersionId, Qt::CaseInsensitive) == 0) {
        config.selectedLoaderType = profile.selectedLoaderType;
        config.selectedLoaderJson = profile.selectedLoaderJson;
    }
    return saveConfig(error);
}

bool LauncherCore::clearVersionSelectedLoader(const QString &baseVersionId, QString *error)
{
    if (baseVersionId.trimmed().isEmpty()) {
        if (error) *error = "版本 ID 不能为空";
        return false;
    }
    LauncherConfig &config = m_configStore.config();
    if (config.versionProfiles.contains(baseVersionId)) {
        LauncherVersionProfile &profile = config.versionProfiles[baseVersionId];
        profile.selectedLoaderType.clear();
        profile.selectedLoaderJson.clear();
    }
    if (config.selectedVersion.compare(baseVersionId, Qt::CaseInsensitive) == 0) {
        config.selectedLoaderType.clear();
        config.selectedLoaderJson.clear();
    }
    return saveConfig(error);
}

QString LauncherCore::resolveActiveLoaderJson(const QString &baseVersionId) const
{
    const LauncherConfig &config = m_configStore.config();
    if (config.versionProfiles.contains(baseVersionId)) {
        const LauncherVersionProfile &profile = config.versionProfiles.value(baseVersionId);
        if (!profile.selectedLoaderJson.isEmpty()) {
            return profile.selectedLoaderJson;
        }
    }
    if (config.selectedVersion.compare(baseVersionId, Qt::CaseInsensitive) == 0
        && !config.selectedLoaderJson.isEmpty()) {
        return config.selectedLoaderJson;
    }
    return {};
}

bool LauncherCore::uninstallLoaderFromVersion(const QString &baseVersionId, const QString &loaderType, const QString &loaderJson, QString *error)
{
    Q_UNUSED(loaderType);
    if (baseVersionId.trimmed().isEmpty()) {
        if (error) *error = "版本 ID 不能为空";
        return false;
    }
    const QString jsonPath = versionJsonPath(baseVersionId, loaderJson);
    const QString activeJson = resolveActiveLoaderJson(baseVersionId);
    if (QFileInfo::exists(jsonPath)) {
        QFile f(jsonPath);
        if (!f.remove()) {
            if (error) *error = QString("无法删除加载器文件: %1").arg(QDir::toNativeSeparators(jsonPath));
            return false;
        }
    }
    if (activeJson.compare(loaderJson, Qt::CaseInsensitive) == 0
        || activeJson.compare(jsonPath, Qt::CaseInsensitive) == 0) {
        clearVersionSelectedLoader(baseVersionId, nullptr);
    }
    return true;
}

bool LauncherCore::uninstallVersion(const QString &versionId, QString *error)
{
    const QString versionDir = versionsRoot() + "/" + versionId;
    if (QDir(versionDir).exists() && !QDir(versionDir).removeRecursively()) {
        if (error) {
            *error = "删除版本目录失败";
        }
        return false;
    }

    const QString instanceDir = instancesRoot() + "/" + versionId;
    if (QDir(instanceDir).exists() && !QDir(instanceDir).removeRecursively()) {
        if (error) {
            *error = "删除版本实例目录失败";
        }
        return false;
    }

    LauncherConfig &config = m_configStore.config();
    config.versionProfiles.remove(versionId);
    if (config.selectedVersion == versionId) {
        config.selectedVersion.clear();
    }
    return saveConfig(error);
}

QStringList LauncherCore::discoverJavaExecutables() const
{
    QStringList candidates;
    const QString configured = QDir::fromNativeSeparators(m_configStore.config().javaPath);
    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        candidates << configured;
    }

    const QStringList knownRoots = {
        QDir::homePath() + "/AppData/Local/Programs",
        QDir::homePath() + "/AppData/Local/Microsoft",
        QDir::homePath() + "/AppData/Roaming",
        "C:/Program Files/Java",
        "C:/Program Files/Eclipse Adoptium",
        "C:/Program Files/Microsoft",
        "C:/Program Files/AdoptOpenJDK"
    };

    for (const QString &root : knownRoots) {
        if (!QFileInfo::exists(root)) {
            continue;
        }
        QDirIterator it(root, QStringList() << javaExecutableName(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            candidates << QDir::toNativeSeparators(it.next());
        }
    }

    candidates << locateBundledJava(QString());
    candidates.removeDuplicates();
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const QString &item) {
        return item.isEmpty() || !QFileInfo::exists(item);
    }), candidates.end());
    return candidates;
}

namespace {

constexpr const char *kMicrosoftDefaultClientId = "c4502edb-87c6-40cb-b595-64a280cf8906";
constexpr const char *kMicrosoftScope = "XboxLive.signin offline_access";
constexpr const char *kMicrosoftDefaultRedirect = "https://login.live.com/oauth20_desktop.srf";
constexpr const char *kMicrosoftLiveAuthorizeUrl = "https://login.live.com/oauth20_authorize.srf";
constexpr const char *kMicrosoftLiveTokenUrl = "https://login.live.com/oauth20_token.srf";
constexpr const char *kMicrosoftDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr const char *kMicrosoftConsumersTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";

constexpr const char *kXboxLiveAuthenticateUrl = "https://user.auth.xboxlive.com/user/authenticate";
constexpr const char *kXboxXstsAuthorizeUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr const char *kMinecraftLoginXboxUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr const char *kMinecraftEntitlementUrl = "https://api.minecraftservices.com/entitlements/mcstore";
constexpr const char *kMinecraftLicenseUrl = "https://api.minecraftservices.com/entitlements/license";
constexpr const char *kMinecraftProfileUrl = "https://api.minecraftservices.com/minecraft/profile";

QString randomBase64Url(int byteCount)
{
    QByteArray bytes(byteCount, Qt::Uninitialized);
    QRandomGenerator *gen = QRandomGenerator::system();
    for (int i = 0; i < byteCount; ++i) {
        bytes[i] = static_cast<char>(gen->bounded(256));
    }
    QByteArray b64 = bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(b64);
}

QString sha256Base64Url(const QString &asciiInput)
{
    const QByteArray bytes = asciiInput.toLatin1();
    const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

qint64 currentUtcSec()
{
    return QDateTime::currentSecsSinceEpoch();
}

} // namespace

QString LauncherCore::effectiveMicrosoftClientId() const
{
    const QString configured = m_configStore.config().microsoftClientId.trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return QString::fromLatin1(kMicrosoftDefaultClientId);
}

QString LauncherCore::microsoftDefaultRedirectUri() const
{
    return QString::fromLatin1(kMicrosoftDefaultRedirect);
}

QString LauncherCore::microsoftOAuthScope() const
{
    return QString::fromLatin1(kMicrosoftScope);
}

QString LauncherCore::buildMicrosoftAuthUrl() const
{
    const QString clientId = effectiveMicrosoftClientId();
    const QString redirect = microsoftDefaultRedirectUri();

    QUrl url(QString::fromLatin1(kMicrosoftLiveAuthorizeUrl));
    QUrlQuery query;
    query.addQueryItem("client_id", clientId);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("redirect_uri", redirect);
    query.addQueryItem("scope", microsoftOAuthScope());
    query.addQueryItem("prompt", "select_account");
    url.setQuery(query);
    return url.toString();
}

LauncherCore::MicrosoftPkceAuth LauncherCore::buildMicrosoftPkceAuthUrl() const
{
    MicrosoftPkceAuth auth;
    auth.redirectUri = microsoftDefaultRedirectUri();
    auth.codeVerifier = randomBase64Url(32);
    auth.state = randomBase64Url(16);
    const QString codeChallenge = sha256Base64Url(auth.codeVerifier);

    const QString clientId = effectiveMicrosoftClientId();
    QUrl url(QString::fromLatin1(kMicrosoftLiveAuthorizeUrl));
    QUrlQuery query;
    query.addQueryItem("client_id", clientId);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("redirect_uri", auth.redirectUri);
    query.addQueryItem("scope", microsoftOAuthScope());
    query.addQueryItem("prompt", "select_account");
    query.addQueryItem("code_challenge", codeChallenge);
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("state", auth.state);
    url.setQuery(query);
    auth.authorizeUrl = url.toString();
    return auth;
}

bool LauncherCore::saveConfig(QString *error)
{
    if (!m_configStore.save()) {
        if (error) {
            *error = "配置保存失败";
        }
        return false;
    }
    return true;
}

bool LauncherCore::addOfflineAccount(const QString &username, QString *error)
{
    if (username.trimmed().isEmpty()) {
        *error = "离线用户名不能为空";
        return false;
    }

    Account account;
    account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    account.type = Account::Type::Offline;
    account.username = username.trimmed();
    account.uuid = makeOfflineUuid(account.username);
    account.accessToken = account.uuid.remove('-');

    LauncherConfig &config = m_configStore.config();
    config.accounts.append(account);
    config.selectedAccountIndex = config.accounts.size() - 1;
    return saveConfig(error);
}

QString LauncherCore::postFormSync(const QUrl &url,
                                  const QUrlQuery &form,
                                  QString *error,
                                  const QList<QPair<QString, QString>> &headers,
                                  bool ignoreHttpError,
                                  int *outHttpCode) const
{
    if (error) error->clear();
    if (outHttpCode) *outHttpCode = 0;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    for (const auto &h : headers) {
        req.setRawHeader(h.first.toUtf8(), h.second.toUtf8());
    }

    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();
    QNetworkReply *reply = m_networkManager->post(req, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (outHttpCode) *outHttpCode = httpCode;

    const QByteArray bytes = reply->readAll();
    const bool netError = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();

    if (netError && !ignoreHttpError) {
        if (error) *error = QString("HTTP POST 失败: %1").arg(reply->errorString());
        return {};
    }
    if (!ignoreHttpError && httpCode >= 400) {
        if (error) *error = QString("HTTP POST 返回错误状态码 %1").arg(httpCode);
    }
    return QString::fromUtf8(bytes);
}

QString LauncherCore::postJsonSync(const QUrl &url,
                                   const QJsonObject &payload,
                                   QString *error,
                                   const QList<QPair<QString, QString>> &headers,
                                   bool ignoreHttpError,
                                   int *outHttpCode) const
{
    if (error) error->clear();
    if (outHttpCode) *outHttpCode = 0;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    for (const auto &h : headers) {
        req.setRawHeader(h.first.toUtf8(), h.second.toUtf8());
    }

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_networkManager->post(req, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (outHttpCode) *outHttpCode = httpCode;

    const QByteArray bytes = reply->readAll();
    const bool netError = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();

    if (netError && !ignoreHttpError) {
        if (error) *error = QString("HTTP POST 失败: %1").arg(reply->errorString());
        return {};
    }
    if (!ignoreHttpError && httpCode >= 400) {
        if (error) *error = QString("HTTP POST 返回错误状态码 %1").arg(httpCode);
    }
    return QString::fromUtf8(bytes);
}

QString LauncherCore::describeXboxErrorCode(qint64 xErr, const QString &redirectUrl) const
{
    switch (xErr) {
    case 2148916227LL:
        return QStringLiteral("Xbox Live 账号已被封禁 (XErr=2148916227)");
    case 2148916233LL:
        return QStringLiteral("该 Microsoft 账号未开通 Xbox Live 账户，请先注册 Xbox Live (XErr=2148916233)");
    case 2148916235LL:
        return QStringLiteral("Xbox Live 在当前所在国家/地区不可用 (XErr=2148916235)");
    case 2148916238LL:
        if (!redirectUrl.isEmpty()) {
            return QStringLiteral("账号为未成年账号，需要先加入家庭组。请访问: %1").arg(redirectUrl);
        }
        return QStringLiteral("账号为未成年账号，需要家长将其加入家庭组 (XErr=2148916238)");
    default:
        if (!redirectUrl.isEmpty()) {
            return QStringLiteral("Xbox Live 授权失败，XErr=%1，跳转地址: %2").arg(xErr).arg(redirectUrl);
        }
        return QStringLiteral("Xbox Live 授权失败，XErr=%1").arg(xErr);
    }
}

static QString extractXboxUhs(const QJsonObject &resp, bool *hasError, QString *error)
{
    const QJsonObject claims = resp.value("DisplayClaims").toObject();
    const QJsonArray xui = claims.value("xui").toArray();
    if (xui.isEmpty()) {
        if (hasError) *hasError = true;
        if (error) *error = QStringLiteral("Xbox 返回的 DisplayClaims/xui 为空");
        return {};
    }
    const QJsonObject first = xui.first().toObject();
    if (!first.contains("uhs")) {
        if (hasError) *hasError = true;
        if (error) *error = QStringLiteral("Xbox 返回的 xui 中缺少 uhs 字段");
        return {};
    }
    return first.value("uhs").toString();
}

bool LauncherCore::authenticateXboxLive(const QString &liveAccessToken, MicrosoftXboxAuthResult *outXbl, QString *error) const
{
    const QJsonObject payload{
        {"Properties", QJsonObject{
            {"AuthMethod", "RPS"},
            {"SiteName", "user.auth.xboxlive.com"},
            {"RpsTicket", "d=" + liveAccessToken}
        }},
        {"RelyingParty", "http://auth.xboxlive.com"},
        {"TokenType", "JWT"}
    };

    int httpCode = 0;
    QString localErr;
    const QString raw = postJsonSync(QUrl(QString::fromLatin1(kXboxLiveAuthenticateUrl)),
                                     payload, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();

    const qint64 xErr = resp.value("XErr").toVariant().toLongLong();
    const QString redirect = resp.value("Redirect").toString();
    if (xErr != 0) {
        if (error) *error = describeXboxErrorCode(xErr, redirect);
        return false;
    }
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("Xbox Live 鉴权请求失败: %1").arg(localErr);
        return false;
    }

    const QString token = resp.value("Token").toString();
    if (token.isEmpty()) {
        if (error) *error = QStringLiteral("Xbox Live 响应缺少 Token");
        return false;
    }

    bool uhsErr = false;
    QString uhsErrorText;
    const QString uhs = extractXboxUhs(resp, &uhsErr, &uhsErrorText);
    if (uhsErr) {
        if (error) *error = uhsErrorText;
        return false;
    }

    if (outXbl) {
        outXbl->token = token;
        outXbl->uhs = uhs;
        const QJsonObject first = resp.value("DisplayClaims").toObject().value("xui").toArray().first().toObject();
        outXbl->xuid = first.value("xid").toString();
    }
    return true;
}

bool LauncherCore::authenticateMinecraftXsts(const QString &xblToken, MicrosoftXboxAuthResult *outXsts, const QString &expectedUhs, QString *error) const
{
    const QJsonObject payload{
        {"Properties", QJsonObject{
            {"SandboxId", "RETAIL"},
            {"UserTokens", QJsonArray{ xblToken }}
        }},
        {"RelyingParty", "rp://api.minecraftservices.com/"},
        {"TokenType", "JWT"}
    };

    int httpCode = 0;
    QString localErr;
    const QString raw = postJsonSync(QUrl(QString::fromLatin1(kXboxXstsAuthorizeUrl)),
                                     payload, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();

    const qint64 xErr = resp.value("XErr").toVariant().toLongLong();
    const QString redirect = resp.value("Redirect").toString();
    if (xErr != 0) {
        if (error) *error = describeXboxErrorCode(xErr, redirect);
        return false;
    }
    if (httpCode == 401) {
        if (error) *error = QStringLiteral("XSTS 授权失败 (HTTP 401)，Xbox 凭证无效或已过期");
        return false;
    }
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("XSTS 授权请求失败: %1").arg(localErr);
        return false;
    }

    const QString token = resp.value("Token").toString();
    if (token.isEmpty()) {
        if (error) *error = QStringLiteral("XSTS 响应缺少 Token");
        return false;
    }

    bool uhsErr = false;
    QString uhsErrorText;
    const QString uhs = extractXboxUhs(resp, &uhsErr, &uhsErrorText);
    if (uhsErr) {
        if (error) *error = uhsErrorText;
        return false;
    }
    if (!expectedUhs.isEmpty() && uhs != expectedUhs) {
        if (error) *error = QStringLiteral("XSTS 返回的 uhs 与 XBL 返回的不一致");
        return false;
    }

    if (outXsts) {
        outXsts->token = token;
        outXsts->uhs = uhs;
        const QJsonObject first = resp.value("DisplayClaims").toObject().value("xui").toArray().first().toObject();
        outXsts->xuid = first.value("xid").toString();
    }
    return true;
}

bool LauncherCore::loginMinecraftWithXbox(const QString &uhs, const QString &xstsToken, QString *outMcAccessToken, QString *outMcTokenType, int *outExpiresInSec, QString *outUsername, QString *error) const
{
    const QJsonObject payload{
        {"identityToken", QString("XBL3.0 x=%1;%2").arg(uhs, xstsToken)}
    };

    int httpCode = 0;
    QString localErr;
    const QString raw = postJsonSync(QUrl(QString::fromLatin1(kMinecraftLoginXboxUrl)),
                                     payload, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("Minecraft Xbox 登录请求失败: %1").arg(localErr);
        return false;
    }

    const QString access = resp.value("access_token").toString();
    if (access.isEmpty()) {
        const QString errMsg = resp.value("errorMessage").toString(resp.value("error").toString());
        if (error) {
            if (!errMsg.isEmpty()) *error = QStringLiteral("Minecraft Xbox 登录失败: %1").arg(errMsg);
            else *error = QStringLiteral("Minecraft Xbox 登录响应缺少 access_token");
        }
        return false;
    }

    if (outMcAccessToken) *outMcAccessToken = access;
    if (outMcTokenType) *outMcTokenType = resp.value("token_type").toString("Bearer");
    if (outExpiresInSec) *outExpiresInSec = resp.value("expires_in").toInt(3600);
    if (outUsername) *outUsername = resp.value("username").toString();
    return true;
}

bool LauncherCore::checkMinecraftOwnership(const QString &mcAccessToken, QString *error)
{
    QString localErr;
    const QByteArray bytes = getBytes(QUrl(QString::fromLatin1(kMinecraftEntitlementUrl)),
                                      &localErr,
                                      {{"Authorization", "Bearer " + mcAccessToken}});
    if (!localErr.isEmpty()) {
        if (error) *error = QStringLiteral("读取 Minecraft 授权清单失败: %1").arg(localErr);
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(bytes).object();
    const QJsonArray items = obj.value("items").toArray();
    const bool hasGame = std::any_of(items.begin(), items.end(), [](const QJsonValue &v) {
        const QJsonObject item = v.toObject();
        const QString name = item.value("name").toString();
        return name == "product_minecraft" || name == "game_minecraft";
    });
    if (!hasGame) {
        if (error) *error = QStringLiteral("账号未检测到 Minecraft Java Edition 购买授权（mcstore/items 为空或无游戏项）");
        return false;
    }
    return true;
}

bool LauncherCore::fetchMinecraftProfile(const QString &mcTokenType, const QString &mcAccessToken, QString *outUuid, QString *outName, QString *error)
{
    const QString authHeader = QString("%1 %2").arg(mcTokenType.isEmpty() ? "Bearer" : mcTokenType, mcAccessToken);

    QNetworkRequest req(QUrl(QString::fromLatin1(kMinecraftProfileUrl)));
    req.setRawHeader("Authorization", authHeader.toUtf8());
    QNetworkReply *reply = m_networkManager->get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray bytes = reply->readAll();
    const auto netErr = reply->error();
    reply->deleteLater();

    if (httpCode == 404) {
        QString licErr;
        const QByteArray licBytes = getBytes(QUrl(QString::fromLatin1(kMinecraftLicenseUrl)),
                                             &licErr,
                                             {{"Authorization", authHeader}});
        bool hasLicense = false;
        if (licErr.isEmpty()) {
            const QJsonObject lic = QJsonDocument::fromJson(licBytes).object();
            const QJsonArray items = lic.value("items").toArray();
            hasLicense = std::any_of(items.begin(), items.end(), [](const QJsonValue &v) {
                return v.toObject().value("name").toString() == "game_minecraft";
            });
        }
        if (error) {
            if (hasLicense) {
                *error = QStringLiteral("该 Microsoft 账号已拥有 Minecraft 授权，但尚未在官方启动器中创建 Java 版玩家档案。请先用官方启动器登录一次以创建角色名。");
            } else {
                *error = QStringLiteral("该 Microsoft 账号没有 Minecraft Java Edition 授权，或尚未购买游戏。");
            }
        }
        return false;
    }
    if (netErr != QNetworkReply::NoError) {
        if (error) *error = QStringLiteral("获取 Minecraft 档案请求失败: %1").arg(netErr);
        return false;
    }
    if (httpCode >= 400) {
        if (error) *error = QStringLiteral("获取 Minecraft 档案返回错误状态码 %1").arg(httpCode);
        return false;
    }

    const QJsonObject profile = QJsonDocument::fromJson(bytes).object();
    const QString id = profile.value("id").toString();
    const QString name = profile.value("name").toString();
    if (id.isEmpty() || name.isEmpty()) {
        const QString err = profile.value("errorMessage").toString(profile.value("error").toString());
        if (error) {
            if (!err.isEmpty()) *error = QStringLiteral("Minecraft 档案返回错误: %1").arg(err);
            else *error = QStringLiteral("Minecraft 档案响应缺少 id 或 name 字段");
        }
        return false;
    }

    QString dashed = id;
    if (dashed.length() == 32 && !dashed.contains('-')) {
        dashed.insert(8, '-').insert(13, '-').insert(18, '-').insert(23, '-');
    }
    if (outUuid) *outUuid = dashed;
    if (outName) *outName = name;
    return true;
}

bool LauncherCore::saveMicrosoftAccountFromTokensInternal(const QString &liveAccessToken,
                                                          const QString &liveRefreshToken,
                                                          const QString &clientId,
                                                          int msExpiresInSec,
                                                          QString *error)
{
    MicrosoftXboxAuthResult xbl;
    if (!authenticateXboxLive(liveAccessToken, &xbl, error)) return false;

    MicrosoftXboxAuthResult xsts;
    if (!authenticateMinecraftXsts(xbl.token, &xsts, xbl.uhs, error)) return false;

    QString mcAccessToken, mcTokenType, mcUsername;
    int mcExpiresInSec = 3600;
    if (!loginMinecraftWithXbox(xsts.uhs, xsts.token, &mcAccessToken, &mcTokenType, &mcExpiresInSec, &mcUsername, error)) {
        return false;
    }

    if (!checkMinecraftOwnership(mcAccessToken, error)) return false;

    QString profileUuid, profileName;
    if (!fetchMinecraftProfile(mcTokenType, mcAccessToken, &profileUuid, &profileName, error)) return false;

    Account account;
    account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    account.type = Account::Type::Microsoft;
    account.username = profileName;
    account.uuid = profileUuid;
    account.profileId = profileUuid;
    account.accessToken = mcAccessToken;
    account.tokenType = mcTokenType;
    account.refreshToken = liveRefreshToken;
    account.clientId = clientId;
    account.xuid = xsts.xuid.isEmpty() ? xbl.xuid : xsts.xuid;
    const int expirySec = msExpiresInSec > 0 ? msExpiresInSec : mcExpiresInSec;
    account.expiry = QDateTime::currentDateTimeUtc().addSecs(expirySec);
    account.microsoftUsername = mcUsername;

    LauncherConfig &config = m_configStore.config();
    config.accounts.append(account);
    config.selectedAccountIndex = config.accounts.size() - 1;
    return saveConfig(error);
}

bool LauncherCore::saveMicrosoftAccountFromTokens(const QString &liveAccessToken,
                                                  const QString &liveRefreshToken,
                                                  const QString &clientId,
                                                  int msExpiresInSec,
                                                  QString *error)
{
    return saveMicrosoftAccountFromTokensInternal(liveAccessToken, liveRefreshToken, clientId, msExpiresInSec, error);
}

bool LauncherCore::exchangeMicrosoftAuthCodeForToken(const QString &clientId,
                                                     const QString &code,
                                                     const QString &redirectUri,
                                                     const QString &codeVerifier,
                                                     QString *outAccessToken,
                                                     QString *outRefreshToken,
                                                     int *outExpiresInSec,
                                                     QString *outOAuthError,
                                                     QString *error) const
{
    QUrlQuery form;
    form.addQueryItem("client_id", clientId);
    form.addQueryItem("code", code);
    form.addQueryItem("grant_type", "authorization_code");
    if (!codeVerifier.isEmpty()) {
        form.addQueryItem("code_verifier", codeVerifier);
    }
    form.addQueryItem("redirect_uri", redirectUri);
    form.addQueryItem("scope", microsoftOAuthScope());

    int httpCode = 0;
    QString localErr;
    const QString raw = postFormSync(QUrl(QString::fromLatin1(kMicrosoftLiveTokenUrl)),
                                     form, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();

    const QString oauthErr = resp.value("error").toString();
    const QString oauthDesc = resp.value("error_description").toString();
    if (!oauthErr.isEmpty()) {
        if (outOAuthError) *outOAuthError = oauthErr;
        if (error) {
            if (oauthErr == "invalid_grant" && oauthDesc.contains("AADSTS70000")) {
                *error = QStringLiteral("Microsoft OAuth 授权码已过期或无效 (invalid_grant/AADSTS70000)，请重新发起登录。");
            } else if (!oauthDesc.isEmpty()) {
                *error = QStringLiteral("Microsoft OAuth 错误 [%1]: %2").arg(oauthErr, oauthDesc);
            } else {
                *error = QStringLiteral("Microsoft OAuth 错误: %1").arg(oauthErr);
            }
        }
        return false;
    }
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("Microsoft OAuth 令牌请求失败: %1").arg(localErr);
        return false;
    }

    const QString access = resp.value("access_token").toString();
    const QString refresh = resp.value("refresh_token").toString();
    if (access.isEmpty()) {
        if (error) *error = QStringLiteral("Microsoft OAuth 响应缺少 access_token");
        return false;
    }

    if (outAccessToken) *outAccessToken = access;
    if (outRefreshToken) *outRefreshToken = refresh;
    if (outExpiresInSec) *outExpiresInSec = resp.value("expires_in").toInt(3600);
    return true;
}

bool LauncherCore::exchangeMicrosoftDeviceCodeForToken(const QString &clientId,
                                                       const QString &deviceCode,
                                                       QString *outAccessToken,
                                                       QString *outRefreshToken,
                                                       int *outExpiresInSec,
                                                       QString *outPollError,
                                                       QString *error) const
{
    QUrlQuery form;
    form.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    form.addQueryItem("code", deviceCode);
    form.addQueryItem("client_id", clientId);

    int httpCode = 0;
    QString localErr;
    const QString raw = postFormSync(QUrl(QString::fromLatin1(kMicrosoftConsumersTokenUrl)),
                                     form, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();

    const QString pollErr = resp.value("error").toString();
    if (!pollErr.isEmpty()) {
        if (outPollError) *outPollError = pollErr;
        const QString desc = resp.value("error_description").toString();
        if (pollErr == "authorization_pending"
            || pollErr == "expired_token"
            || pollErr == "slow_down"
            || pollErr == "bad_verification_code") {
            return false;
        }
        if (error) {
            if (!desc.isEmpty()) *error = QStringLiteral("Microsoft Device Code 错误 [%1]: %2").arg(pollErr, desc);
            else *error = QStringLiteral("Microsoft Device Code 错误: %1").arg(pollErr);
        }
        return false;
    }
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("Microsoft Device Code 令牌请求失败: %1").arg(localErr);
        return false;
    }

    const QString access = resp.value("access_token").toString();
    if (access.isEmpty()) {
        if (error) *error = QStringLiteral("Microsoft Device Code 响应缺少 access_token");
        return false;
    }
    if (outAccessToken) *outAccessToken = access;
    if (outRefreshToken) *outRefreshToken = resp.value("refresh_token").toString();
    if (outExpiresInSec) *outExpiresInSec = resp.value("expires_in").toInt(3600);
    return true;
}

bool LauncherCore::exchangeMicrosoftRefreshToken(const QString &clientId,
                                                 const QString &oldRefreshToken,
                                                 QString *outNewAccessToken,
                                                 QString *outNewRefreshToken,
                                                 int *outExpiresInSec,
                                                 QString *outOAuthError,
                                                 QString *error) const
{
    QUrlQuery form;
    form.addQueryItem("client_id", clientId);
    form.addQueryItem("refresh_token", oldRefreshToken);
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("scope", microsoftOAuthScope());

    int httpCode = 0;
    QString localErr;
    const QString raw = postFormSync(QUrl(QString::fromLatin1(kMicrosoftLiveTokenUrl)),
                                     form, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();

    const QString oauthErr = resp.value("error").toString();
    const QString oauthDesc = resp.value("error_description").toString();
    if (!oauthErr.isEmpty()) {
        if (outOAuthError) *outOAuthError = oauthErr;
        if (error) {
            if (oauthErr == "invalid_grant") {
                *error = QStringLiteral("Microsoft Refresh Token 已失效（invalid_grant），请重新登录正版账户。");
            } else if (!oauthDesc.isEmpty()) {
                *error = QStringLiteral("Microsoft Refresh 错误 [%1]: %2").arg(oauthErr, oauthDesc);
            } else {
                *error = QStringLiteral("Microsoft Refresh 错误: %1").arg(oauthErr);
            }
        }
        return false;
    }
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("Microsoft Refresh Token 请求失败: %1").arg(localErr);
        return false;
    }

    const QString access = resp.value("access_token").toString();
    if (access.isEmpty()) {
        if (error) *error = QStringLiteral("Microsoft Refresh 响应缺少 access_token");
        return false;
    }

    if (outNewAccessToken) *outNewAccessToken = access;
    if (outNewRefreshToken) *outNewRefreshToken = resp.value("refresh_token").toString(oldRefreshToken);
    if (outExpiresInSec) *outExpiresInSec = resp.value("expires_in").toInt(3600);
    return true;
}

bool LauncherCore::loginMicrosoftAuthorizationCode(const QString &codeOrRedirectUrl, const QString &codeVerifier, QString *error)
{
    const QString clientId = effectiveMicrosoftClientId();
    const QString redirect = microsoftDefaultRedirectUri();

    QString code = codeOrRedirectUrl.trimmed();
    const QUrl asUrl(code);
    if (asUrl.isValid() && code.contains("oauth20_desktop.srf")) {
        QUrlQuery q(asUrl);
        code = q.queryItemValue("code");
    }
    if (code.isEmpty()) {
        if (error) *error = QStringLiteral("未解析到授权码 code，请检查粘贴内容。");
        return false;
    }

    QString accessToken, refreshToken, oauthErr;
    int expiresInSec = 3600;
    if (!exchangeMicrosoftAuthCodeForToken(clientId, code, redirect, codeVerifier,
                                           &accessToken, &refreshToken, &expiresInSec,
                                           &oauthErr, error)) {
        return false;
    }

    return saveMicrosoftAccountFromTokensInternal(accessToken, refreshToken, clientId, expiresInSec, error);
}

bool LauncherCore::loginMicrosoftWithRedirectUrl(const QString &redirectUrl, QString *error)
{
    return loginMicrosoftAuthorizationCode(redirectUrl, QString(), error);
}

bool LauncherCore::startMicrosoftDeviceLogin(MicrosoftDeviceCode *outCode, QString *error)
{
    if (!outCode) {
        if (error) *error = QStringLiteral("空输出参数");
        return false;
    }

    const QString clientId = effectiveMicrosoftClientId();
    QUrlQuery form;
    form.addQueryItem("client_id", clientId);
    form.addQueryItem("scope", microsoftOAuthScope());

    int httpCode = 0;
    QString localErr;
    const QString raw = postFormSync(QUrl(QString::fromLatin1(kMicrosoftDeviceCodeUrl)),
                                     form, &localErr, {}, true, &httpCode);
    const QJsonObject resp = QJsonDocument::fromJson(raw.toUtf8()).object();

    const QString oauthErr = resp.value("error").toString();
    const QString oauthDesc = resp.value("error_description").toString();
    if (!oauthErr.isEmpty()) {
        if (error) {
            if (!oauthDesc.isEmpty()) *error = QStringLiteral("Device Code 错误 [%1]: %2").arg(oauthErr, oauthDesc);
            else *error = QStringLiteral("Device Code 错误: %1").arg(oauthErr);
        }
        return false;
    }
    if (!localErr.isEmpty() && httpCode >= 400 && resp.isEmpty()) {
        if (error) *error = QStringLiteral("Device Code 请求失败: %1").arg(localErr);
        return false;
    }

    const QString userCode = resp.value("user_code").toString();
    const QString verificationUri = resp.value("verification_uri").toString();
    const QString deviceCode = resp.value("device_code").toString();
    if (userCode.isEmpty() || verificationUri.isEmpty() || deviceCode.isEmpty()) {
        if (error) *error = QStringLiteral("Device Code 响应字段不完整");
        return false;
    }

    outCode->userCode = userCode;
    outCode->verificationUri = verificationUri;
    outCode->deviceCode = deviceCode;
    outCode->intervalSec = resp.value("interval").toInt(5);
    const int expiresIn = resp.value("expires_in").toInt(900);
    outCode->expiresAtUtcSec = currentUtcSec() + qMin(expiresIn, 900);
    return true;
}

LauncherCore::DevicePollOutcome LauncherCore::pollMicrosoftDeviceLogin(MicrosoftDeviceCode &ioCode, QString *pollErrorOut, QString *error)
{
    if (pollErrorOut) pollErrorOut->clear();
    if (error) error->clear();

    if (ioCode.expiresAtUtcSec > 0 && currentUtcSec() >= ioCode.expiresAtUtcSec) {
        return DevicePollOutcome::Expired;
    }

    const QString clientId = effectiveMicrosoftClientId();
    QString accessToken, refreshToken, pollErr;
    int expiresInSec = 3600;
    const bool ok = exchangeMicrosoftDeviceCodeForToken(clientId, ioCode.deviceCode,
                                                        &accessToken, &refreshToken, &expiresInSec,
                                                        &pollErr, error);
    if (!pollErr.isEmpty() && pollErrorOut) *pollErrorOut = pollErr;

    if (pollErr == "authorization_pending") {
        return DevicePollOutcome::Pending;
    }
    if (pollErr == "expired_token") {
        return DevicePollOutcome::Expired;
    }
    if (pollErr == "slow_down") {
        ioCode.intervalSec = qMin(60, ioCode.intervalSec + 5);
        return DevicePollOutcome::SlowDown;
    }
    if (!ok) {
        return DevicePollOutcome::Failed;
    }

    QString saveErr;
    if (!saveMicrosoftAccountFromTokensInternal(accessToken, refreshToken, clientId, expiresInSec, &saveErr)) {
        if (error) error->swap(saveErr);
        return DevicePollOutcome::Failed;
    }
    return DevicePollOutcome::Authorized;
}

bool LauncherCore::refreshMicrosoftAccount(Account &account, QString *error)
{
    if (account.type != Account::Type::Microsoft) {
        return true;
    }
    if (account.refreshToken.isEmpty()) {
        if (error) *error = QStringLiteral("缺少 refresh token，需重新登录");
        return false;
    }
    const QString clientId = account.clientId.isEmpty() ? effectiveMicrosoftClientId() : account.clientId;

    QString newLiveAccess, newLiveRefresh, oauthErr;
    int expiresInSec = 3600;
    if (!exchangeMicrosoftRefreshToken(clientId, account.refreshToken,
                                       &newLiveAccess, &newLiveRefresh, &expiresInSec,
                                       &oauthErr, error)) {
        return false;
    }

    MicrosoftXboxAuthResult xbl;
    if (!authenticateXboxLive(newLiveAccess, &xbl, error)) return false;
    MicrosoftXboxAuthResult xsts;
    if (!authenticateMinecraftXsts(xbl.token, &xsts, xbl.uhs, error)) return false;

    QString mcAccess, mcType, mcUser;
    int mcExpiry = 3600;
    if (!loginMinecraftWithXbox(xsts.uhs, xsts.token, &mcAccess, &mcType, &mcExpiry, &mcUser, error)) {
        return false;
    }

    account.accessToken = mcAccess;
    account.tokenType = mcType;
    if (!newLiveRefresh.isEmpty()) account.refreshToken = newLiveRefresh;
    account.clientId = clientId;
    if (!xsts.xuid.isEmpty()) account.xuid = xsts.xuid;
    account.expiry = QDateTime::currentDateTimeUtc().addSecs(expiresInSec > 0 ? expiresInSec : mcExpiry);
    account.microsoftUsername = mcUser;

    if (account.uuid.isEmpty() || account.username.isEmpty()) {
        QString uuid, name;
        if (fetchMinecraftProfile(mcType, mcAccess, &uuid, &name, error)) {
            account.uuid = uuid;
            account.profileId = uuid;
            account.username = name;
        }
    }
    return true;
}

bool LauncherCore::ensureMicrosoftAccountUsable(Account &account, QString *error)
{
    if (account.type != Account::Type::Microsoft) {
        return true;
    }
    if (account.expiry.isValid()
        && account.expiry > QDateTime::currentDateTimeUtc().addSecs(120)
        && !account.accessToken.isEmpty()) {
        return true;
    }
    return refreshMicrosoftAccount(account, error);
}



bool LauncherCore::installVanillaVersion(const QString &versionId, QString *error)
{
    const QJsonObject metadata = findVersionMetadata(versionId, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString versionDir = versionsRoot() + "/" + versionId;
    ensureDirectory(versionDir, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QJsonDocument versionDoc = getJson(QUrl(rewriteUrl(metadata.value("url").toString())), error);
    if (!error->isEmpty()) {
        return false;
    }

    QFile versionFile(versionDir + "/" + versionId + ".json");
    if (!versionFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = "无法写入版本 JSON";
        return false;
    }
    versionFile.write(versionDoc.toJson(QJsonDocument::Indented));

    VersionInstallContext ctx;
    ctx.versionId = versionId;
    ctx.versionDir = versionDir;
    ctx.instanceDir = instanceDirectoryForVersion(versionId);
    ctx.versionJson = versionDoc.object();

    if (!resolveInheritance(ctx.versionJson, error)) {
        return false;
    }

    ensureDirectory(ctx.instanceDir, error);
    if (!error->isEmpty()) {
        return false;
    }

    log(QString("安装版本 %1").arg(versionId));
    const bool ok = installClientJar(ctx, error)
        && installLibraries(ctx, error)
        && installAssetIndex(ctx, error)
        && installAssets(ctx, error)
        && extractNatives(ctx, error);
    if (ok) {
        ensureVersionProfileStored(versionId, nullptr);
    }
    return ok;
}

bool LauncherCore::installVanillaVersionWithDialog(const QString &versionId, QString *error, QWidget *parentWidget)
{
    log(QString("开始安装版本: %1").arg(versionId));

    // Find a host widget for the in-window overlay: prefer the top-level window.
    QWidget *host = parentWidget ? parentWidget->window() : nullptr;
    if (!host) {
        host = QApplication::activeWindow();
    }

    auto failWith = [&](const QString &msg) -> bool {
        if (error) *error = msg;
        log(QString("安装失败: %1").arg(msg));
        return false;
    };

    // ---- 1. Version metadata & JSON -------------------------------------
    const QJsonObject metadata = findVersionMetadata(versionId, error);
    if (error && !error->isEmpty()) return false;

    const QString versionDir = versionsRoot() + "/" + versionId;
    ensureDirectory(versionDir, error);
    if (error && !error->isEmpty()) return false;

    const QJsonDocument versionDoc = getJson(QUrl(rewriteUrl(metadata.value("url").toString())), error);
    if (error && !error->isEmpty()) return false;

    QFile versionFile(versionDir + "/" + versionId + ".json");
    if (!versionFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failWith(QStringLiteral("无法写入版本 JSON"));
    }
    versionFile.write(versionDoc.toJson(QJsonDocument::Indented));
    versionFile.close();

    VersionInstallContext ctx;
    ctx.versionId = versionId;
    ctx.versionDir = versionDir;
    ctx.instanceDir = instanceDirectoryForVersion(versionId);
    ctx.versionJson = versionDoc.object();

    if (!resolveInheritance(ctx.versionJson, error)) {
        return false;
    }
    ensureDirectory(ctx.instanceDir, error);
    if (error && !error->isEmpty()) return false;

    log(QString("安装版本 %1").arg(versionId));

    // ---- 2. Build the full download task list ---------------------------
    QList<DownloadTaskSpec> tasks;
    const bool verify = shouldVerify();

    // 2a. Client jar
    {
        const QJsonObject client = ctx.versionJson.value("downloads").toObject().value("client").toObject();
        const QString url = rewriteUrl(client.value("url").toString());
        if (!url.isEmpty()) {
            DownloadTaskSpec t;
            t.url = QUrl(url);
            t.filePath = ctx.versionDir + "/" + ctx.versionId + ".jar";
            t.sha1 = client.value("sha1").toString();
            t.label = QString("客户端 Jar: %1").arg(ctx.versionId);
            tasks.append(t);
        }
    }

    // 2b. Libraries (artifacts + native classifiers)
    const QJsonArray libraries = ctx.versionJson.value("libraries").toArray();
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) continue;
        const QJsonObject downloads = library.value("downloads").toObject();

        const QJsonObject artifact = downloads.value("artifact").toObject();
        if (!artifact.isEmpty()) {
            DownloadTaskSpec t;
            t.url = QUrl(rewriteUrl(artifact.value("url").toString()));
            t.filePath = librariesRoot() + "/" + artifact.value("path").toString();
            t.sha1 = artifact.value("sha1").toString();
            t.label = QString("库: %1").arg(QFileInfo(t.filePath).fileName());
            if (t.url.isValid() && !t.filePath.isEmpty()) tasks.append(t);
        }

        const QString classifier = nativeClassifier(library);
        const QJsonObject nativeObject = downloads.value("classifiers").toObject().value(classifier).toObject();
        if (!nativeObject.isEmpty()) {
            DownloadTaskSpec t;
            t.url = QUrl(rewriteUrl(nativeObject.value("url").toString()));
            t.filePath = librariesRoot() + "/" + nativeObject.value("path").toString();
            t.sha1 = nativeObject.value("sha1").toString();
            t.label = QString("原生库: %1").arg(QFileInfo(t.filePath).fileName());
            if (t.url.isValid() && !t.filePath.isEmpty()) tasks.append(t);
        }
    }

    // 2c. Asset index + all asset objects
    const QJsonObject assetIndexObj = ctx.versionJson.value("assetIndex").toObject();
    QStringList assetHashes;
    QHash<QString, QString> assetPaths; // hash -> target path
    if (!assetIndexObj.isEmpty()) {
        const QString id = assetIndexObj.value("id").toString();
        const QString indexFile = assetsRoot() + "/indexes/" + id + ".json";

        DownloadTaskSpec t;
        t.url = QUrl(rewriteUrl(assetIndexObj.value("url").toString()));
        t.filePath = indexFile;
        t.sha1 = assetIndexObj.value("sha1").toString();
        t.label = QString("资源索引: %1").arg(id);
        if (t.url.isValid() && !t.filePath.isEmpty()) tasks.append(t);

        // The asset index must exist on disk before we can enqueue the
        // individual asset objects. On a fresh install it does not exist yet,
        // so fetch it synchronously here (it is tiny) and write it to disk.
        // Previously the index was only queued as a download task and objects
        // were parsed immediately from a file that did not exist, which is why
        // assets\objects\xx\xxxx files were never downloaded until first launch.
        if (!QFileInfo::exists(indexFile)) {
            QString indexErr;
            QByteArray indexBytes = getBytes(QUrl(rewriteUrl(assetIndexObj.value("url").toString())), &indexErr);
            if (!indexErr.isEmpty() || indexBytes.isEmpty()) {
                log(QString("资源索引预取失败: %1").arg(indexErr));
            } else {
                QDir().mkpath(QFileInfo(indexFile).absolutePath());
                QFile outFile(indexFile);
                if (outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    outFile.write(indexBytes);
                    outFile.close();
                    log(QString("已预取资源索引: %1").arg(id));
                }
            }
        }

        QFile af(indexFile);
        QJsonObject objects;
        if (af.open(QIODevice::ReadOnly)) {
            const QJsonObject index = QJsonDocument::fromJson(af.readAll()).object();
            objects = index.value("objects").toObject();
            af.close();
        }
        log(QString("资源索引 %1 含 %2 个对象").arg(id).arg(objects.size()));

        const bool useMirror = (m_configStore.config().mirrorSource != "mojang");
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QJsonObject item = it.value().toObject();
            const QString hash = item.value("hash").toString();
            if (hash.size() < 2) continue;
            const QString prefix = hash.left(2);
            const QString filePath = assetsRoot() + "/objects/" + prefix + "/" + hash;
            QString url = useMirror
                ? QString("https://bmclapi2.bangbang93.com/assets/%1/%2").arg(prefix, hash)
                : QString("https://resources.download.minecraft.net/%1/%2").arg(prefix, hash);

            DownloadTaskSpec t2;
            t2.url = QUrl(url);
            t2.filePath = filePath;
            t2.sha1 = hash;
            t2.label = QString("资源: %1").arg(hash.left(8));
            tasks.append(t2);
        }
    }

    // ---- 3. Filter already-present files -------------------------------
    qint64 skippedBytes = 0;
    const QList<DownloadTaskSpec> pending = DownloadTaskEngine::filterPending(tasks, verify, &skippedBytes);

    log(QString("下载计划: 共 %1 个文件, 需下载 %2 个, 跳过 %3 个")
            .arg(tasks.size()).arg(pending.size()).arg(tasks.size() - pending.size()));

    // ---- 4. Run the overlay + parallel engine --------------------------
    bool cancelled = false;
    bool success = true;
    QString engineError;

    if (!pending.isEmpty() && host) {
        DownloadOverlay overlay(host);
        overlay.attachTo(host);
        overlay.setTitle(QString("安装 Minecraft %1").arg(versionId));
        overlay.setPhaseLabel(QString("正在下载 %1 个文件...").arg(pending.size()));
        overlay.setSummaryText(QString("并发下载 · 已跳过 %1 个已存在的文件").arg(tasks.size() - pending.size()));
        overlay.setOverallRange(0);
        overlay.setOverallIndeterminate(true);

        DownloadTaskEngine engine(m_networkManager, this);
        engine.setConcurrency(8);

        overlay.setTaskTotal(pending.size());

        QEventLoop loop;
        QObject::connect(&overlay, &DownloadOverlay::cancelled, [&]() {
            cancelled = true;
            engine.cancel();
        });
        QObject::connect(&engine, &DownloadTaskEngine::overallRangeChanged,
                         [&overlay](qint64 total, qint64 done) {
            overlay.setOverallRange(total);
            overlay.setOverallValue(done);
            overlay.setOverallIndeterminate(total <= 0);
        });
        QObject::connect(&engine, &DownloadTaskEngine::overallProgress,
                         [&overlay](qint64 done, qint64 total) {
            overlay.setOverallValue(done);
            Q_UNUSED(total);
        });
        QObject::connect(&engine, &DownloadTaskEngine::taskStarted,
                         [&overlay, &pending](int index) {
            if (index >= 0 && index < pending.size()) {
                overlay.addTask(index, pending.at(index).label);
            }
        });
        QObject::connect(&engine, &DownloadTaskEngine::taskProgress,
                         [&overlay](int index, qint64 received, qint64 total) {
            overlay.setTaskProgress(index, received, total);
            if (total > 0) {
                overlay.setTaskStatus(index, QString("%1%").arg(static_cast<int>(received * 100 / total)));
            }
        });
        QObject::connect(&engine, &DownloadTaskEngine::taskFinished,
                         [&overlay](int index, bool ok, const QString &) {
            overlay.setTaskDone(index, ok);
        });
        QObject::connect(&engine, &DownloadTaskEngine::allFinished,
                         [&](bool ok, const QString &err) {
            success = ok;
            engineError = err;
            loop.quit();
        });

        engine.start(pending);
        loop.exec();
    }

    if (cancelled) {
        return failWith(QStringLiteral("操作已取消"));
    }
    if (!success) {
        return failWith(engineError.isEmpty() ? QStringLiteral("下载失败") : engineError);
    }

    // ---- 5. Extract natives & finalize ---------------------------------
    log(QStringLiteral("下载完成，正在解压原生库..."));
    if (!extractNatives(ctx, error)) {
        return false;
    }

    log(QString("版本安装完成: %1").arg(versionId));
    ensureVersionProfileStored(versionId, nullptr);
    return true;
}

bool LauncherCore::installClientJar(const VersionInstallContext &ctx, QString *error)
{
    const QJsonObject client = ctx.versionJson.value("downloads").toObject().value("client").toObject();
    const QString url = rewriteUrl(client.value("url").toString());
    const QString sha1 = client.value("sha1").toString();
    const QString file = ctx.versionDir + "/" + ctx.versionId + ".jar";
    log(QString("下载客户端 Jar: %1").arg(ctx.versionId));
    return downloadToFileWithProgress(QUrl(url), file, QString("客户端 Jar: %1").arg(ctx.versionId), error, sha1);
}

bool LauncherCore::rulesAllow(const QJsonArray &rules) const
{
    if (rules.isEmpty()) {
        return true;
    }

    bool allowed = false;
    for (const QJsonValue &value : rules) {
        const QJsonObject rule = value.toObject();
        bool matches = true;
        const QJsonObject os = rule.value("os").toObject();
        if (!os.isEmpty()) {
            const QString name = os.value("name").toString();
            if (!name.isEmpty() && name != osName()) {
                matches = false;
            }
        }
        if (rule.contains("features")) {
            const QJsonObject features = rule.value("features").toObject();
            for (auto it = features.begin(); it != features.end(); ++it) {
                if (it.value().toBool()) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches) {
            allowed = rule.value("action").toString("allow") == "allow";
        }
    }
    return allowed;
}

static bool isAllowedNativesJarForCurrentArch(const QString &fileName)
{
    if (!fileName.contains("-natives-", Qt::CaseInsensitive)) return true;
    const QString lower = fileName.toLower();
    if (!lower.contains("-windows-")) return true;
    const bool isArm64 = lower.contains("-arm64") || lower.contains("-aarch64");
    const bool isX86 = lower.contains("-x86") || lower.contains("-i686") || lower.contains("-32");
    const bool isX64 = lower.contains("-x64") || lower.contains("-amd64") || lower.contains("-x86_64");
    if (!isArm64 && !isX86 && !isX64) return true;
#if defined(Q_PROCESSOR_ARM_64)
    return isArm64;
#elif defined(Q_PROCESSOR_X86_32)
    return isX86;
#else
    return isX64;
#endif
}

static bool isModulePathAllowedJar(const QString &mavenCoords, const QString &fileName)
{
    const QString coords = mavenCoords.trimmed().toLower();
    const QString name = fileName.toLower();
    static const QSet<QString> kBlockedGaPrefixes = {
        "com.google.guava:failureaccess",
        "com.google.guava:listenablefuture",
        "com.google.errorprone:error_prone_annotations",
        "com.google.j2objc:j2objc-annotations",
        "com.google.code.findbugs:jsr305",
        "com.google.code.findbugs:annotations",
        "org.codehaus.mojo:animal-sniffer-annotations",
        "org.checkerframework:checker-qual",
        "org.checkerframework:checker-compat-qual"
    };
    for (const QString &prefix : kBlockedGaPrefixes) {
        if (coords.startsWith(prefix)) return false;
    }
    if (name.contains("failureaccess")) return false;
    if (name.contains("listenablefuture")) return false;
    if (name.contains("error_prone_annotations") || name.contains("errorprone")) return false;
    if (name.contains("j2objc")) return false;
    if (name.contains("jsr305")) return false;
    if (name.contains("animal-sniffer")) return false;
    if (name.contains("checker-qual") || name.contains("checker-compat")) return false;
    return true;
}

QString LauncherCore::nativeClassifier(const QJsonObject &library) const
{
    const QJsonObject natives = library.value("natives").toObject();
    QString classifier = natives.value(osName()).toString();
    classifier.replace("${arch}", "64");
    return classifier;
}

static QString mavenCoordsToPath(const QString &mavenCoords)
{
    const QStringList parts = mavenCoords.trimmed().split(':');
    if (parts.size() < 3) return QString();
    const QString group = parts.at(0);
    const QString artifact = parts.at(1);
    const QString version = parts.at(2);
    const QString classifier = parts.size() >= 4 ? parts.at(3) : QString();
    const QString groupPath = QString(group).replace('.', '/');
    QString fileName = artifact + "-" + version;
    if (!classifier.isEmpty()) fileName += "-" + classifier;
    fileName += ".jar";
    return groupPath + "/" + artifact + "/" + version + "/" + fileName;
}

static QString mavenBuildDownloadUrl(const QString &baseRepoUrl, const QString &mavenCoords)
{
    const QString path = mavenCoordsToPath(mavenCoords);
    if (path.isEmpty()) return QString();
    QString repo = baseRepoUrl;
    if (!repo.endsWith('/')) repo += '/';
    return repo + path;
}

bool LauncherCore::installLibraries(const VersionInstallContext &ctx, QString *error)
{
    const QJsonArray libraries = ctx.versionJson.value("libraries").toArray();

    int totalFiles = 0;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) {
            continue;
        }
        const QJsonObject downloads = library.value("downloads").toObject();
        const QJsonObject artifact = downloads.value("artifact").toObject();
        const QString coords = library.value("name").toString();
        const QString baseUrl = library.value("url").toString();

        if (!artifact.isEmpty()) {
            const QString path = librariesRoot() + "/" + artifact.value("path").toString();
            const QString fileName = QFileInfo(path).fileName();
            if (!isAllowedNativesJarForCurrentArch(fileName)) continue;
            totalFiles++;
        } else if (!coords.isEmpty() && downloads.isEmpty()) {
            const QString localPath = mavenCoordsToPath(coords);
            if (!localPath.isEmpty()) {
                const QString path = librariesRoot() + "/" + localPath;
                const QString fileName = QFileInfo(path).fileName();
                if (!isAllowedNativesJarForCurrentArch(fileName)) {
                    // skip artifact path, but still check classifiers
                } else {
                    totalFiles++;
                }
            }
        }

        const QString classifier = nativeClassifier(library);
        const QJsonObject nativeObject = downloads.value("classifiers").toObject().value(classifier).toObject();
        if (!nativeObject.isEmpty()) {
            const QString path = librariesRoot() + "/" + nativeObject.value("path").toString();
            const QString fileName = QFileInfo(path).fileName();
            if (!isAllowedNativesJarForCurrentArch(fileName)) continue;
            totalFiles++;
        } else if (!classifier.isEmpty() && !coords.isEmpty()) {
            const QString coordsWithClassifier = coords + ":" + classifier;
            const QString localPath = mavenCoordsToPath(coordsWithClassifier);
            if (!localPath.isEmpty()) {
                totalFiles++;
            }
        }
    }

    int currentFile = 0;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) {
            continue;
        }

        const QString coords = library.value("name").toString();
        const QString baseUrl = library.value("url").toString();
        const QString sha1 = library.value("sha1").toString();
        const QJsonObject downloads = library.value("downloads").toObject();
        const QJsonObject artifact = downloads.value("artifact").toObject();

        if (!artifact.isEmpty()) {
            const QString path = librariesRoot() + "/" + artifact.value("path").toString();
            const QString fileName = QFileInfo(path).fileName();
            if (!isAllowedNativesJarForCurrentArch(fileName)) goto handle_classifiers;
            currentFile++;
            emit progressUpdated(currentFile, totalFiles, QString("下载库: %1 (%2/%3)").arg(fileName).arg(currentFile).arg(totalFiles));

            const QString checksum = artifact.value("sha1").toString().isEmpty()
                                        ? sha1 : artifact.value("sha1").toString();
            const QString dlUrl = artifact.value("url").toString().isEmpty() && !baseUrl.isEmpty()
                                      ? mavenBuildDownloadUrl(baseUrl, coords)
                                      : artifact.value("url").toString();
            if (!downloadToFileWithProgress(QUrl(rewriteUrl(dlUrl)),
                                path,
                                QString("库: %1").arg(fileName),
                                error,
                                checksum)) {
                return false;
            }
        } else if (!coords.isEmpty() && downloads.isEmpty()) {
            // Only legacy name-only libraries (no downloads object) get a
            // synthesized main-jar download here. Libraries that ship only
            // classifier jars (e.g. lwjgl-platform) must not fetch a main jar.
            const QString localPath = mavenCoordsToPath(coords);
            if (!localPath.isEmpty()) {
                const QString path = librariesRoot() + "/" + localPath;
                const QString fileName = QFileInfo(path).fileName();
                if (isAllowedNativesJarForCurrentArch(fileName)) {
                    currentFile++;
                    emit progressUpdated(currentFile, totalFiles, QString("下载库: %1 (%2/%3)").arg(fileName).arg(currentFile).arg(totalFiles));
                    const QString effectiveRepo = !baseUrl.isEmpty()
                                              ? baseUrl
                                              : QStringLiteral("https://libraries.minecraft.net/");
                    const QString dlUrl = mavenBuildDownloadUrl(effectiveRepo, coords);
                    if (!dlUrl.isEmpty()) {
                        if (!downloadToFileWithProgress(QUrl(rewriteUrl(dlUrl)),
                                            path,
                                            QString("库: %1").arg(fileName),
                                            error,
                                            sha1)) {
                            return false;
                        }
                    }
                }
            }
        }

handle_classifiers:
        const QString classifier = nativeClassifier(library);
        const QJsonObject nativeObject = downloads.value("classifiers").toObject().value(classifier).toObject();
        if (!nativeObject.isEmpty()) {
            const QString path = librariesRoot() + "/" + nativeObject.value("path").toString();
            const QString fileName = QFileInfo(path).fileName();
            if (!isAllowedNativesJarForCurrentArch(fileName)) continue;
            currentFile++;
            emit progressUpdated(currentFile, totalFiles, QString("下载原生库: %1 (%2/%3)").arg(fileName).arg(currentFile).arg(totalFiles));

            if (!downloadToFileWithProgress(QUrl(rewriteUrl(nativeObject.value("url").toString())),
                                path,
                                QString("原生库: %1").arg(fileName),
                                error,
                                nativeObject.value("sha1").toString())) {
                return false;
            }
        } else if (!classifier.isEmpty() && !coords.isEmpty() && !baseUrl.isEmpty()) {
            const QString coordsWithClassifier = coords + ":" + classifier;
            const QString localPath = mavenCoordsToPath(coordsWithClassifier);
            if (!localPath.isEmpty()) {
                const QString path = librariesRoot() + "/" + localPath;
                const QString fileName = QFileInfo(path).fileName();
                if (isAllowedNativesJarForCurrentArch(fileName)) {
                    currentFile++;
                    emit progressUpdated(currentFile, totalFiles, QString("下载原生库: %1 (%2/%3)").arg(fileName).arg(currentFile).arg(totalFiles));
                    const QString dlUrl = mavenBuildDownloadUrl(baseUrl, coordsWithClassifier);
                    if (!downloadToFileWithProgress(QUrl(rewriteUrl(dlUrl)),
                                        path,
                                        QString("原生库: %1").arg(fileName),
                                        error,
                                        QString())) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool LauncherCore::installAssetIndex(const VersionInstallContext &ctx, QString *error)
{
    const QJsonObject assetIndex = ctx.versionJson.value("assetIndex").toObject();
    if (assetIndex.isEmpty()) {
        return true;
    }

    const QString id = assetIndex.value("id").toString();
    const QString file = assetsRoot() + "/indexes/" + id + ".json";
    return downloadToFileWithProgress(QUrl(rewriteUrl(assetIndex.value("url").toString())),
                          file,
                          QString("资源索引: %1").arg(id),
                          error,
                          assetIndex.value("sha1").toString());
}

bool LauncherCore::installAssets(const VersionInstallContext &ctx, QString *error)
{
    const QJsonObject assetIndex = ctx.versionJson.value("assetIndex").toObject();
    if (assetIndex.isEmpty()) {
        return true;
    }

    const QString indexFile = assetsRoot() + "/indexes/" + assetIndex.value("id").toString() + ".json";
    QFile file(indexFile);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = "无法读取资源索引";
        return false;
    }
    const QJsonObject index = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject objects = index.value("objects").toObject();
    
    const int totalObjects = objects.size();
    int currentObject = 0;

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        currentObject++;
        const QJsonObject item = it.value().toObject();
        const QString hash = item.value("hash").toString();
        if (hash.size() < 2) {
            continue;
        }
        const QString prefix = hash.left(2);
        const QString filePath = assetsRoot() + "/objects/" + prefix + "/" + hash;
        QString url = QString("https://resources.download.minecraft.net/%1/%2").arg(prefix, hash);
        if (m_configStore.config().mirrorSource != "mojang") {
            url = QString("https://bmclapi2.bangbang93.com/assets/%1/%2").arg(prefix, hash);
        }
        
        emit progressUpdated(currentObject, totalObjects, QString("下载资源: %1/%2").arg(currentObject).arg(totalObjects));
        if (!downloadToFileWithProgress(QUrl(url), filePath, QString("资源: %1").arg(hash.left(8)), error, hash)) {
            return false;
        }
    }
    return true;
}

bool LauncherCore::extractNatives(const VersionInstallContext &ctx, QString *error)
{
    const QString nativesDir = ctx.instanceDir + "/natives";
    ensureDirectory(nativesDir, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QJsonArray libraries = ctx.versionJson.value("libraries").toArray();
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) {
            continue;
        }

        const QString classifier = nativeClassifier(library);
        if (classifier.isEmpty()) {
            continue;
        }

        const QJsonObject nativeObject = library.value("downloads").toObject()
                                         .value("classifiers").toObject()
                                         .value(classifier).toObject();
        if (nativeObject.isEmpty()) {
            continue;
        }

        const QString archivePath = librariesRoot() + "/" + nativeObject.value("path").toString();
        if (!QFileInfo::exists(archivePath)) {
            continue;
        }
        log(QString("解压原生库: %1 -> %2")
                .arg(QDir::toNativeSeparators(archivePath),
                     QDir::toNativeSeparators(nativesDir)));
        QString unzipError;
        if (!ZipUtils::extractArchive(archivePath, nativesDir, &unzipError)) {
            *error = QString("原生库解压失败: %1").arg(unzipError);
            log(QString("原生库解压失败: %1").arg(QDir::toNativeSeparators(archivePath)));
            return false;
        }
    }
    log(QString("原生库解压完成: %1").arg(ctx.versionId));
    return true;
}

QString LauncherCore::javaExecutableName() const
{
    return "javaw.exe";
}

QString LauncherCore::javaLogExecutablePath(const QString &javaPath) const
{
    const QFileInfo info(javaPath);
    if (info.fileName().compare("javaw.exe", Qt::CaseInsensitive) != 0) {
        return javaPath;
    }

    const QString consoleJava = info.absolutePath() + "/java.exe";
    if (QFileInfo::exists(consoleJava)) {
        return QDir::toNativeSeparators(consoleJava);
    }
    return javaPath;
}

QString LauncherCore::locateBundledJava(const QString &versionId) const
{
    const QString runtimeRoot = appDataRoot() + "/java";
    if (!versionId.isEmpty()) {
        const QString direct = runtimeRoot + "/" + versionId + "/bin/" + javaExecutableName();
        if (QFileInfo::exists(direct)) {
            return QDir::toNativeSeparators(direct);
        }
    }

    QDirIterator it(runtimeRoot, QStringList() << javaExecutableName(), QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return QDir::toNativeSeparators(it.next());
    }
    return {};
}

int LauncherCore::detectJavaMajorFromPath(const QString &javaPath) const
{
    if (javaPath.isEmpty() || !QFileInfo::exists(javaPath)) {
        return 0;
    }
    QProcess process;
    process.start(javaLogExecutablePath(javaPath), {"-version"});
    if (!process.waitForFinished(5000)) {
        return 0;
    }
    const QString output = process.readAllStandardError() + process.readAllStandardOutput();
    static const QVector<int> knownVersions = {8, 11, 16, 17, 21, 22, 23, 24, 25};
    for (int v : knownVersions) {
        const QRegularExpression re(QString(R"((?:^|\D)%1(?:[\.\D]|$))").arg(v));
        if (re.match(output).hasMatch()) {
            return v;
        }
    }
    QRegularExpression versionRe(R"(version\s+"(\d+))");
    QRegularExpressionMatch match = versionRe.match(output);
    if (match.hasMatch()) {
        bool ok = false;
        int major = match.captured(1).toInt(&ok);
        if (ok && major > 0) {
            return major;
        }
    }
    return 0;
}

int LauncherCore::mapMinecraftVersionToJavaMajor(const QString &versionId) const
{
    if (versionId.isEmpty()) {
        return 8;
    }
    QRegularExpression re(R"(^(\d+)\.(\d+)(?:\.(\d+))?)");
    QRegularExpressionMatch match = re.match(versionId);
    if (!match.hasMatch()) {
        return 8;
    }
    bool okMajor = false, okMinor = false, okPatch = false;
    const int major = match.captured(1).toInt(&okMajor);
    const int minor = match.captured(2).toInt(&okMinor);
    const int patch = match.capturedLength(3) > 0 ? match.captured(3).toInt(&okPatch) : 0;
    if (!okMajor || !okMinor) {
        return 8;
    }
    if (major != 1) {
        if (major >= 21) return 21;
        if (major >= 17) return 17;
        return 8;
    }
    if (minor <= 16) {
        return 8;
    }
    if (minor == 17) {
        return 16;
    }
    if (minor >= 18 && minor <= 20) {
        return 17;
    }
    if (minor == 20) {
        return patch >= 5 ? 21 : 17;
    }
    if (minor >= 21) {
        return 21;
    }
    return 17;
}

bool LauncherCore::extractZipArchive(const QString &zipPath, const QString &destDir, QString *error) const
{
    ensureDirectory(destDir, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    QString unzipError;
    if (!ZipUtils::extractArchive(zipPath, destDir, &unzipError)) {
        if (error) {
            *error = QString("压缩包解压失败: %1").arg(unzipError);
        }
        return false;
    }
    return true;
}

int LauncherCore::requiredJavaMajor(const QJsonObject &versionJson) const
{
    const int fromManifest = versionJson.value("javaVersion").toObject().value("majorVersion").toInt(0);
    if (fromManifest > 0) {
        return fromManifest;
    }
    const QString versionId = versionJson.value("id").toString();
    return mapMinecraftVersionToJavaMajor(versionId);
}

bool LauncherCore::ensureJavaAvailable(int requiredMajor, QString *resolvedPath, QString *error)
{
    const QString configured = QDir::fromNativeSeparators(m_configStore.config().javaPath);
    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        const int detected = detectJavaMajorFromPath(configured);
        if (detected == requiredMajor) {
            *resolvedPath = QDir::toNativeSeparators(configured);
            log(QString("使用已配置 Java %1: %2").arg(requiredMajor).arg(*resolvedPath));
            return true;
        }
        log(QString("已配置 Java 版本不匹配(检测到%1, 需要%2): %3 - 跳过，继续查找")
                .arg(detected).arg(requiredMajor).arg(configured));
    }

    const QString autoJava = locateBundledJava(QString("java-%1-jre").arg(requiredMajor));
    if (!autoJava.isEmpty()) {
        *resolvedPath = autoJava;
        log(QString("使用自动安装 JRE %1: %2").arg(requiredMajor).arg(*resolvedPath));
        return true;
    }
    const QString autoJavaOld = locateBundledJava(QString("java-%1").arg(requiredMajor));
    if (!autoJavaOld.isEmpty()) {
        *resolvedPath = autoJavaOld;
        log(QString("使用自动安装 Java %1: %2").arg(requiredMajor).arg(*resolvedPath));
        return true;
    }

    for (const QString &candidate : discoverJavaExecutables()) {
        const int detected = detectJavaMajorFromPath(candidate);
        if (detected == requiredMajor) {
            *resolvedPath = candidate;
            log(QString("探测到可用 JRE %1: %2").arg(requiredMajor).arg(QDir::toNativeSeparators(candidate)));
            return true;
        }
    }

    log(QString("未找到 JRE %1，准备自动安装").arg(requiredMajor));
    *resolvedPath = installJavaRuntime(requiredMajor, error);
    return !resolvedPath->isEmpty();
}

QJsonObject LauncherCore::fetchJavaRuntimeManifest(QString *error)
{
    const QString endpoint =
        "https://api.adoptium.net/v3/assets/latest/%1/hotspot?architecture=x64&heap_size=normal&image_type=jre&jvm_impl=hotspot&os=windows&vendor=eclipse";
    const QJsonDocument doc = getJson(QUrl(endpoint.arg(QString::number(17))), error);
    if (!error->isEmpty() || doc.isNull()) {
        return {};
    }
    return doc.object();
}

QString LauncherCore::installJavaRuntime(int requiredMajor, QString *error)
{
    QStringList endpoints;
    endpoints << QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot?architecture=x64&heap_size=normal&image_type=jre&jvm_impl=hotspot&os=windows&vendor=eclipse").arg(requiredMajor);
    endpoints << QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot?architecture=x64&heap_size=normal&image_type=jre&jvm_impl=hotspot&os=windows&vendor=adoptium").arg(requiredMajor);

    QString url;
    QString checksum;
    QString manifestError;

    log(QString("自动安装 JRE %1 (Adoptium Temurin)").arg(requiredMajor));

    for (const QString &endpoint : endpoints) {
        manifestError.clear();
        const QJsonDocument doc = getJson(QUrl(endpoint), &manifestError);
        if (!manifestError.isEmpty()) {
            log(QString("JRE %1 清单获取失败: %2 端点: %3").arg(requiredMajor).arg(manifestError).arg(endpoint));
            continue;
        }
        const QJsonArray array = doc.array();
        if (array.isEmpty()) {
            log(QString("JRE %1 清单为空，端点: %2").arg(requiredMajor).arg(endpoint));
            continue;
        }
        const QJsonObject pkg = array.first().toObject().value("binary").toObject().value("package").toObject();
        url = pkg.value("link").toString();
        checksum = pkg.value("checksum").toString();
        const QString checksumAlgo = pkg.value("checksum_algorithm").toString().toLower();
        if (!checksumAlgo.isEmpty() && checksumAlgo != "sha-256" && checksumAlgo != "sha256") {
            log(QString("JRE %1 checksum算法(%2)不是SHA-256，本次跳过校验").arg(requiredMajor).arg(checksumAlgo));
            checksum.clear();
        }
        if (!url.isEmpty()) break;
    }

    if (url.isEmpty()) {
        const QString fallbackUrl = QString("https://api.adoptium.net/v3/binary/latest/%1/ga/windows/x64/jre/hotspot/normal/eclipse").arg(requiredMajor);
        log(QString("使用回退直接下载 URL: %1").arg(fallbackUrl));
        url = fallbackUrl;
        checksum.clear();
    }

    const QString zipPath = appDataRoot() + QString("/java/java-%1-jre.zip").arg(requiredMajor);
    const QString installDir = appDataRoot() + QString("/java/java-%1-jre").arg(requiredMajor);
    log(QString("下载 JRE %1: %2").arg(requiredMajor).arg(url));
    if (!downloadToFile(QUrl(url), zipPath, error, QString(), checksum)) {
        return {};
    }

    log(QString("解压 JRE %1: %2 -> %3")
            .arg(requiredMajor)
            .arg(QDir::toNativeSeparators(zipPath))
            .arg(QDir::toNativeSeparators(installDir)));
    if (!extractZipArchive(zipPath, installDir, error)) {
        log(QString("JRE 解压失败: %1").arg(QDir::toNativeSeparators(zipPath)));
        return {};
    }

    QDirIterator it(installDir, QStringList() << javaExecutableName(), QDir::Files, QDirIterator::Subdirectories);
    if (!it.hasNext()) {
        *error = "解压完成，但未找到 javaw.exe";
        return {};
    }
    const QString java = QDir::toNativeSeparators(it.next());
    m_configStore.config().javaPath = java;
    saveConfig(nullptr);
    log(QString("JRE %1 安装完成: %2").arg(requiredMajor).arg(java));
    return java;
}

QString LauncherCore::installJavaRuntimeWithDialog(int requiredMajor, QString *error, QWidget *parentWidget)
{
    QStringList endpoints;
    endpoints << QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot?architecture=x64&heap_size=normal&image_type=jre&jvm_impl=hotspot&os=windows&vendor=eclipse").arg(requiredMajor);
    endpoints << QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot?architecture=x64&heap_size=normal&image_type=jre&jvm_impl=hotspot&os=windows&vendor=adoptium").arg(requiredMajor);

    log(QString("自动安装 JRE %1 (Adoptium Temurin)").arg(requiredMajor));

    // Embedded (in-window) overlay, consistent with the launch overlay.
    QWidget *jreHost = parentWidget ? parentWidget->window() : nullptr;
    if (!jreHost) jreHost = QApplication::activeWindow();
    DownloadProgressDialog dialog(jreHost,
                                  QString("下载 JRE %1").arg(requiredMajor),
                                  QString("正在获取 JRE %1 下载信息...").arg(requiredMajor));
    dialog.showOverallProgress(true);
    dialog.setOverallBusy(true);
    dialog.setStepLabel("准备中...");
    dialog.setBusy(true);
    dialog.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool cancelled = false;
    QObject::connect(&dialog, &DownloadProgressDialog::cancelled, [&cancelled]() {
        cancelled = true;
    });

    auto pumpEvents = [&]() -> bool {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (cancelled || dialog.isCancelled()) {
            if (error) *error = "操作已取消";
            return false;
        }
        return true;
    };

    if (!pumpEvents()) return {};

    QString url;
    QString checksum;
    QString manifestError;

    for (const QString &endpoint : endpoints) {
        if (!pumpEvents()) return {};
        manifestError.clear();
        const QJsonDocument doc = getJson(QUrl(endpoint), &manifestError);
        if (!manifestError.isEmpty()) {
            log(QString("JRE %1 清单获取失败: %2").arg(requiredMajor).arg(manifestError));
            continue;
        }
        const QJsonArray array = doc.array();
        if (array.isEmpty()) continue;
        const QJsonObject pkg = array.first().toObject().value("binary").toObject().value("package").toObject();
        url = pkg.value("link").toString();
        checksum = pkg.value("checksum").toString();
        const QString checksumAlgo = pkg.value("checksum_algorithm").toString().toLower();
        if (!checksumAlgo.isEmpty() && checksumAlgo != "sha-256" && checksumAlgo != "sha256") {
            log(QString("JRE %1 checksum算法(%2)不是SHA-256，本次跳过校验").arg(requiredMajor).arg(checksumAlgo));
            checksum.clear();
        }
        if (!url.isEmpty()) break;
    }

    if (url.isEmpty()) {
        url = QString("https://api.adoptium.net/v3/binary/latest/%1/ga/windows/x64/jre/hotspot/normal/eclipse").arg(requiredMajor);
        log(QString("使用回退直接下载 URL: %1").arg(url));
        checksum.clear();
    }

    if (!pumpEvents()) return {};

    const QString zipPath = appDataRoot() + QString("/java/java-%1-jre.zip").arg(requiredMajor);
    const QString installDir = appDataRoot() + QString("/java/java-%1-jre").arg(requiredMajor);
    log(QString("下载 JRE %1: %2").arg(requiredMajor).arg(url));

    dialog.setStepLabel(QString("步骤 1/2: 下载 JRE %1").arg(requiredMajor));
    dialog.setOverallBusy(false);
    dialog.setOverallProgress(0, 2);
    dialog.setStatus("正在下载 Java 运行时...");
    if (!pumpEvents()) return {};

    {
        bool downloadDone = false;
        bool downloadOk = false;
        QString downloadError;

        DownloadManager *manager = new DownloadManager(m_networkManager, this);
        QObject::connect(&dialog, &DownloadProgressDialog::cancelled, manager, &DownloadManager::cancel);
        QObject::connect(manager, &DownloadManager::downloadProgress,
                         [&dialog](qint64 received, qint64 total) {
            if (total > 0) {
                const qint64 pct = total > 0 ? received * 100 / total : 0;
                dialog.setProgress(static_cast<int>(pct > 100 ? 100 : (pct < 0 ? 0 : pct)), 100);
                const QString recStr = (received < 1024 * 1024) ? QString::number(received / 1024.0, 'f', 1) + " KB"
                                       : QString::number(received / (1024.0 * 1024.0), 'f', 2) + " MB";
                const QString totStr = (total < 1024 * 1024) ? QString::number(total / 1024.0, 'f', 1) + " KB"
                                       : QString::number(total / (1024.0 * 1024.0), 'f', 2) + " MB";
                dialog.setStatus(QString("%1 / %2").arg(recStr, totStr));
            } else {
                dialog.setStatus(QString("已下载: %1 MB").arg(received / (1024 * 1024)));
            }
        });
        QObject::connect(manager, &DownloadManager::downloadFinished,
                         [&](bool success, const QString &err) {
            downloadDone = true;
            downloadOk = success;
            downloadError = err;
        });

        manager->downloadFileSilent(QUrl(url), zipPath, checksum);

        while (!downloadDone) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (cancelled || dialog.isCancelled()) {
                manager->deleteLater();
                if (error) *error = "操作已取消";
                return {};
            }
        }

        manager->deleteLater();

        if (!downloadOk) {
            if (error && !downloadError.contains("下载已取消")) {
                *error = downloadError;
            }
            log(QString("JRE %1 下载失败: %2").arg(requiredMajor).arg(downloadError));
            return {};
        }
    }

    dialog.setOverallProgress(1, 2);
    dialog.setStepLabel("步骤 2/2: 解压 JRE 运行时");
    dialog.setStatus("正在解压...");
    dialog.setBusy(true);
    if (!pumpEvents()) return {};

    log(QString("解压 JRE %1: %2 -> %3")
            .arg(requiredMajor)
            .arg(QDir::toNativeSeparators(zipPath))
            .arg(QDir::toNativeSeparators(installDir)));

    QString extractError;
    if (!extractZipArchive(zipPath, installDir, &extractError)) {
        if (error) *error = extractError;
        log(QString("JRE 解压失败: %1").arg(QDir::toNativeSeparators(zipPath)));
        return {};
    }

    QDirIterator it(installDir, QStringList() << javaExecutableName(), QDir::Files, QDirIterator::Subdirectories);
    if (!it.hasNext()) {
        *error = "解压完成，但未找到 javaw.exe";
        return {};
    }
    const QString java = QDir::toNativeSeparators(it.next());
    m_configStore.config().javaPath = java;
    saveConfig(nullptr);
    log(QString("JRE %1 安装完成: %2").arg(requiredMajor).arg(java));

    dialog.setOverallProgress(2, 2);
    dialog.setProgress(100, 100);
    dialog.setStatus("JRE 安装完成！");
    dialog.setStepLabel("安装完成");
    dialog.setBusy(false);
    if (dialog.isEmbedded()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
        dialog.hide();
    } else {
        QTimer::singleShot(500, &dialog, &QDialog::accept);
        dialog.exec();
    }

    return java;
}

QString LauncherCore::instanceDirectoryForVersion(const QString &versionId) const
{
    const LauncherVersionProfile profile = effectiveVersionProfile(versionId);
    if (profile.versionIsolation && !versionId.isEmpty()) {
        return instancesRoot() + "/" + versionId;
    }
    return QDir::fromNativeSeparators(m_configStore.config().minecraftDir);
}

static QString libraryGroupArtifactKey(const QString &mavenCoords)
{
    const QStringList parts = mavenCoords.trimmed().split(':');
    if (parts.size() < 2) return mavenCoords.trimmed();
    return parts.at(0) + ":" + parts.at(1);
}

static QString libraryGaClassifierKey(const QString &mavenCoords)
{
    const QStringList parts = mavenCoords.trimmed().split(':');
    if (parts.size() < 2) return mavenCoords.trimmed();
    const QString classifier = parts.size() >= 4 ? parts.at(3) : QString();
    return parts.at(0) + ":" + parts.at(1) + ":" + classifier;
}

QStringList LauncherCore::buildClasspath(const VersionInstallContext &ctx) const
{
    QStringList classpath;
    const QJsonArray libraries = ctx.versionJson.value("libraries").toArray();
    QString forgeGaVersionDir;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) {
            continue;
        }
        QString libPath;
        const QJsonObject artifact = library.value("downloads").toObject().value("artifact").toObject();
        if (!artifact.isEmpty()) {
            libPath = artifact.value("path").toString();
        } else {
            const QString coords = library.value("name").toString();
            libPath = mavenCoordsToPath(coords);
            const QString classifier = nativeClassifier(library);
            if (!classifier.isEmpty() && !coords.contains(':' + classifier + ':')) {
                const QString coordsWithClassifier = coords + ":" + classifier;
                const QString classifiedPath = mavenCoordsToPath(coordsWithClassifier);
                const QString classifiedFull = QDir::toNativeSeparators(librariesRoot() + "/" + classifiedPath);
                if (QFileInfo::exists(classifiedFull)) {
                    classpath << classifiedFull;
                    continue;
                }
            }
        }
        if (!libPath.isEmpty()) {
            const QString full = QDir::toNativeSeparators(librariesRoot() + "/" + libPath);
            const QString fileName = QFileInfo(full).fileName();
            if (!isAllowedNativesJarForCurrentArch(fileName)) {
                continue;
            }
            if (!QFileInfo::exists(full)) continue;
            classpath << full;
            const QString coords = library.value("name").toString();
            const QString ga = libraryGroupArtifactKey(coords);
            if (ga == "net.minecraftforge:forge") {
                const QFileInfo fi(full);
                forgeGaVersionDir = fi.absolutePath();
            }
        }
    }
    if (!forgeGaVersionDir.isEmpty() && QFileInfo::exists(forgeGaVersionDir)) {
        QDir forgeDir(forgeGaVersionDir);
        const QStringList jars = forgeDir.entryList(QStringList() << "*.jar", QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &jar : jars) {
            const QString full = QDir::toNativeSeparators(forgeDir.absoluteFilePath(jar));
            if (!classpath.contains(full)) {
                classpath << full;
            }
        }
    }
    classpath << QDir::toNativeSeparators(ctx.versionDir + "/" + ctx.versionId + ".jar");
    return classpath;
}

QStringList LauncherCore::buildModulePath(const VersionInstallContext &ctx) const
{
    QStringList result;
    const QJsonArray libraries = ctx.versionJson.value("libraries").toArray();
    QSet<QString> seenGaClassifier;
    QSet<QString> seenAutoModulePrefix;
    const QString versionJar = QDir::toNativeSeparators(ctx.versionDir + "/" + ctx.versionId + ".jar");
    result << versionJar;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) {
            continue;
        }
        QString libPath;
        const QJsonObject artifact = library.value("downloads").toObject().value("artifact").toObject();
        if (!artifact.isEmpty()) {
            libPath = artifact.value("path").toString();
        } else {
            const QString coords = library.value("name").toString();
            libPath = mavenCoordsToPath(coords);
        }
        if (libPath.isEmpty()) continue;
        const QString path = QDir::toNativeSeparators(librariesRoot() + "/" + libPath);
        if (!QFileInfo::exists(path)) continue;
        const QString fileName = QFileInfo(path).fileName();
        if (!isAllowedNativesJarForCurrentArch(fileName)) continue;
        const QString coords = library.value("name").toString();
        if (!isModulePathAllowedJar(coords, fileName)) continue;
        const QString key = libraryGaClassifierKey(coords);
        if (!key.isEmpty()) {
            if (seenGaClassifier.contains(key)) continue;
            seenGaClassifier.insert(key);
        }
        QString stem = fileName;
        const qsizetype dotJar = stem.lastIndexOf(".jar", -1, Qt::CaseInsensitive);
        if (dotJar > 0) stem.truncate(dotJar);
        QString autoModPrefix;
        static const QRegularExpression versionedRe(QStringLiteral("^(.+?)(?:-\\d.*)$"));
        const QRegularExpressionMatch m = versionedRe.match(stem);
        autoModPrefix = m.hasMatch() ? m.captured(1) : stem;
        if (!autoModPrefix.isEmpty()) {
            if (seenAutoModulePrefix.contains(autoModPrefix)) continue;
            seenAutoModulePrefix.insert(autoModPrefix);
        }
        result << path;
    }
    result.removeDuplicates();
    return result;
}

QJsonArray LauncherCore::expandArgumentArray(const QJsonValue &value) const
{
    if (value.isString()) {
        return QJsonArray{ value.toString() };
    }
    return value.toArray();
}

QString LauncherCore::substituteArgument(const QString &arg,
                                         const VersionInstallContext &ctx,
                                         const Account &account,
                                         const QString &javaPath,
                                         const QString &classpath,
                                         const QString &modulePath,
                                         bool useModulePathForClasspathPlaceholder) const
{
    QString value = arg;
    const QString clientId = account.clientId.isEmpty() ? "KillLauncher" : account.clientId;
    value.replace("${auth_player_name}", account.username);
    value.replace("${version_name}", ctx.versionId);
    value.replace("${game_directory}", QDir::toNativeSeparators(ctx.instanceDir));
    value.replace("${assets_root}", QDir::toNativeSeparators(assetsRoot()));
    value.replace("${assets_index_name}", ctx.versionJson.value("assets").toString());
    value.replace("${game_assets}", QDir::toNativeSeparators(assetsRoot()));
    value.replace("${auth_uuid}", account.uuid);
    value.replace("${auth_access_token}", account.accessToken);
    value.replace("${user_type}", account.type == Account::Type::Microsoft ? "msa" : "offline");
    value.replace("${version_type}", ctx.versionJson.value("type").toString("release"));
    value.replace("${natives_directory}", QDir::toNativeSeparators(ctx.instanceDir + "/natives"));
    value.replace("${launcher_name}", "Kill");
    value.replace("${launcher_version}", "0.1.0");
    const QString cpForPlaceholder = useModulePathForClasspathPlaceholder ? modulePath : classpath;
    value.replace("${classpath}", cpForPlaceholder);
    value.replace("${classpath_separator}", classpathSeparator());
    value.replace("${library_directory}", QDir::toNativeSeparators(librariesRoot()));
    value.replace("${auth_xuid}", account.xuid);
    value.replace("${clientid}", clientId);
    value.replace("${user_properties}", "{}");
    value.replace("${resolution_width}", "1280");
    value.replace("${resolution_height}", "720");
    if (!modulePath.isEmpty()) {
        value.replace("${module_path}", modulePath);
    } else {
        value.replace("${module_path}", classpath);
    }
    Q_UNUSED(javaPath);
    return value;
}

QStringList LauncherCore::buildJvmArguments(const VersionInstallContext &ctx,
                                            const Account &account,
                                            const QString &javaPath,
                                            const QString &classpath,
                                            const QString &modulePath,
                                            const LauncherVersionProfile &profile) const
{
    QStringList args;
    args << QString("-Xms%1M").arg(profile.minMemoryMb)
         << QString("-Xmx%1M").arg(profile.maxMemoryMb);
    args << QProcess::splitCommand(profile.javaArgs);

    const bool useModulePath = versionJsonRequestsModulePath(ctx.versionJson);
    const QJsonObject arguments = ctx.versionJson.value("arguments").toObject();
    const QJsonArray jvmArgs = arguments.value("jvm").toArray();
    for (const QJsonValue &entry : jvmArgs) {
        if (entry.isString()) {
            args << substituteArgument(entry.toString(), ctx, account, javaPath, classpath, modulePath, useModulePath);
            continue;
        }
        const QJsonObject object = entry.toObject();
        if (!rulesAllow(object.value("rules").toArray())) {
            continue;
        }
        for (const QJsonValue &item : expandArgumentArray(object.value("value"))) {
            args << substituteArgument(item.toString(), ctx, account, javaPath, classpath, modulePath, useModulePath);
        }
    }

    if (profile.softwareOpenGL == 1
        && !args.contains("-Dorg.lwjgl.opengl.Display.allowSoftwareOpenGL=true")) {
        args << "-Dorg.lwjgl.opengl.Display.allowSoftwareOpenGL=true";
    }
    args << "-Dfml.ignoreInvalidMinecraftCertificates=true"
         << "-Dfml.ignorePatchDiscrepancies=true";
    const QString clientJar = QDir::toNativeSeparators(versionsRoot() + "/" + ctx.versionId + "/" + ctx.versionId + ".jar");
    args << QString("-Dminecraft.client.jar=%1").arg(clientJar);

    // LWJGL 2 (used by old versions) loads its native DLLs from java.library.path.
    // Old version JSONs do not add this JVM option, so ensure the extracted
    // natives directory is on the library path; otherwise the game crashes with
    // "UnsatisfiedLinkError: no lwjgl in java.library.path" (white screen).
    bool hasLibraryPathArg = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i).startsWith("-Djava.library.path=")) {
            hasLibraryPathArg = true;
            break;
        }
    }
    if (!hasLibraryPathArg) {
        const QString nativesDir = QDir::toNativeSeparators(ctx.instanceDir + "/natives");
        args << QString("-Djava.library.path=%1").arg(nativesDir);
    }

    // Legacy versions (pre-1.6, e.g. 1.1) have no "arguments.jvm" block, so the
    // -cp/-classpath option is never added. Ensure the classpath is always passed
    // to the JVM; otherwise java cannot load the main class (e.g. launchwrapper).
    bool hasClasspathArg = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == "-cp" || a == "-classpath" || a == "--class-path") {
            hasClasspathArg = true;
            break;
        }
    }
    if (!hasClasspathArg && !classpath.isEmpty()) {
        args << "-cp" << classpath;
    }

    return args;
}

QStringList LauncherCore::buildGameArguments(const VersionInstallContext &ctx,
                                            const Account &account,
                                            const LauncherVersionProfile &profile) const
{
    QStringList args;
    const QJsonObject arguments = ctx.versionJson.value("arguments").toObject();
    const QJsonArray gameArgs = arguments.value("game").toArray();
    if (!gameArgs.isEmpty()) {
        for (const QJsonValue &entry : gameArgs) {
            if (entry.isString()) {
                args << entry.toString();
                continue;
            }
            const QJsonObject object = entry.toObject();
            if (!rulesAllow(object.value("rules").toArray())) {
                continue;
            }
            for (const QJsonValue &item : expandArgumentArray(object.value("value"))) {
                args << item.toString();
            }
        }
    } else {
        const QString legacy = ctx.versionJson.value("minecraftArguments").toString();
        for (const QString &item : QProcess::splitCommand(legacy)) {
            args << item;
        }
    }

    if (!profile.extraGameArgs.trimmed().isEmpty()) {
        args << QProcess::splitCommand(profile.extraGameArgs);
    }
    return args;
}

bool LauncherCore::versionJsonRequestsModulePath(const QJsonObject &versionJson) const
{
    return false;
}

bool LauncherCore::argumentListContainsFlag(const QStringList &args, const QString &flag) const
{
    for (const QString &a : args) {
        if (a.compare(flag, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool LauncherCore::startTrackedMinecraftProcess(const QString &javaPath,
                                                const QStringList &arguments,
                                                const QString &workingDirectory,
                                                const QString &versionId,
                                                bool softwareOpenGL,
                                                QString *error)
{
    if (m_gameProcess && m_gameProcess->state() != QProcess::NotRunning) {
        if (error) {
            *error = "已有正在运行的 Minecraft 进程";
        }
        log(QString("拒绝启动版本 %1: 已有游戏进程在运行").arg(versionId));
        return false;
    }

    if (m_gameProcess) {
        m_gameProcess->deleteLater();
        m_gameProcess = nullptr;
    }

    m_gameStdoutBuffer.clear();
    m_gameStderrBuffer.clear();

    m_gameProcess = new QProcess(this);
    m_gameProcess->setWorkingDirectory(workingDirectory);
    m_gameProcess->setProcessChannelMode(QProcess::SeparateChannels);

    // When software OpenGL is enabled (per-version or global setting) and a Mesa
    // opengl32.dll ships next to java.exe (machines without hardware OpenGL,
    // e.g. Windows 7 RTM in VMware), help Mesa pick the software rasterizer and
    // report a modern GL version so LWJGL 2 does not abort with
    // "Pixel format not accelerated".
    if (softwareOpenGL) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!env.contains("GALLIUM_DRIVER")) env.insert("GALLIUM_DRIVER", "llvmpipe");
        if (!env.contains("MESA_GL_VERSION_OVERRIDE")) env.insert("MESA_GL_VERSION_OVERRIDE", "3.3");
        if (!env.contains("MESA_GLSL_VERSION_OVERRIDE")) env.insert("MESA_GLSL_VERSION_OVERRIDE", "330");
        if (!env.contains("LIBGL_ALWAYS_SOFTWARE")) env.insert("LIBGL_ALWAYS_SOFTWARE", "1");
        m_gameProcess->setProcessEnvironment(env);
    }

    const QString launchJava = javaLogExecutablePath(javaPath);
    QString mainClassForLog = "<unknown>";
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        if (a == "--module" && i + 1 < arguments.size()) {
            mainClassForLog = QString("--module %1").arg(arguments.at(i + 1));
            break;
        }
        if (!a.startsWith('-')) {
            mainClassForLog = a;
            break;
        }
    }
    log(QString("准备启动 Minecraft %1").arg(versionId));
    log(QString("工作目录: %1").arg(QDir::toNativeSeparators(workingDirectory)));
    log(QString("Java 可执行文件: %1").arg(QDir::toNativeSeparators(launchJava)));
    {
        // Report whether a Mesa opengl32.dll sits next to the real java.exe,
        // so software-OpenGL setup problems are visible in the log.
        const QString javaDir = QFileInfo(javaPath).absolutePath();
        const QString mesaGl = javaDir + "/opengl32.dll";
        const QString mesaGallium = javaDir + "/libgallium_wgl.dll";
        if (QFileInfo::exists(mesaGl)) {
            log(QString("[OpenGL] Java dir has opengl32.dll: %1").arg(QDir::toNativeSeparators(mesaGl)));
            log(QString("[OpenGL] libgallium_wgl.dll present: %1").arg(QFileInfo::exists(mesaGallium) ? QStringLiteral("yes") : QStringLiteral("NO (Mesa needs it too)")));
        } else {
            log(QString("[OpenGL] opengl32.dll not found in java dir, using system OpenGL. dir: %1").arg(QDir::toNativeSeparators(javaDir)));
        }
    }
    log(QString("入口: %1").arg(mainClassForLog));

    connect(m_gameProcess, &QProcess::started, this, [this, versionId]() {
        if (!m_gameProcess) {
            return;
        }
        log(QString("Minecraft 进程已启动: 版本 %1, PID=%2")
                .arg(versionId)
                .arg(m_gameProcess->processId()));
    });

    connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_gameProcess) {
            return;
        }
        flushProcessLogBuffer("Java STDOUT", m_gameStdoutBuffer, m_gameProcess->readAllStandardOutput());
    });

    connect(m_gameProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_gameProcess) {
            return;
        }
        flushProcessLogBuffer("Java STDERR", m_gameStderrBuffer, m_gameProcess->readAllStandardError());
    });

    connect(m_gameProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (!m_gameProcess) {
            return;
        }
        log(QString("Minecraft 进程错误: %1 (%2)")
                .arg(processErrorToString(processError), m_gameProcess->errorString()));
    });

    connect(m_gameProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, versionId](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!m_gameProcess) {
                    return;
                }
                flushProcessLogBuffer("Java STDOUT", m_gameStdoutBuffer, QByteArray(), true);
                flushProcessLogBuffer("Java STDERR", m_gameStderrBuffer, QByteArray(), true);

                if (exitStatus == QProcess::CrashExit) {
                    log(QString("Minecraft 已崩溃: 版本 %1, exitCode=%2").arg(versionId).arg(exitCode));
                } else if (exitCode != 0) {
                    log(QString("Minecraft 异常退出: 版本 %1, exitCode=%2").arg(versionId).arg(exitCode));
                } else {
                    log(QString("Minecraft 正常退出: 版本 %1").arg(versionId));
                }
                emit gameExited(exitCode);
            });

    m_gameProcess->start(launchJava, arguments);
    if (!m_gameProcess->waitForStarted(10000)) {
        const QString processError = m_gameProcess->errorString();
        log(QString("Minecraft 启动失败: %1").arg(processError));
        if (error) {
            *error = processError.isEmpty() ? "进程启动失败" : processError;
        }
        m_gameProcess->deleteLater();
        m_gameProcess = nullptr;
        return false;
    }

    return true;
}

bool LauncherCore::resolveInheritance(QJsonObject &versionJson, QString *error) const
{
    if (!versionJson.contains("inheritsFrom")) {
        return true;
    }

    const QString parentId = versionJson.value("inheritsFrom").toString();
    const QString parentFile = versionsRoot() + "/" + parentId + "/" + parentId + ".json";
    
    if (!QFileInfo::exists(parentFile)) {
        *error = QString("找不到父版本: %1").arg(parentId);
        return false;
    }

    QFile file(parentFile);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QString("无法读取父版本文件: %1").arg(parentId);
        return false;
    }

    QJsonObject parentJson = QJsonDocument::fromJson(file.readAll()).object();
    
    if (!resolveInheritance(parentJson, error)) {
        return false;
    }

    QJsonObject merged = parentJson;

    for (const QString key : {"id", "type", "mainClass", "minecraftArguments", "assetIndex", "assets", "downloads", "javaVersion"}) {
        if (versionJson.contains(key)) {
            merged.insert(key, versionJson.value(key));
        }
    }

    if (versionJson.contains("libraries")) {
        QJsonArray parentLibs = parentJson.value("libraries").toArray();
        QJsonArray childLibs = versionJson.value("libraries").toArray();
        QSet<QString> seenCoords;
        QSet<QString> seenGaClassifier;
        QHash<QString, qsizetype> gaClassifierIndex;
        QJsonArray mergedLibs;

        auto pushLib = [&](const QJsonValue &v, bool fromChild) {
            const QJsonObject lib = v.toObject();
            const QString coords = lib.value("name").toString().trimmed();
            if (coords.isEmpty()) return;
            const QString key = libraryGaClassifierKey(coords);
            if (seenCoords.contains(coords)) return;
            if (!key.isEmpty() && seenGaClassifier.contains(key)) {
                if (fromChild) {
                    const auto it = gaClassifierIndex.constFind(key);
                    if (it != gaClassifierIndex.constEnd()) {
                        mergedLibs.replace(it.value(), v);
                        return;
                    }
                }
                return;
            }
            seenCoords.insert(coords);
            if (!key.isEmpty()) {
                seenGaClassifier.insert(key);
                gaClassifierIndex.insert(key, mergedLibs.size());
            }
            mergedLibs.append(v);
        };

        for (const QJsonValue &v : parentLibs) pushLib(v, false);
        for (const QJsonValue &v : childLibs) pushLib(v, true);
        merged.insert("libraries", mergedLibs);
    }

    const bool childHasArgs = versionJson.contains("arguments");
    const bool childHasLegacyArgs = versionJson.contains("minecraftArguments");
    const bool parentHasArgs = parentJson.contains("arguments");

    if (childHasArgs) {
        QJsonObject mergedArgs = parentHasArgs ? parentJson.value("arguments").toObject() : QJsonObject();
        const QJsonObject childArgs = versionJson.value("arguments").toObject();

        auto mergeArgArray = [&](const QString &key) {
            if (!childArgs.contains(key)) return;
            QJsonArray mergedArr;
            QSet<QString> seenStr;
            auto pushArr = [&](const QJsonArray &arr) {
                for (const QJsonValue &entry : arr) {
                    if (entry.isString()) {
                        const QString s = entry.toString();
                        if (!s.isEmpty()) {
                            if (seenStr.contains(s)) continue;
                            seenStr.insert(s);
                        }
                    }
                    mergedArr.append(entry);
                }
            };
            if (mergedArgs.contains(key)) pushArr(mergedArgs.value(key).toArray());
            pushArr(childArgs.value(key).toArray());
            mergedArgs.insert(key, mergedArr);
        };

        mergeArgArray("game");
        mergeArgArray("jvm");
        merged.insert("arguments", mergedArgs);

        if (merged.contains("minecraftArguments") && mergedArgs.contains("game")) {
            merged.remove("minecraftArguments");
        }
    } else if (parentHasArgs && !childHasLegacyArgs) {
        merged.insert("arguments", parentJson.value("arguments").toObject());
    }

    versionJson = merged;
    return true;
}

bool LauncherCore::ensureLaunchAssetsAvailable(const VersionInstallContext &ctx, QString *error)
{
    const bool verify = shouldVerify();
    const QJsonObject versionJson = ctx.versionJson;
    const bool useMirror = (m_configStore.config().mirrorSource != "mojang");

    const auto checkFile = [&](const QString &filePath, const QString &expectedSha1) -> bool {
        const QFileInfo fi(filePath);
        if (!fi.exists()) return false;
        if (!verify || expectedSha1.isEmpty()) return true;
        return verifySha1(filePath, expectedSha1);
    };

    // Helper: emit per-file progress to the outer dialog, then download if needed.
    auto ensureFile = [&](const QUrl &url, const QString &filePath, const QString &sha1,
                          const QString &label) -> bool {
        if (checkFile(filePath, sha1)) return true;
        emit progressStarted(label);
        emit progressUpdated(0, 100, label);
        const bool ok = downloadToFile(url, filePath, error, verify ? sha1 : QString());
        if (ok) {
            emit progressUpdated(100, 100, label);
            emit progressFinished(label);
        }
        return ok;
    };

    const QJsonObject client = versionJson.value("downloads").toObject().value("client").toObject();
    if (!client.isEmpty()) {
        const QString filePath = ctx.versionDir + "/" + ctx.versionId + ".jar";
        if (!ensureFile(QUrl(rewriteUrl(client.value("url").toString())), filePath,
                        client.value("sha1").toString(),
                        QString("客户端 Jar: %1").arg(ctx.versionId))) {
            if (error && error->isEmpty()) *error = QString("客户端 Jar 丢失: %1").arg(ctx.versionId);
            return false;
        }
    }

    const QJsonArray libraries = versionJson.value("libraries").toArray();
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) continue;
        const QJsonObject downloads = library.value("downloads").toObject();

        const QJsonObject artifact = downloads.value("artifact").toObject();
        if (!artifact.isEmpty()) {
            const QString filePath = librariesRoot() + "/" + artifact.value("path").toString();
            const QString fileName = QFileInfo(filePath).fileName();
            if (isAllowedNativesJarForCurrentArch(fileName)) {
                if (!ensureFile(QUrl(rewriteUrl(artifact.value("url").toString())), filePath,
                                artifact.value("sha1").toString(),
                                QString("库: %1").arg(fileName))) {
                    if (error && error->isEmpty()) *error = QString("库文件丢失: %1").arg(fileName);
                    return false;
                }
            }
        }

        const QString classifier = nativeClassifier(library);
        const QJsonObject nativeObject = downloads.value("classifiers").toObject().value(classifier).toObject();
        if (!nativeObject.isEmpty()) {
            const QString filePath = librariesRoot() + "/" + nativeObject.value("path").toString();
            const QString fileName = QFileInfo(filePath).fileName();
            if (isAllowedNativesJarForCurrentArch(fileName)) {
                if (!ensureFile(QUrl(rewriteUrl(nativeObject.value("url").toString())), filePath,
                                nativeObject.value("sha1").toString(),
                                QString("原生库: %1").arg(fileName))) {
                    if (error && error->isEmpty()) *error = QString("原生库文件丢失: %1").arg(fileName);
                    return false;
                }
            }
        }
    }

    const QJsonObject assetIndexObj = versionJson.value("assetIndex").toObject();
    if (!assetIndexObj.isEmpty()) {
        const QString id = assetIndexObj.value("id").toString();
        const QString indexPath = assetsRoot() + "/indexes/" + id + ".json";
        if (!ensureFile(QUrl(rewriteUrl(assetIndexObj.value("url").toString())), indexPath,
                        assetIndexObj.value("sha1").toString(),
                        QString("资源索引: %1").arg(id))) {
            if (error && error->isEmpty()) *error = QString("资源索引丢失: %1").arg(id);
            return false;
        }

        QFile af(indexPath);
        if (!af.open(QIODevice::ReadOnly)) {
            if (error) *error = QString("无法读取资源索引: %1").arg(id);
            return false;
        }
        const QJsonObject index = QJsonDocument::fromJson(af.readAll()).object();
        af.close();
        const QJsonObject objects = index.value("objects").toObject();
        const QString objectsRoot = assetsRoot() + "/objects";
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QJsonObject meta = it.value().toObject();
            const QString hash = meta.value("hash").toString();
            if (hash.size() < 2) continue;
            const QString filePath = objectsRoot + "/" + hash.left(2) + "/" + hash;
            if (checkFile(filePath, hash)) continue;
            const QString url = useMirror
                ? QString("https://bmclapi2.bangbang93.com/assets/%1/%2").arg(hash.left(2), hash)
                : QString("https://resources.download.minecraft.net/%1/%2").arg(hash.left(2), hash);
            if (!ensureFile(QUrl(rewriteUrl(url)), filePath, hash, QString("资源: %1").arg(hash.left(8)))) {
                if (error && error->isEmpty()) *error = QString("资源对象丢失: %1").arg(hash);
                return false;
            }
        }
    }
    return true;
}

bool LauncherCore::ensureLaunchAssetsAvailableWithDialog(const VersionInstallContext &ctx,
                                                         QString *error,
                                                         QWidget *parentWidget)
{
    const bool verify = shouldVerify();
    const QJsonObject versionJson = ctx.versionJson;

    int totalItems = 0;
    const QJsonObject client = versionJson.value("downloads").toObject().value("client").toObject();
    if (!client.isEmpty()) totalItems++;

    const QJsonArray libraries = versionJson.value("libraries").toArray();
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!rulesAllow(library.value("rules").toArray())) continue;
        const QJsonObject downloads = library.value("downloads").toObject();
        const QJsonObject artifact = downloads.value("artifact").toObject();
        if (!artifact.isEmpty()) {
            const QString filePath = librariesRoot() + "/" + artifact.value("path").toString();
            if (isAllowedNativesJarForCurrentArch(QFileInfo(filePath).fileName())) totalItems++;
        }
        const QString classifier = nativeClassifier(library);
        const QJsonObject nativeObject = downloads.value("classifiers").toObject().value(classifier).toObject();
        if (!nativeObject.isEmpty()) {
            const QString filePath = librariesRoot() + "/" + nativeObject.value("path").toString();
            if (isAllowedNativesJarForCurrentArch(QFileInfo(filePath).fileName())) totalItems++;
        }
    }

    const QJsonObject assetIndexObj = versionJson.value("assetIndex").toObject();
    int totalAssetObjects = 0;
    if (!assetIndexObj.isEmpty()) {
        const QString id = assetIndexObj.value("id").toString();
        const QString indexPath = assetsRoot() + "/indexes/" + id + ".json";
        QFile af(indexPath);
        if (af.open(QIODevice::ReadOnly)) {
            const QJsonObject index = QJsonDocument::fromJson(af.readAll()).object();
            totalAssetObjects = index.value("objects").toObject().size();
        }
    }
    if (!assetIndexObj.isEmpty()) totalItems++;
    totalItems += totalAssetObjects;

    DownloadProgressDialog dialog("校验与修复游戏文件",
                                  "正在检查客户端、库与资源文件...",
                                  parentWidget);
    dialog.showOverallProgress(true);
    dialog.setOverallBusy(false);
    dialog.setOverallProgress(0, totalItems);
    dialog.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool cancelled = false;
    bool currentDone = false;
    bool currentOk = false;
    QString currentError;
    QObject::connect(&dialog, &DownloadProgressDialog::cancelled, [&cancelled]() { cancelled = true; });

    auto pumpEvents = [&]() -> bool {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (cancelled || dialog.isCancelled()) {
            if (error) *error = "操作已取消";
            return false;
        }
        return true;
    };

    auto downloadWithDialog = [&](const QUrl &url, const QString &filePath, const QString &sha1,
                                   const QString &label,
                                   const QList<QPair<QString, QString>> &headers = {}) -> bool {
        if (QFileInfo::exists(filePath) && (!verify || sha1.isEmpty() || verifySha1(filePath, sha1))) {
            log(QString("跳过校验，文件已通过: %1").arg(QDir::toNativeSeparators(filePath)));
            dialog.setProgress(100, 100);
            return true;
        }

        currentDone = false;
        currentOk = false;
        currentError.clear();
        DownloadManager *manager = new DownloadManager(m_networkManager, this);
        QObject::connect(&dialog, &DownloadProgressDialog::cancelled, manager, &DownloadManager::cancel);
        QObject::connect(manager, &DownloadManager::downloadProgress,
                         [&dialog](qint64 received, qint64 total) {
            if (total > 0) {
                dialog.setProgress(static_cast<int>(received), static_cast<int>(total));
                dialog.setStatus(QString("已下载: %1 / %2 MB")
                    .arg(received / (1024 * 1024))
                    .arg(total / (1024 * 1024)));
            }
        });
        QObject::connect(manager, &DownloadManager::downloadFinished,
                         [&](bool ok, const QString &errStr) {
            currentOk = ok;
            currentError = errStr;
            currentDone = true;
        });
        const QString checksum = verify ? sha1 : QString();
        log(QString("开始下载 %1: %2 <- %3").arg(label, QDir::toNativeSeparators(filePath), rewriteUrl(url.toString())));
        manager->downloadFileSilent(QUrl(rewriteUrl(url.toString())), filePath, checksum, headers);
        while (!currentDone) {
            if (!pumpEvents()) {
                manager->cancel();
                return false;
            }
            QThread::msleep(10);
        }
        manager->deleteLater();
        if (!currentOk) {
            if (error) *error = currentError.isEmpty() ? QString("下载失败: %1").arg(label) : currentError;
            log(QString("下载失败 %1: %2").arg(label, *error));
            return false;
        }
        log(QString("下载完成 %1: %2").arg(label, QDir::toNativeSeparators(filePath)));
        dialog.setProgress(100, 100);
        return true;
    };

    auto checkFile = [&](const QString &filePath, const QString &expectedSha1) -> bool {
        const QFileInfo fi(filePath);
        if (!fi.exists()) return false;
        if (!verify || expectedSha1.isEmpty()) return true;
        return verifySha1(filePath, expectedSha1);
    };

    int currentStep = 0;
    auto bumpStep = [&]() {
        currentStep++;
        dialog.setOverallProgress(currentStep, totalItems);
    };

    dialog.setBusy(false);

    if (!client.isEmpty()) {
        const QString filePath = ctx.versionDir + "/" + ctx.versionId + ".jar";
        const QString sha1 = client.value("sha1").toString();
        dialog.setStepLabel(QString("步骤 %1/%2: 检查客户端 Jar").arg(currentStep + 1).arg(totalItems));
        dialog.setStatus(QString("客户端: %1").arg(ctx.versionId));
        if (!pumpEvents()) return false;
        if (!checkFile(filePath, sha1)) {
            if (!downloadWithDialog(QUrl(rewriteUrl(client.value("url").toString())), filePath, sha1,
                                     QString("客户端 Jar %1").arg(ctx.versionId))) {
                return false;
            }
        } else {
            dialog.setProgress(100, 100);
        }
        bumpStep();
        if (!pumpEvents()) return false;
    }

    {
        const int totalLibsInBatch = std::max(1, totalItems - (assetIndexObj.isEmpty() ? 1 : 2) - totalAssetObjects);
        dialog.setStepLabel(QString("步骤 %1/%2: 检查依赖库").arg(currentStep + 1).arg(totalItems));
        int idx = 0;
        for (const QJsonValue &value : libraries) {
            const QJsonObject library = value.toObject();
            if (!rulesAllow(library.value("rules").toArray())) continue;
            const QJsonObject downloads = library.value("downloads").toObject();

            const QJsonObject artifact = downloads.value("artifact").toObject();
            if (!artifact.isEmpty()) {
                const QString filePath = librariesRoot() + "/" + artifact.value("path").toString();
                const QString fileName = QFileInfo(filePath).fileName();
                if (isAllowedNativesJarForCurrentArch(fileName)) {
                    idx++;
                    const QString sha1 = artifact.value("sha1").toString();
                    dialog.setStatus(QString("库: %1 (%2)").arg(fileName).arg(idx));
                    if (!pumpEvents()) return false;
                    if (!checkFile(filePath, sha1)) {
                        if (!downloadWithDialog(QUrl(rewriteUrl(artifact.value("url").toString())), filePath, sha1,
                                                 QString("库文件 %1").arg(fileName))) {
                            return false;
                        }
                    }
                    bumpStep();
                }
            }

            const QString classifier = nativeClassifier(library);
            const QJsonObject nativeObject = downloads.value("classifiers").toObject().value(classifier).toObject();
            if (!nativeObject.isEmpty()) {
                const QString filePath = librariesRoot() + "/" + nativeObject.value("path").toString();
                const QString fileName = QFileInfo(filePath).fileName();
                if (isAllowedNativesJarForCurrentArch(fileName)) {
                    idx++;
                    const QString sha1 = nativeObject.value("sha1").toString();
                    dialog.setStatus(QString("原生库: %1 (%2)").arg(fileName).arg(idx));
                    if (!pumpEvents()) return false;
                    if (!checkFile(filePath, sha1)) {
                        if (!downloadWithDialog(QUrl(rewriteUrl(nativeObject.value("url").toString())), filePath, sha1,
                                                 QString("原生库 %1").arg(fileName))) {
                            return false;
                        }
                    }
                    bumpStep();
                }
            }
        }
        Q_UNUSED(totalLibsInBatch);
    }
    if (!pumpEvents()) return false;

    if (!assetIndexObj.isEmpty()) {
        const QString id = assetIndexObj.value("id").toString();
        const QString indexPath = assetsRoot() + "/indexes/" + id + ".json";
        const QString indexSha1 = assetIndexObj.value("sha1").toString();
        dialog.setStepLabel(QString("步骤 %1/%2: 检查资源索引").arg(currentStep + 1).arg(totalItems));
        dialog.setStatus(QString("索引: %1").arg(id));
        if (!pumpEvents()) return false;
        if (!checkFile(indexPath, indexSha1)) {
            if (!downloadWithDialog(QUrl(rewriteUrl(assetIndexObj.value("url").toString())), indexPath, indexSha1,
                                     QString("资源索引 %1").arg(id))) {
                return false;
            }
        } else {
            dialog.setProgress(100, 100);
        }
        bumpStep();

        QFile af(indexPath);
        if (!af.open(QIODevice::ReadOnly)) {
            if (error) *error = QString("无法读取资源索引: %1").arg(id);
            return false;
        }
        const QJsonObject index = QJsonDocument::fromJson(af.readAll()).object();
        const QJsonObject objects = index.value("objects").toObject();
        const QString objectsRoot = assetsRoot() + "/objects";
        const int total = objects.size();
        int processed = 0;
        int missingDownloaded = 0;
        dialog.setStepLabel(QString("步骤 %1/%2: 检查资源对象 (%3)")
            .arg(currentStep + 1).arg(totalItems).arg(total));
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QJsonObject meta = it.value().toObject();
            const QString hash = meta.value("hash").toString();
            if (hash.size() < 2) { processed++; bumpStep(); continue; }
            const QString filePath = objectsRoot + "/" + hash.left(2) + "/" + hash;
            processed++;
            if (processed % 256 == 0) {
                dialog.setStatus(QString("资源: %1/%2 已处理, %3 已补下")
                    .arg(processed).arg(total).arg(missingDownloaded));
                if (!pumpEvents()) return false;
            }
            if (!checkFile(filePath, hash)) {
                dialog.setStatus(QString("资源缺失: %1 正在下载...").arg(hash.left(10)));
                if (!pumpEvents()) return false;
                const QString url = QStringLiteral("https://resources.download.minecraft.net/%1/%2").arg(hash.left(2), hash);
                if (!downloadWithDialog(QUrl(rewriteUrl(url)), filePath, verify ? hash : QString(),
                                         QString("资源对象 %1").arg(hash.left(10)))) {
                    return false;
                }
                missingDownloaded++;
            }
            bumpStep();
        }
    }

    dialog.setOverallProgress(totalItems, totalItems);
    dialog.setProgress(100, 100);
    dialog.setStepLabel("文件校验完成");
    dialog.setStatus("所有文件完整");
    dialog.setBusy(false);
    if (dialog.isEmbedded()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
        dialog.hide();
    } else {
        QTimer::singleShot(250, &dialog, &QDialog::accept);
        dialog.exec();
    }
    return true;
}

bool LauncherCore::launchSelectedVersion(QString *error)
{
    LauncherConfig &config = m_configStore.config();
    if (config.selectedVersion.isEmpty()) {
        *error = "请先选择版本";
        return false;
    }

    if (!ensureVersionProfileStored(config.selectedVersion, error)) {
        return false;
    }
    const LauncherVersionProfile profile = effectiveVersionProfile(config.selectedVersion);

    const QString baseVersionDir = versionsRoot() + "/" + config.selectedVersion;
    const QString vanillaJson = baseVersionDir + "/" + config.selectedVersion + ".json";
    if (!QFileInfo::exists(vanillaJson)) {
        if (!installVanillaVersion(config.selectedVersion, error)) {
            return false;
        }
    }

    const QString activeLoaderJson = resolveActiveLoaderJson(config.selectedVersion);
    QString loadError;
    QJsonObject resolvedJson = loadVersionJsonWithOverride(config.selectedVersion, activeLoaderJson, true, &loadError);
    if (resolvedJson.isEmpty()) {
        *error = loadError;
        return false;
    }

    VersionInstallContext ctx;
    ctx.versionId = config.selectedVersion;
    ctx.versionDir = baseVersionDir;
    ctx.instanceDir = versionInstanceDirectory(config.selectedVersion);
    ctx.versionJson = resolvedJson;

    ensureDirectory(ctx.instanceDir, error);
    if (!error->isEmpty()) {
        return false;
    }

    if (!installLibraries(ctx, error)) {
        return false;
    }

    if (!extractNatives(ctx, error)) {
        return false;
    }

    if (!ensureLaunchAssetsAvailable(ctx, error)) {
        return false;
    }

    Account account;
    if (config.selectedAccountIndex >= 0 && config.selectedAccountIndex < config.accounts.size()) {
        account = config.accounts.at(config.selectedAccountIndex);
    } else {
        account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        account.type = Account::Type::Offline;
        account.username = "Player";
        account.uuid = makeOfflineUuid(account.username);
        account.accessToken = account.uuid;
    }

    if (!ensureMicrosoftAccountUsable(account, error)) {
        return false;
    }
    if (config.selectedAccountIndex >= 0 && config.selectedAccountIndex < config.accounts.size()) {
        config.accounts[config.selectedAccountIndex] = account;
        saveConfig(nullptr);
    }

    QString javaPath;
    if (!profile.javaPath.isEmpty() && QFileInfo::exists(profile.javaPath)) {
        javaPath = QDir::toNativeSeparators(profile.javaPath);
    } else if (!ensureJavaAvailable(requiredJavaMajor(ctx.versionJson), &javaPath, error)) {
        return false;
    }

    const QString classpath = buildClasspath(ctx).join(classpathSeparator());
    const bool useModulePath = versionJsonRequestsModulePath(ctx.versionJson);
    const QString modulePath = useModulePath ? buildModulePath(ctx).join(classpathSeparator()) : classpath;
    QStringList args = buildJvmArguments(ctx, account, javaPath, classpath, modulePath, profile);
    if (!argumentListContainsFlag(args, "--module")) {
        const QString mainClass = ctx.versionJson.value("mainClass").toString();
        if (!mainClass.isEmpty()) args << mainClass;
        {
            const QString cpJoin = buildClasspath(ctx).join(classpathSeparator());
            const bool hasLaunchWrapper = cpJoin.contains("launchwrapper", Qt::CaseInsensitive);
            const QString loaderJson = resolveActiveLoaderJson(ctx.versionId);
            log(QString("[Launch] mainClass=%1").arg(mainClass));
            log(QString("[Launch] selectedLoaderJson=%1").arg(loaderJson.isEmpty() ? QStringLiteral("(vanilla)") : loaderJson));
            log(QString("[Launch] classpath has launchwrapper=%1").arg(hasLaunchWrapper ? QStringLiteral("yes") : QStringLiteral("NO")));
            log(QString("[Launch] classpath library count=%1").arg(buildClasspath(ctx).size()));
        }
    }
    const auto substituteCommon = [&](const QString &s) -> QString {
        return substituteArgument(s, ctx, account, QString(), classpath, modulePath, useModulePath);
    };
    QStringList gameArgs = buildGameArguments(ctx, account, profile);
    for (int i = 0; i < gameArgs.size(); ++i) {
        gameArgs[i] = substituteCommon(gameArgs[i]);
    }
    args << gameArgs;

    log(QString("启动 Minecraft: %1%2")
            .arg(config.selectedVersion,
                 activeLoaderJson.isEmpty() ? QString() : QString(" (%1)").arg(activeLoaderJson)));
    log(QString("Java: %1").arg(javaPath));
    log(QString("账户: %1").arg(account.displayName()));
    if (!startTrackedMinecraftProcess(javaPath, args, ctx.instanceDir, config.selectedVersion, profile.softwareOpenGL == 1, error)) {
        return false;
    }
    return true;
}

bool LauncherCore::launchSelectedVersionWithDialog(QString *error, QWidget *parentWidget)
{
    LauncherConfig &config = m_configStore.config();
    if (config.selectedVersion.isEmpty()) {
        *error = "请先选择版本";
        return false;
    }

    if (!ensureVersionProfileStored(config.selectedVersion, error)) {
        return false;
    }
    const LauncherVersionProfile profile = effectiveVersionProfile(config.selectedVersion);

    // Embedded (in-window) overlay on the main window, HMCL-style.
    QWidget *host = parentWidget ? parentWidget->window() : nullptr;
    if (!host) host = QApplication::activeWindow();
    DownloadProgressDialog dialog(host, "启动游戏", "正在准备启动 Minecraft...");
    dialog.showOverallProgress(true);
    dialog.setOverallBusy(true);
    dialog.setStepLabel("准备中...");
    dialog.setBusy(true);
    dialog.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    bool cancelled = false;
    QObject::connect(&dialog, &DownloadProgressDialog::cancelled, [&cancelled]() {
        cancelled = true;
    });

    // Show per-file download progress inside the launch window (HMCL-style):
    // library and asset completion downloads report through these signals.
    QObject::connect(this, &LauncherCore::progressStarted, &dialog,
                     [&dialog](const QString &message) {
        dialog.setStatus(message);
    });
    QObject::connect(this, &LauncherCore::progressUpdated, &dialog,
                     [&dialog](int value, int maximum, const QString &message) {
        dialog.setProgress(value, maximum);
        dialog.setStatus(message);
    });
    QObject::connect(this, &LauncherCore::progressFinished, &dialog,
                     [&dialog](const QString &message) {
        dialog.setStatus(message);
    });

    auto pumpEvents = [&]() -> bool {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (cancelled || dialog.isCancelled()) {
            if (error) *error = "操作已取消";
            return false;
        }
        return true;
    };

    if (!pumpEvents()) return false;

    const int totalSteps = 8;
    int currentStep = 0;
    dialog.setOverallBusy(false);
    dialog.setOverallProgress(0, totalSteps);

    currentStep++;
    dialog.setStepLabel(QString("步骤 %1/%2: 检查版本").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在检查版本文件...");
    if (!pumpEvents()) return false;

    const QString baseVersionDir = versionsRoot() + "/" + config.selectedVersion;
    const QString vanillaJsonPath = baseVersionDir + "/" + config.selectedVersion + ".json";
    if (!QFileInfo::exists(vanillaJsonPath)) {
        *error = QString("版本「%1」未安装，请先在「下载」或「管理版本」页面安装该版本。").arg(config.selectedVersion);
        log(QString("启动失败: %1").arg(*error));
        return false;
    }

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 加载版本信息").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在读取版本信息...");
    if (!pumpEvents()) return false;

    const QString activeLoaderJson = resolveActiveLoaderJson(config.selectedVersion);
    QString loadError;
    QJsonObject resolvedJson = loadVersionJsonWithOverride(config.selectedVersion, activeLoaderJson, true, &loadError);
    if (resolvedJson.isEmpty()) {
        *error = loadError;
        log(QString("启动失败: %1").arg(*error));
        return false;
    }

    VersionInstallContext ctx;
    ctx.versionId = config.selectedVersion;
    ctx.versionDir = baseVersionDir;
    ctx.instanceDir = versionInstanceDirectory(config.selectedVersion);
    ctx.versionJson = resolvedJson;

    ensureDirectory(ctx.instanceDir, error);
    if (!error->isEmpty()) {
        log(QString("启动失败: %1").arg(*error));
        return false;
    }

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 确保依赖库").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在检查并下载缺失的模组加载器依赖库...");
    dialog.setBusy(true);
    if (!pumpEvents()) return false;

    if (!installLibraries(ctx, error)) {
        log(QString("启动失败: 依赖库下载失败 - %1").arg(*error));
        return false;
    }
    dialog.setBusy(false);

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 准备原生库").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在解压原生库...");
    dialog.setBusy(true);
    if (!pumpEvents()) return false;

    if (!extractNatives(ctx, error)) {
        return false;
    }
    dialog.setBusy(false);

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 校验资源完整性").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在检查客户端、依赖库和资源对象...");
    dialog.setBusy(true);
    if (!pumpEvents()) return false;

    if (!ensureLaunchAssetsAvailable(ctx, error)) {
        return false;
    }
    dialog.setBusy(false);

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 验证账户").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在验证账户信息...");
    if (!pumpEvents()) return false;

    Account account;
    if (config.selectedAccountIndex >= 0 && config.selectedAccountIndex < config.accounts.size()) {
        account = config.accounts.at(config.selectedAccountIndex);
    } else {
        account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        account.type = Account::Type::Offline;
        account.username = "Player";
        account.uuid = makeOfflineUuid(account.username);
        account.accessToken = account.uuid;
    }

    if (account.type == Account::Type::Microsoft) {
        dialog.setStatus("正在刷新 Microsoft 账户...");
    }
    if (!ensureMicrosoftAccountUsable(account, error)) {
        return false;
    }
    if (config.selectedAccountIndex >= 0 && config.selectedAccountIndex < config.accounts.size()) {
        config.accounts[config.selectedAccountIndex] = account;
        saveConfig(nullptr);
    }

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 准备 Java").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在检查 Java 环境...");
    if (!pumpEvents()) return false;

    QString javaPath;
    const int requiredMajor = requiredJavaMajor(ctx.versionJson);

    const QString configured = QDir::fromNativeSeparators(profile.javaPath);
    const QString autoJava = locateBundledJava(QString("java-" + QString::number(requiredMajor)));
    bool javaFound = false;

    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        javaPath = QDir::toNativeSeparators(configured);
        javaFound = true;
    } else if (!autoJava.isEmpty()) {
        javaPath = autoJava;
        javaFound = true;
    } else {
        for (const QString &candidate : discoverJavaExecutables()) {
            QProcess process;
            process.start(candidate, {"-version"});
            process.waitForFinished(5000);
            const QString output = process.readAllStandardError() + process.readAllStandardOutput();
            if (output.contains(QString::number(requiredMajor))) {
                javaPath = candidate;
                javaFound = true;
                break;
            }
        }
    }

    if (!javaFound) {
        dialog.setStatus(QString("未找到 Java %1，正在下载...").arg(requiredMajor));
        javaPath = installJavaRuntimeWithDialog(requiredMajor, error, &dialog);
        if (javaPath.isEmpty()) {
            return false;
        }
        dialog.show();
        dialog.raise();
        dialog.activateWindow();
    }

    currentStep++;
    dialog.setOverallProgress(currentStep, totalSteps);
    dialog.setStepLabel(QString("步骤 %1/%2: 启动游戏").arg(currentStep).arg(totalSteps));
    dialog.setStatus("正在启动 Minecraft...");
    dialog.setBusy(true);
    if (!pumpEvents()) return false;

    const QString classpath = buildClasspath(ctx).join(classpathSeparator());
    const bool useModulePath = versionJsonRequestsModulePath(ctx.versionJson);
    const QString modulePath = useModulePath ? buildModulePath(ctx).join(classpathSeparator()) : classpath;
    QStringList args = buildJvmArguments(ctx, account, javaPath, classpath, modulePath, profile);
    if (!argumentListContainsFlag(args, "--module")) {
        const QString mainClass = ctx.versionJson.value("mainClass").toString();
        if (!mainClass.isEmpty()) args << mainClass;
    }
    const auto substituteCommon = [&](const QString &s) -> QString {
        return substituteArgument(s, ctx, account, QString(), classpath, modulePath, useModulePath);
    };
    QStringList gameArgs = buildGameArguments(ctx, account, profile);
    for (int i = 0; i < gameArgs.size(); ++i) {
        gameArgs[i] = substituteCommon(gameArgs[i]);
    }
    args << gameArgs;

    log(QString("启动 Minecraft: %1%2")
            .arg(config.selectedVersion,
                 activeLoaderJson.isEmpty() ? QString() : QString(" (%1)").arg(activeLoaderJson)));
    log(QString("Java: %1").arg(javaPath));
    log(QString("账户: %1").arg(account.displayName()));
    if (!startTrackedMinecraftProcess(javaPath, args, ctx.instanceDir, config.selectedVersion, profile.softwareOpenGL == 1, error)) {
        return false;
    }

    dialog.setOverallProgress(totalSteps, totalSteps);
    dialog.setProgress(100, 100);
    dialog.setStatus("游戏已启动！");
    dialog.setStepLabel("启动完成");
    dialog.setBusy(false);
    if (dialog.isEmbedded()) {
        // Embedded overlay: keep the success state visible briefly, then hide.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 800);
        dialog.hide();
    } else {
        QTimer::singleShot(800, &dialog, &QDialog::accept);
        dialog.exec();
    }

    return true;
}

QJsonArray LauncherCore::searchModrinth(const QString &query,
                                        const QString &gameVersion,
                                        const QString &projectType,
                                        QString *error)
{
    QUrl url("https://api.modrinth.com/v2/search");
    QUrlQuery q;
    q.addQueryItem("query", query);
    q.addQueryItem("limit", "30");
    q.addQueryItem("index", "relevance");
    QString facets = QString("[[\"project_type:%1\"]").arg(projectType.isEmpty() ? "mod" : projectType);
    if (!gameVersion.isEmpty()) {
        facets += QString(",[\"versions:%1\"]").arg(gameVersion);
    }
    facets += "]";
    q.addQueryItem("facets", facets);
    url.setQuery(q);

    const QJsonDocument doc = getJson(url, error);
    return doc.object().value("hits").toArray();
}

QJsonArray LauncherCore::searchCurseForge(const QString &query,
                                          const QString &gameVersion,
                                          QString *error)
{
    if (m_configStore.config().curseForgeApiKey.isEmpty()) {
        *error = "CurseForge 查询需要先填写 API Key";
        return {};
    }

    QUrl url("https://api.curseforge.com/v1/mods/search");
    QUrlQuery q;
    q.addQueryItem("gameId", "432");
    q.addQueryItem("classId", "6");
    q.addQueryItem("searchFilter", query);
    if (!gameVersion.isEmpty()) {
        q.addQueryItem("gameVersion", gameVersion);
    }
    q.addQueryItem("pageSize", "20");
    url.setQuery(q);

    const QJsonDocument doc = getJson(url, error);
    if (!error->isEmpty()) {
        return {};
    }

    const QByteArray bytes = getBytes(
        url,
        error,
        {{"x-api-key", m_configStore.config().curseForgeApiKey}});
    if (!error->isEmpty()) {
        return {};
    }
    return QJsonDocument::fromJson(bytes).object().value("data").toArray();
}

QString LauncherCore::targetDirectoryForType(const QString &targetType) const
{
    const QString versionId = m_configStore.config().selectedVersion;
    if (targetType == "resourcepack") {
        return versionInstanceDirectory(versionId) + "/resourcepacks";
    }
    if (targetType == "modpack") {
        return modpacksRoot();
    }
    return versionModsDirectory(versionId);
}

bool LauncherCore::recordManagedFile(const QString &relativePath, const QString &targetType, QString *error)
{
    LauncherConfig &config = m_configStore.config();
    QStringList *targetList = &config.managedMods;
    if (targetType == "resourcepack") {
        targetList = &config.managedResourcePacks;
    } else if (targetType == "modpack") {
        targetList = &config.managedModpacks;
    }

    if (!targetList->contains(relativePath)) {
        targetList->append(relativePath);
    }
    const bool ok = saveConfig(error);
    if (ok) {
        log(QString("已记录受管文件 [%1]: %2").arg(targetType, QDir::toNativeSeparators(relativePath)));
    } else if (error) {
        log(QString("记录受管文件失败 [%1]: %2 -> %3")
                .arg(targetType,
                     QDir::toNativeSeparators(relativePath),
                     *error));
    }
    return ok;
}

bool LauncherCore::downloadModrinthProject(const QString &projectId,
                                           const QString &gameVersion,
                                           const QString &loader,
                                           const QString &targetType,
                                           QString *error)
{
    log(QString("开始下载 Modrinth 项目: projectId=%1, gameVersion=%2, loader=%3, targetType=%4")
            .arg(projectId, gameVersion, loader, targetType));
    QUrl url(QString("https://api.modrinth.com/v2/project/%1/version").arg(projectId));
    QUrlQuery query;
    if (!gameVersion.isEmpty()) {
        query.addQueryItem("game_versions", QString("[\"%1\"]").arg(gameVersion));
    }
    if (!loader.isEmpty()) {
        query.addQueryItem("loaders", QString("[\"%1\"]").arg(loader));
    }
    url.setQuery(query);

    const QJsonDocument doc = getJson(url, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QJsonArray versions = doc.array();
    if (versions.isEmpty()) {
        *error = "未找到匹配的 Modrinth 版本";
        return false;
    }

    const QJsonObject version = versions.first().toObject();
    const QJsonArray files = version.value("files").toArray();
    if (files.isEmpty()) {
        *error = "Modrinth 项目没有可下载文件";
        return false;
    }

    QJsonObject selectedFile = files.first().toObject();
    for (const QJsonValue &value : files) {
        if (value.toObject().value("primary").toBool()) {
            selectedFile = value.toObject();
            break;
        }
    }

    const QString directory = targetDirectoryForType(targetType);
    ensureDirectory(directory, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString fileName = sanitizeFileName(selectedFile.value("filename").toString());
    const QString filePath = directory + "/" + fileName;
    log(QString("准备安装 Modrinth 文件: %1 -> %2")
            .arg(fileName, QDir::toNativeSeparators(filePath)));
    if (!downloadToFile(QUrl(selectedFile.value("url").toString()),
                        filePath,
                        error,
                        selectedFile.value("hashes").toObject().value("sha1").toString())) {
        return false;
    }

    const QString relative = QDir(QDir::fromNativeSeparators(m_configStore.config().minecraftDir)).relativeFilePath(filePath);
    return recordManagedFile(relative, targetType, error);
}

QJsonArray LauncherCore::fetchLoaderVersions(const QString &gameVersion, const QString &loaderType, QString *error)
{
    QString url;
    if (loaderType == "fabric") {
        url = QString("https://meta.fabricmc.net/v2/versions/loader/%1").arg(gameVersion);
    } else if (loaderType == "forge") {
        url = QString("https://bmclapi2.bangbang93.com/forge/minecraft/%1").arg(gameVersion);
    } else if (loaderType == "neoforge") {
        url = QString("https://bmclapi2.bangbang93.com/neoforge/list/%1").arg(gameVersion);
    } else if (loaderType == "quilt") {
        url = QString("https://meta.quiltmc.org/v3/versions/loader/%1").arg(gameVersion);
    } else {
        *error = "不支持的加载器类型";
        return {};
    }

    const QJsonDocument doc = getJson(QUrl(rewriteUrl(url)), error);
    if (!error->isEmpty()) {
        return {};
    }

    if (doc.isArray()) {
        return doc.array();
    } else if (doc.isObject()) {
        // NeoForge returns an object with a "version" array
        return doc.object().value("version").toArray();
    }
    return {};
}

bool LauncherCore::installLoaderWithDialog(const QString &gameVersion, const QString &loaderType, const QString &loaderVersion, QString *error, QWidget *parentWidget)
{
    log(QString("开始安装加载器: %1 %2 (游戏版本: %3)").arg(loaderType, loaderVersion, gameVersion));
    const LauncherVersionProfile baseProfile = effectiveVersionProfile(gameVersion);
    const QString baseDir = versionsRoot() + "/" + gameVersion;
    ensureDirectory(baseDir, error);
    if (!error->isEmpty()) return false;
    
    if (loaderType == "fabric" || loaderType == "quilt") {
        QString url;
        if (loaderType == "fabric") {
            url = QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json").arg(gameVersion, loaderVersion);
        } else {
            url = QString("https://meta.quiltmc.org/v3/versions/loader/%1/%2/profile/json").arg(gameVersion, loaderVersion);
        }
        
        const QJsonDocument doc = getJson(QUrl(rewriteUrl(url)), error);
        if (!error->isEmpty()) {
            return false;
        }
        
        QJsonObject profile = doc.object();
        const QString versionId = profile.value("id").toString();
        if (versionId.isEmpty()) {
            *error = "无效的 profile JSON";
            return false;
        }
        
        profile.insert("inheritsFrom", gameVersion);
        const QString jsonName = QString("%1.json").arg(versionId);
        const QString jsonPath = baseDir + "/" + jsonName;
        QFile file(jsonPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *error = QString("无法写入加载器配置: %1").arg(QDir::toNativeSeparators(jsonPath));
            return false;
        }
        file.write(QJsonDocument(profile).toJson());
        file.close();
        
        saveVersionProfile(gameVersion, baseProfile, nullptr);
        if (!setVersionSelectedLoader(gameVersion, loaderType, jsonName, error)) {
            return false;
        }
        log(QString("加载器 %1 安装成功，配置: %2").arg(loaderType, jsonName));
        return true;
    } else if (loaderType == "forge" || loaderType == "neoforge") {
        const QSet<QString> jsonFilesBefore = [&] {
            QSet<QString> s;
            const QDir d(baseDir);
            for (const QFileInfo &fi : d.entryInfoList(QStringList() << "*.json", QDir::Files)) {
                s.insert(fi.fileName());
            }
            return s;
        }();

        QString installerUrl;
        QString installerName;
        if (loaderType == "forge") {
            installerUrl = QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1-%2/forge-%1-%2-installer.jar").arg(gameVersion, loaderVersion);
            installerName = QString("forge-%1-%2-installer.jar").arg(gameVersion, loaderVersion);
        } else {
            installerUrl = QString("https://maven.neoforged.net/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(loaderVersion);
            installerName = QString("neoforge-%1-installer.jar").arg(loaderVersion);
        }
        
        const QString tempDir = appDataRoot() + "/temp";
        ensureDirectory(tempDir, error);
        if (!error->isEmpty()) {
            return false;
        }
        
        const QString installerPath = tempDir + "/" + installerName;
        
        DownloadManager manager(m_networkManager, this);
        if (!manager.downloadFile(QUrl(rewriteUrl(installerUrl)),
                                  installerPath,
                                  "下载加载器安装程序",
                                  QString("正在下载: %1").arg(installerName),
                                  error,
                                  parentWidget)) {
            if (error) {
                log(QString("加载器安装程序下载失败: %1").arg(*error));
            }
            return false;
        }
        
        QString javaPath;
        if (!ensureJavaAvailable(17, &javaPath, error)) {
            return false;
        }
        
        const QString mcDir = QDir::fromNativeSeparators(m_configStore.config().minecraftDir);
        const QString profilesPath = mcDir + "/launcher_profiles.json";
        if (!QFileInfo::exists(profilesPath)) {
            ensureDirectory(mcDir, error);
            QFile profilesFile(profilesPath);
            if (profilesFile.open(QIODevice::WriteOnly)) {
                QJsonObject profilesObj;
                profilesObj.insert("profiles", QJsonObject());
                profilesFile.write(QJsonDocument(profilesObj).toJson());
            }
        }
        
        log(QString("运行加载器安装程序: %1").arg(installerPath));
        
        DownloadProgressDialog dialog("安装加载器", "正在运行安装程序，请稍候...", parentWidget);
        dialog.setOverallBusy(true);
        dialog.setBusy(true);
        dialog.show();
        
        QProcess process;
        process.setProgram(javaPath);
        QStringList installerArgs;
        installerArgs << "-jar" << installerPath << "--installClient" << QDir::toNativeSeparators(mcDir);
        process.setArguments(installerArgs);
        process.setWorkingDirectory(tempDir);
        process.start();
        
        bool cancelled = false;
        QObject::connect(&dialog, &DownloadProgressDialog::cancelled, [&]() {
            cancelled = true;
            process.kill();
        });
        
        while (process.state() == QProcess::Running) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (cancelled || dialog.isCancelled()) {
                if (error) *error = "操作已取消";
                return false;
            }
        }
        
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            *error = QString("安装程序运行失败: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()));
            log(*error);
            return false;
        }

        QString pickedJson;
        const QDir d(baseDir);
        for (const QFileInfo &fi : d.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Time | QDir::Reversed)) {
            const QString name = fi.fileName();
            if (jsonFilesBefore.contains(name)) continue;
            if (name.compare(gameVersion + ".json", Qt::CaseInsensitive) == 0) continue;
            QString err;
            const QJsonObject candidate = loadVersionJsonWithOverride(gameVersion, name, false, &err);
            if (candidate.isEmpty()) continue;
            if (detectLoaderType(name, candidate) == loaderType) {
                pickedJson = name;
                break;
            }
        }

        if (pickedJson.isEmpty()) {
            const QStringList versionsAfterInstall = listInstalledVersions();
            for (const QString &candidate : versionsAfterInstall) {
                if (!versionMatchesBase(candidate, gameVersion)) continue;
                if (candidate.compare(gameVersion, Qt::CaseInsensitive) == 0) continue;
                const QJsonObject candidateJson = loadVersionJson(candidate, false, nullptr);
                if (detectLoaderType(candidate, candidateJson) == loaderType) {
                    const QString srcDir = versionsRoot() + "/" + candidate;
                    const QString srcJson = srcDir + "/" + candidate + ".json";
                    const QString dstName = QString("%1.json").arg(candidate);
                    const QString dstJson = baseDir + "/" + dstName;
                    if (QFile::exists(srcJson)) {
                        if (QFile::exists(dstJson)) QFile::remove(dstJson);
                        if (QFile::copy(srcJson, dstJson)) {
                            pickedJson = dstName;
                        }
                    }
                    saveVersionProfile(candidate, baseProfile, nullptr);
                    break;
                }
            }
        }
        
        if (pickedJson.isEmpty()) {
            log(QString("加载器 %1 安装完成，但未能在版本目录中定位到对应的 JSON，将保留默认原版启动。").arg(loaderType));
            saveVersionProfile(gameVersion, baseProfile, nullptr);
            return true;
        }

        saveVersionProfile(gameVersion, baseProfile, nullptr);
        if (!setVersionSelectedLoader(gameVersion, loaderType, pickedJson, error)) {
            return false;
        }
        log(QString("加载器 %1 安装成功，激活配置: %2").arg(loaderType, pickedJson));
        return true;
    }
    
    *error = "不支持的加载器类型";
    return false;
}

bool LauncherCore::downloadCurseForgeFile(qint64 modId,
                                          qint64 fileId,
                                          const QString &targetType,
                                          QString *error)
{
    if (m_configStore.config().curseForgeApiKey.isEmpty()) {
        *error = "CurseForge 下载需要 API Key";
        return false;
    }

    log(QString("开始下载 CurseForge 文件: modId=%1, fileId=%2, targetType=%3")
            .arg(modId)
            .arg(fileId)
            .arg(targetType));
    const QString apiKey = m_configStore.config().curseForgeApiKey;
    const QByteArray metaBytes = getBytes(
        QUrl(QString("https://api.curseforge.com/v1/mods/%1/files/%2").arg(modId).arg(fileId)),
        error,
        {{"x-api-key", apiKey}});
    if (!error->isEmpty()) {
        return false;
    }
    const QJsonObject meta = QJsonDocument::fromJson(metaBytes).object().value("data").toObject();

    const QByteArray urlBytes = getBytes(
        QUrl(QString("https://api.curseforge.com/v1/mods/%1/files/%2/download-url").arg(modId).arg(fileId)),
        error,
        {{"x-api-key", apiKey}});
    if (!error->isEmpty()) {
        return false;
    }
    const QString downloadUrl = QJsonDocument::fromJson(urlBytes).object().value("data").toString();
    if (downloadUrl.isEmpty()) {
        *error = "CurseForge 未返回下载地址";
        return false;
    }

    const QString directory = targetDirectoryForType(targetType);
    ensureDirectory(directory, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString fileName = sanitizeFileName(meta.value("fileName").toString());
    const QString filePath = directory + "/" + fileName;
    log(QString("准备安装 CurseForge 文件: %1 -> %2")
            .arg(fileName, QDir::toNativeSeparators(filePath)));
    if (!downloadToFile(QUrl(downloadUrl), filePath, error)) {
        return false;
    }

    const QString relative = QDir(QDir::fromNativeSeparators(m_configStore.config().minecraftDir)).relativeFilePath(filePath);
    return recordManagedFile(relative, targetType, error);
}

QJsonObject LauncherCore::getModrinthProjectDetails(const QString &projectId, QString *error)
{
    QUrl url(QString("https://api.modrinth.com/v2/project/%1").arg(projectId));
    const QJsonDocument doc = getJson(url, error);
    if (!error->isEmpty()) {
        return {};
    }
    return doc.object();
}

QJsonArray LauncherCore::getModrinthProjectVersions(const QString &projectId, QString *error)
{
    QUrl url(QString("https://api.modrinth.com/v2/project/%1/version").arg(projectId));
    const QJsonDocument doc = getJson(url, error);
    if (!error->isEmpty()) {
        return {};
    }
    return doc.array();
}

bool LauncherCore::downloadModrinthVersionFile(const QJsonObject &versionObj,
                                               const QJsonObject &fileObj,
                                               const QString &targetType,
                                               QString *error)
{
    log(QString("开始下载 Modrinth 版本文件: version=%1, targetType=%2")
            .arg(versionObj.value("name").toString(), targetType));
    const QString directory = targetDirectoryForType(targetType);
    ensureDirectory(directory, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString fileName = sanitizeFileName(fileObj.value("filename").toString());
    const QString filePath = directory + "/" + fileName;
    log(QString("准备安装 Modrinth 版本文件: %1 -> %2")
            .arg(fileName, QDir::toNativeSeparators(filePath)));
    if (!downloadToFile(QUrl(fileObj.value("url").toString()),
                        filePath,
                        error,
                        fileObj.value("hashes").toObject().value("sha1").toString())) {
        return false;
    }

    const QString relative = QDir(QDir::fromNativeSeparators(m_configStore.config().minecraftDir)).relativeFilePath(filePath);
    return recordManagedFile(relative, targetType, error);
}

bool LauncherCore::downloadModrinthProjectWithDialog(const QString &projectId,
                                                     const QString &gameVersion,
                                                     const QString &loader,
                                                     const QString &targetType,
                                                     QString *error,
                                                     QWidget *parentWidget)
{
    log(QString("开始下载 Modrinth 项目(对话框): projectId=%1, gameVersion=%2, loader=%3, targetType=%4")
            .arg(projectId, gameVersion, loader, targetType));
    QUrl url(QString("https://api.modrinth.com/v2/project/%1/version").arg(projectId));
    QUrlQuery query;
    if (!gameVersion.isEmpty()) {
        query.addQueryItem("game_versions", QString("[\"%1\"]").arg(gameVersion));
    }
    if (!loader.isEmpty()) {
        query.addQueryItem("loaders", QString("[\"%1\"]").arg(loader));
    }
    url.setQuery(query);

    const QJsonDocument doc = getJson(url, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QJsonArray versions = doc.array();
    if (versions.isEmpty()) {
        *error = "未找到匹配的 Modrinth 版本";
        return false;
    }

    const QJsonObject version = versions.first().toObject();
    const QJsonArray files = version.value("files").toArray();
    if (files.isEmpty()) {
        *error = "Modrinth 项目没有可下载文件";
        return false;
    }

    QJsonObject selectedFile = files.first().toObject();
    for (const QJsonValue &value : files) {
        if (value.toObject().value("primary").toBool()) {
            selectedFile = value.toObject();
            break;
        }
    }

    const QString directory = targetDirectoryForType(targetType);
    ensureDirectory(directory, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString fileName = sanitizeFileName(selectedFile.value("filename").toString());
    const QString filePath = directory + "/" + fileName;
    log(QString("准备安装 Modrinth 文件: %1 -> %2")
            .arg(fileName, QDir::toNativeSeparators(filePath)));

    DownloadManager manager(m_networkManager, this);
    if (!manager.downloadFile(QUrl(selectedFile.value("url").toString()),
                              filePath,
                              "下载模组",
                              QString("正在下载: %1").arg(fileName),
                              error,
                              parentWidget,
                              selectedFile.value("hashes").toObject().value("sha1").toString())) {
        if (error) {
            log(QString("Modrinth 下载失败: %1").arg(*error));
        }
        return false;
    }

    const QString relative = QDir(QDir::fromNativeSeparators(m_configStore.config().minecraftDir)).relativeFilePath(filePath);
    return recordManagedFile(relative, targetType, error);
}

bool LauncherCore::downloadCurseForgeFileWithDialog(qint64 modId,
                                                    qint64 fileId,
                                                    const QString &targetType,
                                                    QString *error,
                                                    QWidget *parentWidget)
{
    if (m_configStore.config().curseForgeApiKey.isEmpty()) {
        *error = "CurseForge 下载需要 API Key";
        return false;
    }

    log(QString("开始下载 CurseForge 文件(对话框): modId=%1, fileId=%2, targetType=%3")
            .arg(modId)
            .arg(fileId)
            .arg(targetType));
    const QString apiKey = m_configStore.config().curseForgeApiKey;
    const QByteArray metaBytes = getBytes(
        QUrl(QString("https://api.curseforge.com/v1/mods/%1/files/%2").arg(modId).arg(fileId)),
        error,
        {{"x-api-key", apiKey}});
    if (!error->isEmpty()) {
        return false;
    }
    const QJsonObject meta = QJsonDocument::fromJson(metaBytes).object().value("data").toObject();

    const QByteArray urlBytes = getBytes(
        QUrl(QString("https://api.curseforge.com/v1/mods/%1/files/%2/download-url").arg(modId).arg(fileId)),
        error,
        {{"x-api-key", apiKey}});
    if (!error->isEmpty()) {
        return false;
    }
    const QString downloadUrl = QJsonDocument::fromJson(urlBytes).object().value("data").toString();
    if (downloadUrl.isEmpty()) {
        *error = "CurseForge 未返回下载地址";
        return false;
    }

    const QString directory = targetDirectoryForType(targetType);
    ensureDirectory(directory, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString fileName = sanitizeFileName(meta.value("fileName").toString());
    const QString filePath = directory + "/" + fileName;
    log(QString("准备安装 CurseForge 文件: %1 -> %2")
            .arg(fileName, QDir::toNativeSeparators(filePath)));

    DownloadManager manager(m_networkManager, this);
    if (!manager.downloadFile(QUrl(downloadUrl),
                              filePath,
                              "下载模组",
                              QString("正在下载: %1").arg(fileName),
                              error,
                              parentWidget)) {
        if (error) {
            log(QString("CurseForge 下载失败: %1").arg(*error));
        }
        return false;
    }

    const QString relative = QDir(QDir::fromNativeSeparators(m_configStore.config().minecraftDir)).relativeFilePath(filePath);
    return recordManagedFile(relative, targetType, error);
}

bool LauncherCore::downloadModrinthVersionFileWithDialog(const QJsonObject &versionObj,
                                                         const QJsonObject &fileObj,
                                                         const QString &targetType,
                                                         QString *error,
                                                         QWidget *parentWidget)
{
    log(QString("开始下载 Modrinth 版本文件(对话框): version=%1, targetType=%2")
            .arg(versionObj.value("name").toString(), targetType));
    const QString directory = targetDirectoryForType(targetType);
    ensureDirectory(directory, error);
    if (!error->isEmpty()) {
        return false;
    }

    const QString fileName = sanitizeFileName(fileObj.value("filename").toString());
    const QString filePath = directory + "/" + fileName;
    log(QString("准备安装 Modrinth 版本文件: %1 -> %2")
            .arg(fileName, QDir::toNativeSeparators(filePath)));

    DownloadManager manager(m_networkManager, this);
    if (!manager.downloadFile(QUrl(fileObj.value("url").toString()),
                              filePath,
                              "下载模组",
                              QString("正在下载: %1").arg(fileName),
                              error,
                              parentWidget,
                              fileObj.value("hashes").toObject().value("sha1").toString())) {
        if (error) {
            log(QString("Modrinth 版本文件下载失败: %1").arg(*error));
        }
        return false;
    }

    const QString relative = QDir(QDir::fromNativeSeparators(m_configStore.config().minecraftDir)).relativeFilePath(filePath);
    return recordManagedFile(relative, targetType, error);
}
