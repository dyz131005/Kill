#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "config_store.h"

class QNetworkAccessManager;
class QWidget;

class LauncherCore : public QObject
{
    Q_OBJECT

public:
    explicit LauncherCore(QObject *parent = nullptr);
    ~LauncherCore() override;

    ConfigStore *configStore();
    const ConfigStore *configStore() const;

    QStringList discoverJavaExecutables() const;

    QString effectiveMicrosoftClientId() const;
    [[deprecated("Use buildMicrosoftPkceAuthUrl() which supports PKCE for security.")]]
    QString buildMicrosoftAuthUrl() const;

    struct MicrosoftPkceAuth {
        QString authorizeUrl;
        QString codeVerifier;
        QString state;
        QString redirectUri;
    };
    MicrosoftPkceAuth buildMicrosoftPkceAuthUrl() const;

    bool loginMicrosoftAuthorizationCode(const QString &codeOrRedirectUrl,
                                         const QString &codeVerifier,
                                         QString *error);

    struct MicrosoftDeviceCode {
        QString userCode;
        QString verificationUri;
        QString deviceCode;
        int intervalSec = 5;
        qint64 expiresAtUtcSec = 0;
    };
    bool startMicrosoftDeviceLogin(MicrosoftDeviceCode *outCode, QString *error);

    enum class DevicePollOutcome {
        Pending,
        Authorized,
        Expired,
        SlowDown,
        Failed
    };
    DevicePollOutcome pollMicrosoftDeviceLogin(MicrosoftDeviceCode &ioCode, QString *pollErrorOut, QString *error);

    QString microsoftDefaultRedirectUri() const;
    QString microsoftOAuthScope() const;

    QJsonArray fetchVersionManifest(QString *error);
    QJsonArray filterVersionsByType(const QJsonArray &versions, const QString &type) const;
    QStringList listInstalledVersions() const;
    LauncherVersionProfile effectiveVersionProfile(const QString &versionId) const;
    bool saveVersionProfile(const QString &versionId, const LauncherVersionProfile &profile, QString *error = nullptr);
    QString versionBaseGameVersion(const QString &versionId) const;
    QString versionInstanceDirectory(const QString &versionId) const;
    QString versionModsDirectory(const QString &versionId) const;
    QString versionWorldsDirectory(const QString &versionId) const;
    QJsonArray listVersionMods(const QString &versionId) const;
    bool setModEnabled(const QString &versionId, const QString &fileName, bool enabled, QString *error);
    bool removeModFile(const QString &versionId, const QString &fileName, QString *error);
    QString versionResourcepacksDirectory(const QString &versionId) const;
    QJsonArray listVersionResourcepacks(const QString &versionId) const;
    bool removeResourcepack(const QString &versionId, const QString &fileName, QString *error);
    QJsonArray listVersionWorlds(const QString &versionId) const;
    bool removeWorld(const QString &versionId, const QString &worldName, QString *error);
    QJsonArray scanInstalledLoaders(const QString &versionId) const;
    QJsonArray scanLoadersForBaseVersion(const QString &baseVersionId) const;
    bool setVersionSelectedLoader(const QString &baseVersionId, const QString &loaderType, const QString &loaderJson, QString *error = nullptr);
    bool clearVersionSelectedLoader(const QString &baseVersionId, QString *error = nullptr);
    QString resolveActiveLoaderJson(const QString &baseVersionId) const;
    QString versionJsonPath(const QString &baseVersionId, const QString &jsonName = QString()) const;
    bool uninstallLoaderFromVersion(const QString &baseVersionId, const QString &loaderType, const QString &loaderJson, QString *error = nullptr);
    bool uninstallVersion(const QString &versionId, QString *error);

    bool saveConfig(QString *error = nullptr);
    bool addOfflineAccount(const QString &username, QString *error);
    [[deprecated("Use loginMicrosoftAuthorizationCode() with PKCE codeVerifier.")]]
    bool loginMicrosoftWithRedirectUrl(const QString &redirectUrl, QString *error);
    bool installVanillaVersion(const QString &versionId, QString *error);
    bool installVanillaVersionWithDialog(const QString &versionId, QString *error, QWidget *parentWidget = nullptr);
    bool launchSelectedVersion(QString *error);
    bool launchSelectedVersionWithDialog(QString *error, QWidget *parentWidget = nullptr);
    QString installJavaRuntimeTo(int requiredMajor, const QString &installDir, QString *error);

