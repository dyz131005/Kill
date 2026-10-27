#pragma once

#include <QDateTime>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct Account {
    enum class Type {
        Offline,
        Microsoft
    };

    QString id;
    Type type = Type::Offline;
    QString username;
    QString uuid;
    QString accessToken;
    QString refreshToken;
    QString clientId;
    QString xuid;
    QString profileId;
    QString tokenType;
    QString microsoftUsername;
    QDateTime expiry;

    QJsonObject toJson() const;
    static Account fromJson(const QJsonObject &object);
    QString displayName() const;
    QString typeName() const;
};

struct LauncherVersionProfile {
    bool versionIsolation = true;
    // Software OpenGL override for this version: -1 = follow global, 0 = force off, 1 = force on.
    int softwareOpenGL = -1;
    QString javaPath;
    QString javaArgs;
    QString extraGameArgs;
    int minMemoryMb = 1024;
    int maxMemoryMb = 4096;
    QString selectedLoaderType;
    QString selectedLoaderJson;

    QJsonObject toJson() const;
    static LauncherVersionProfile fromJson(const QJsonObject &object);
};

struct LauncherConfig {
    QString minecraftDir;
    QString javaPath;
    QString javaArgs = "-XX:+UseG1GC -XX:+UnlockExperimentalVMOptions";
    QString extraGameArgs;
    QString selectedVersion;
    QString selectedLoaderType;
    QString selectedLoaderJson;
    QString curseForgeApiKey;
    QString microsoftClientId;
    QString mirrorSource = "bmclapi";
    QString mirrorMode = "auto";
    int minMemoryMb = 1024;
    int maxMemoryMb = 4096;
    bool verifyFiles = true;
    bool versionIsolation = true;
    bool softwareOpenGL = false;
    QStringList managedMods;
    QStringList managedResourcePacks;
    QStringList managedModpacks;
    QMap<QString, LauncherVersionProfile> versionProfiles;
    QList<Account> accounts;
    int selectedAccountIndex = -1;

    QJsonObject toJson() const;
    static LauncherConfig fromJson(const QJsonObject &object);
};

inline QString makeOfflineUuid(const QString &username)
{
    QByteArray bytes = ("OfflinePlayer:" + username).toUtf8().toHex();
    QString raw = QString::fromUtf8(bytes.leftJustified(32, '0', true));
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(raw.mid(0, 8), raw.mid(8, 4), raw.mid(12, 4), raw.mid(16, 4), raw.mid(20, 12));
}
