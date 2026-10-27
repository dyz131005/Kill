#include "config_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString appDataRoot()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + "/.kill";
    }
    QDir().mkpath(root);
    return root;
}
}

QJsonObject Account::toJson() const
{
    return {
        {"id", id},
        {"type", type == Type::Microsoft ? "microsoft" : "offline"},
        {"username", username},
        {"uuid", uuid},
        {"accessToken", accessToken},
        {"refreshToken", refreshToken},
        {"clientId", clientId},
        {"xuid", xuid},
        {"profileId", profileId},
        {"tokenType", tokenType},
        {"microsoftUsername", microsoftUsername},
        {"expiry", expiry.toString(Qt::ISODate)}
    };
}

Account Account::fromJson(const QJsonObject &object)
{
    Account account;
    account.id = object.value("id").toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    account.type = object.value("type").toString() == "microsoft" ? Type::Microsoft : Type::Offline;
    account.username = object.value("username").toString();
    account.uuid = object.value("uuid").toString();
    account.accessToken = object.value("accessToken").toString();
    account.refreshToken = object.value("refreshToken").toString();
    account.clientId = object.value("clientId").toString();
    account.xuid = object.value("xuid").toString();
    account.profileId = object.value("profileId").toString();
    account.tokenType = object.value("tokenType").toString();
    account.microsoftUsername = object.value("microsoftUsername").toString();
    account.expiry = QDateTime::fromString(object.value("expiry").toString(), Qt::ISODate);
    return account;
}

QString Account::displayName() const
{
    return QString("%1 (%2)").arg(username, typeName());
}

QString Account::typeName() const
{
    return type == Type::Microsoft ? "正版" : "离线";
}

QJsonObject LauncherVersionProfile::toJson() const
{
    return {
        {"versionIsolation", versionIsolation},
        {"javaPath", javaPath},
        {"javaArgs", javaArgs},
        {"extraGameArgs", extraGameArgs},
        {"minMemoryMb", minMemoryMb},
        {"maxMemoryMb", maxMemoryMb},
        {"selectedLoaderType", selectedLoaderType},
        {"selectedLoaderJson", selectedLoaderJson},
        {"softwareOpenGL", softwareOpenGL}
    };
}

LauncherVersionProfile LauncherVersionProfile::fromJson(const QJsonObject &object)
{
    LauncherVersionProfile profile;
    profile.versionIsolation = object.value("versionIsolation").toBool(profile.versionIsolation);
    profile.javaPath = object.value("javaPath").toString();
    profile.javaArgs = object.value("javaArgs").toString();
    profile.extraGameArgs = object.value("extraGameArgs").toString();
    profile.minMemoryMb = object.value("minMemoryMb").toInt(profile.minMemoryMb);
    profile.maxMemoryMb = object.value("maxMemoryMb").toInt(profile.maxMemoryMb);
    profile.selectedLoaderType = object.value("selectedLoaderType").toString();
    profile.selectedLoaderJson = object.value("selectedLoaderJson").toString();
    profile.softwareOpenGL = object.value("softwareOpenGL").toInt(profile.softwareOpenGL);
    return profile;
}

QJsonObject LauncherConfig::toJson() const
{
    QJsonArray accountsJson;
    for (const Account &account : accounts) {
        accountsJson.append(account.toJson());
    }

    auto toArray = [](const QStringList &list) {
        QJsonArray array;
        for (const QString &entry : list) {
            array.append(entry);
        }
        return array;
    };

    QJsonObject versionProfilesJson;
    for (auto it = versionProfiles.constBegin(); it != versionProfiles.constEnd(); ++it) {
        versionProfilesJson.insert(it.key(), it.value().toJson());
    }

    return {
        {"minecraftDir", minecraftDir},
        {"javaPath", javaPath},
        {"javaArgs", javaArgs},
        {"extraGameArgs", extraGameArgs},
        {"selectedVersion", selectedVersion},
        {"selectedLoaderType", selectedLoaderType},
        {"selectedLoaderJson", selectedLoaderJson},
        {"curseForgeApiKey", curseForgeApiKey},
        {"microsoftClientId", microsoftClientId},
        {"mirrorSource", mirrorSource},
        {"mirrorMode", mirrorMode},
        {"minMemoryMb", minMemoryMb},
        {"maxMemoryMb", maxMemoryMb},
        {"verifyFiles", verifyFiles},
        {"versionIsolation", versionIsolation},
        {"softwareOpenGL", softwareOpenGL},
        {"managedMods", toArray(managedMods)},
        {"managedResourcePacks", toArray(managedResourcePacks)},
        {"managedModpacks", toArray(managedModpacks)},
        {"versionProfiles", versionProfilesJson},
        {"selectedAccountIndex", selectedAccountIndex},
        {"accounts", accountsJson}
    };
}