    QJsonArray searchModrinth(const QString &query,
                              const QString &gameVersion,
                              const QString &projectType,
                              QString *error);
    QJsonArray searchCurseForge(const QString &query,
                                const QString &gameVersion,
                                QString *error);
    bool downloadModrinthProject(const QString &projectId,
                                 const QString &gameVersion,
                                 const QString &loader,
                                 const QString &targetType,
                                 QString *error);
    bool downloadModrinthProjectWithDialog(const QString &projectId,
                                           const QString &gameVersion,
                                           const QString &loader,
                                           const QString &targetType,
                                           QString *error,
                                           QWidget *parentWidget = nullptr);
    QJsonObject getModrinthProjectDetails(const QString &projectId, QString *error);
    QJsonArray getModrinthProjectVersions(const QString &projectId, QString *error);
    bool downloadModrinthVersionFile(const QJsonObject &versionObj,
                                     const QJsonObject &fileObj,
                                     const QString &targetType,
                                     QString *error);
    bool downloadModrinthVersionFileWithDialog(const QJsonObject &versionObj,
                                               const QJsonObject &fileObj,
                                               const QString &targetType,
                                               QString *error,
                                               QWidget *parentWidget = nullptr);
    bool downloadCurseForgeFile(qint64 modId,
                                qint64 fileId,
                                const QString &targetType,
                                QString *error);
    bool downloadCurseForgeFileWithDialog(qint64 modId,
                                          qint64 fileId,
                                          const QString &targetType,
                                          QString *error,
                                          QWidget *parentWidget = nullptr);

    QJsonArray fetchLoaderVersions(const QString &gameVersion, const QString &loaderType, QString *error);
    bool installLoaderWithDialog(const QString &gameVersion, const QString &loaderType, const QString &loaderVersion, QString *error, QWidget *parentWidget = nullptr);

    QNetworkAccessManager *networkManager() const { return m_networkManager; }

signals:
    void logMessage(const QString &message);
    void progressStarted(const QString &message);
    void progressUpdated(int value, int maximum, const QString &message);
    void progressFinished(const QString &message);

private:
    struct VersionInstallContext {
        QString versionId;
        QString versionDir;
        QString instanceDir;
        QJsonObject versionJson;
    };

    QString appDataRoot() const;
    QString librariesRoot() const;
    QString assetsRoot() const;
    QString versionsRoot() const;
    QString instancesRoot() const;
    QString modpacksRoot() const;

    bool resolveInheritance(QJsonObject &versionJson, QString *error) const;
    bool ensureVersionProfileStored(const QString &versionId, QString *error = nullptr);
    QJsonObject loadVersionJson(const QString &versionId, bool resolve, QString *error = nullptr) const;
    QJsonObject loadVersionJsonWithOverride(const QString &baseVersionId, const QString &loaderJsonName, bool resolve, QString *error = nullptr) const;
    QString detectLoaderType(const QString &versionId, const QJsonObject &versionJson) const;
    bool versionMatchesBase(const QString &candidateVersionId, const QString &baseVersionId) const;

    void log(const QString &message) const;
    void flushProcessLogBuffer(const QString &source, QByteArray &buffer, const QByteArray &chunk, bool flushRemaining = false) const;
    QString mirrorMetaBase() const;
    QString mirrorDataBase() const;
    QString rewriteUrl(const QString &original) const;
    bool shouldVerify() const;

    int detectJavaMajorFromPath(const QString &javaPath) const;
    int mapMinecraftVersionToJavaMajor(const QString &versionId) const;
    bool extractZipArchive(const QString &zipPath, const QString &destDir, QString *error) const;

    static QString sha1ForBytes(const QByteArray &data);
    static QString sha256ForBytes(const QByteArray &data);
    static bool verifySha1(const QString &filePath, const QString &expected);
    static bool verifySha256(const QString &filePath, const QString &expected);

    QJsonDocument getJson(const QUrl &url, QString *error);
    QByteArray getBytes(const QUrl &url, QString *error, const QList<QPair<QString, QString>> &headers = {});
    bool downloadToFile(const QUrl &url,
                        const QString &filePath,
                        QString *error,
                        const QString &expectedSha1 = QString(),
                        const QString &expectedSha256 = QString(),
                        const QList<QPair<QString, QString>> &headers = {});
    bool downloadToFileWithProgress(const QUrl &url,
                                    const QString &filePath,
                                    const QString &fileDescription,
                                    QString *error,
                                    const QString &expectedSha1 = QString(),
                                    const QString &expectedSha256 = QString(),
                                    const QList<QPair<QString, QString>> &headers = {});
    bool ensureDirectory(const QString &path, QString *error) const;
    QJsonObject findVersionMetadata(const QString &versionId, QString *error);
    bool installClientJar(const VersionInstallContext &ctx, QString *error);
    bool installLibraries(const VersionInstallContext &ctx, QString *error);
    bool installAssets(const VersionInstallContext &ctx, QString *error);
    bool installAssetIndex(const VersionInstallContext &ctx, QString *error);
    bool extractNatives(const VersionInstallContext &ctx, QString *error);
    bool rulesAllow(const QJsonArray &rules) const;
    QString nativeClassifier(const QJsonObject &library) const;
    QString javaExecutableName() const;
    QString javaLogExecutablePath(const QString &javaPath) const;
    QString locateBundledJava(const QString &versionId) const;
    int requiredJavaMajor(const QJsonObject &versionJson) const;
    bool ensureJavaAvailable(int requiredMajor, QString *resolvedPath, QString *error);
    QString installJavaRuntime(int requiredMajor, QString *error);
    QString installJavaRuntimeWithDialog(int requiredMajor, QString *error, QWidget *parentWidget = nullptr);
    QJsonObject fetchJavaRuntimeManifest(QString *error);
    bool refreshMicrosoftAccount(Account &account, QString *error);
    bool ensureMicrosoftAccountUsable(Account &account, QString *error);

