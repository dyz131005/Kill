#include "config_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace {
// %APPDATA%/Kill — used only for the bootstrap pointer that records where the
// real config directory lives, and for legacy detection.
QString osAppDataRoot()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + "/AppData/Roaming/Kill";
    }
    return root;
}

QString exeInstallRoot()
{
    return QCoreApplication::applicationDirPath();
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
        {"animationLevel", animationLevel},
        {"storageMode", storageMode},
        {"customStorageDir", customStorageDir},
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
    config.animationLevel = object.value("animationLevel").toInt(config.animationLevel);
    config.storageMode = object.value("storageMode").toString(config.storageMode);
    config.customStorageDir = object.value("customStorageDir").toString(config.customStorageDir);
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
    readBootstrap();
    QDir().mkpath(m_configDir);
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
    return m_configDir + "/kill-config.json";
}

QString ConfigStore::bootstrapFilePath()
{
    return osAppDataRoot() + "/kill-location.json";
}

void ConfigStore::readBootstrap()
{
    m_storageMode = "install";
    m_configDir = exeInstallRoot() + "/.kill";

    QFile f(bootstrapFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const QString mode = obj.value("mode").toString();
        const QString dir = obj.value("dir").toString();
        if (!mode.isEmpty()) {
            m_storageMode = mode;
            if (mode == "custom" && !dir.isEmpty()) {
                m_configDir = dir;
            } else {
                m_configDir = resolveDirForMode(mode);
            }
        } else if (!dir.isEmpty()) {
            m_configDir = dir;
            m_storageMode = "custom";
        }
    }

    m_configDir = QDir::fromNativeSeparators(m_configDir);
}

void ConfigStore::writeBootstrap()
{
    QDir().mkpath(osAppDataRoot());
    QFile f(bootstrapFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonObject obj;
    obj["mode"] = m_storageMode;
    if (m_storageMode == "custom") {
        obj["dir"] = m_configDir;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
}

QString ConfigStore::resolveDirForMode(const QString &mode) const
{
    if (mode == "appdata") {
        return QDir::fromNativeSeparators(osAppDataRoot());
    }
    if (mode == "custom") {
        return QDir::fromNativeSeparators(m_configDir);
    }
    return QDir::fromNativeSeparators(exeInstallRoot() + "/.kill");
}

QString ConfigStore::legacyAppDataDir()
{
    const QString root = osAppDataRoot();
    if (QFileInfo::exists(root + "/kill-config.json")) {
        return QDir::fromNativeSeparators(root);
    }
    return QString();
}

bool ConfigStore::setConfigDir(const QString &newDir, const QString &mode, QString *error)
{
    const QString normalized = QDir::fromNativeSeparators(newDir);
    if (normalized.isEmpty()) {
        if (error) *error = "配置目录不能为空";
        return false;
    }
    if (!QDir().mkpath(normalized)) {
        if (error) *error = QString("无法创建配置目录: %1").arg(normalized);
        return false;
    }

    m_configDir = normalized;
    m_storageMode = mode.isEmpty() ? QStringLiteral("custom") : mode;

    writeBootstrap();
    const bool ok = save();
    if (!ok) {
        if (error) *error = QString("无法写入新配置目录: %1").arg(normalized);
        return false;
    }
    return true;
}

int ConfigStore::migrateFrom(const QString &fromDir, QString *error)
{
    const QString src = QDir::fromNativeSeparators(fromDir);
    if (src.isEmpty() || !QFileInfo::exists(src)) {
        if (error) *error = QString("源目录不存在: %1").arg(fromDir);
        return -1;
    }
    if (QDir(src).absolutePath() == QDir(m_configDir).absolutePath()) {
        if (error) *error = "源目录与目标目录相同";
        return -1;
    }

    int copied = 0;
    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QString rel = QDir(src).relativeFilePath(sourcePath);
        const QString destPath = m_configDir + "/" + rel;
        QDir().mkpath(QFileInfo(destPath).absolutePath());
        if (QFileInfo::exists(destPath)) {
            QFile::remove(destPath);
        }
        if (QFile::copy(sourcePath, destPath)) {
            ++copied;
        }
    }
    return copied;
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
    // Default game directory lives under the config dir (.kill) by default.
    return QDir::toNativeSeparators(m_configDir + "/.minecraft");
}