LauncherConfig LauncherConfig::fromJson(const QJsonObject &object)
{
    LauncherConfig config;
    config.minecraftDir = object.value("minecraftDir").toString();
    config.javaPath = object.value("javaPath").toString();
    config.javaArgs = object.value("javaArgs").toString(config.javaArgs);
    config.extraGameArgs = object.value("extraGameArgs").toString();
    config.selectedVersion = object.value("selectedVersion").toString();
    config.selectedLoaderType = object.value("selectedLoaderType").toString();
    config.selectedLoaderJson = object.value("selectedLoaderJson").toString();
    config.curseForgeApiKey = object.value("curseForgeApiKey").toString();
    config.microsoftClientId = object.value("microsoftClientId").toString();
    config.mirrorSource = object.value("mirrorSource").toString(config.mirrorSource);
    if (object.contains("mirrorMode")) {
        config.mirrorMode = object.value("mirrorMode").toString(config.mirrorMode);
    } else {
        const QString legacy = config.mirrorSource.trimmed().toLower();
        if (legacy == "mojang" || legacy == "official") {
            config.mirrorMode = "official";
        } else if (legacy == "auto") {
            config.mirrorMode = "auto";
        } else if (!legacy.isEmpty()) {
            config.mirrorMode = "mirror";
        } else {
            config.mirrorMode = "auto";
        }
    }
    config.minMemoryMb = object.value("minMemoryMb").toInt(config.minMemoryMb);
    config.maxMemoryMb = object.value("maxMemoryMb").toInt(config.maxMemoryMb);
    config.verifyFiles = object.value("verifyFiles").toBool(config.verifyFiles);
    config.versionIsolation = object.value("versionIsolation").toBool(config.versionIsolation);
    config.softwareOpenGL = object.value("softwareOpenGL").toBool(config.softwareOpenGL);
    config.selectedAccountIndex = object.value("selectedAccountIndex").toInt(-1);

    auto fromArray = [](const QJsonValue &value) {
        QStringList list;
        for (const QJsonValue &entry : value.toArray()) {
            list.append(entry.toString());
        }
        return list;
    };

    config.managedMods = fromArray(object.value("managedMods"));
    config.managedResourcePacks = fromArray(object.value("managedResourcePacks"));
    config.managedModpacks = fromArray(object.value("managedModpacks"));

    const QJsonObject versionProfilesObject = object.value("versionProfiles").toObject();
    for (auto it = versionProfilesObject.begin(); it != versionProfilesObject.end(); ++it) {
        config.versionProfiles.insert(it.key(), LauncherVersionProfile::fromJson(it.value().toObject()));
    }

    for (const QJsonValue &entry : object.value("accounts").toArray()) {
        config.accounts.append(Account::fromJson(entry.toObject()));
    }
    return config;
}

ConfigStore::ConfigStore(QObject *parent)
    : QObject(parent)
{
    load();
}

const LauncherConfig &ConfigStore::config() const
{
    return m_config;
}

LauncherConfig &ConfigStore::config()
{
    return m_config;
}

QString ConfigStore::configFilePath() const
{
    return appDataRoot() + "/kill-config.json";
}

bool ConfigStore::load()
{
    QFile file(configFilePath());
    if (!file.exists()) {
        m_config.minecraftDir = defaultMinecraftDir();
        return save();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    m_config = LauncherConfig::fromJson(doc.object());
    if (m_config.minecraftDir.isEmpty()) {
        m_config.minecraftDir = defaultMinecraftDir();
    }
    emit configReloaded();
    return true;
}

bool ConfigStore::save() const
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(m_config.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

QString ConfigStore::defaultMinecraftDir() const
{
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (appData.isEmpty()) {
        appData = QDir::homePath();
    }
    return QDir::toNativeSeparators(appData + "/Kill/.minecraft");
}