    QString postJsonSync(const QUrl &url,
                         const QJsonObject &payload,
                         QString *error,
                         const QList<QPair<QString, QString>> &headers = {},
                         bool ignoreHttpError = false,
                         int *outHttpCode = nullptr) const;
    QString postFormSync(const QUrl &url,
                         const QUrlQuery &form,
                         QString *error,
                         const QList<QPair<QString, QString>> &headers = {},
                         bool ignoreHttpError = false,
                         int *outHttpCode = nullptr) const;

    QString describeXboxErrorCode(qint64 xErr, const QString &redirectUrl) const;

    struct MicrosoftXboxAuthResult {
        QString token;
        QString uhs;
        QString xuid;
    };
    bool authenticateXboxLive(const QString &liveAccessToken, MicrosoftXboxAuthResult *outXbl, QString *error) const;
    bool authenticateMinecraftXsts(const QString &xblToken, MicrosoftXboxAuthResult *outXsts, const QString &expectedUhs, QString *error) const;
    bool loginMinecraftWithXbox(const QString &uhs, const QString &xstsToken, QString *outMcAccessToken, QString *outMcTokenType, int *outExpiresInSec, QString *outUsername, QString *error) const;
    bool checkMinecraftOwnership(const QString &mcAccessToken, QString *error);
    bool fetchMinecraftProfile(const QString &mcTokenType, const QString &mcAccessToken, QString *outUuid, QString *outName, QString *error);

    bool exchangeMicrosoftAuthCodeForToken(const QString &clientId,
                                           const QString &code,
                                           const QString &redirectUri,
                                           const QString &codeVerifier,
                                           QString *outAccessToken,
                                           QString *outRefreshToken,
                                           int *outExpiresInSec,
                                           QString *outOAuthError,
                                           QString *error) const;
    bool exchangeMicrosoftDeviceCodeForToken(const QString &clientId,
                                             const QString &deviceCode,
                                             QString *outAccessToken,
                                             QString *outRefreshToken,
                                             int *outExpiresInSec,
                                             QString *outPollError,
                                             QString *error) const;
    bool exchangeMicrosoftRefreshToken(const QString &clientId,
                                       const QString &oldRefreshToken,
                                       QString *outNewAccessToken,
                                       QString *outNewRefreshToken,
                                       int *outExpiresInSec,
                                       QString *outOAuthError,
                                       QString *error) const;

    bool saveMicrosoftAccountFromTokens(const QString &liveAccessToken,
                                        const QString &liveRefreshToken,
                                        const QString &clientId,
                                        int msExpiresInSec,
                                        QString *error);
    bool saveMicrosoftAccountFromTokensInternal(const QString &liveAccessToken,
                                                const QString &liveRefreshToken,
                                                const QString &clientId,
                                                int msExpiresInSec,
                                                QString *error);
    bool ensureLaunchAssetsAvailable(const VersionInstallContext &ctx, QString *error);
    bool ensureLaunchAssetsAvailableWithDialog(const VersionInstallContext &ctx,
                                              QString *error,
                                              QWidget *parentWidget = nullptr);
    QString instanceDirectoryForVersion(const QString &versionId) const;
    QStringList buildClasspath(const VersionInstallContext &ctx) const;
    QStringList buildModulePath(const VersionInstallContext &ctx) const;
    QStringList buildJvmArguments(const VersionInstallContext &ctx,
                                  const Account &account,
                                  const QString &javaPath,
                                  const QString &classpath,
                                  const QString &modulePath,
                                  const LauncherVersionProfile &profile) const;
    QStringList buildGameArguments(const VersionInstallContext &ctx,
                                   const Account &account,
                                   const LauncherVersionProfile &profile) const;
    bool argumentListContainsFlag(const QStringList &args, const QString &flag) const;
    bool versionJsonRequestsModulePath(const QJsonObject &versionJson) const;
    QString substituteArgument(const QString &arg,
                               const VersionInstallContext &ctx,
                               const Account &account,
                               const QString &javaPath,
                               const QString &classpath,
                               const QString &modulePath,
                               bool useModulePathForClasspathPlaceholder) const;
    QJsonArray expandArgumentArray(const QJsonValue &value) const;
    bool recordManagedFile(const QString &relativePath, const QString &targetType, QString *error);
    QString targetDirectoryForType(const QString &targetType) const;
    bool startTrackedMinecraftProcess(const QString &javaPath,
                                      const QStringList &arguments,
                                      const QString &workingDirectory,
                                      const QString &versionId,
                                      QString *error);

    ConfigStore m_configStore;
    QNetworkAccessManager *m_networkManager = nullptr;
    QProcess *m_gameProcess = nullptr;
    mutable QByteArray m_gameStdoutBuffer;
    mutable QByteArray m_gameStderrBuffer;
};
