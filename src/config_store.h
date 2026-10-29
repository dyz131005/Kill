#pragma once

#include <QObject>
#include <QString>

#include "launcher_types.h"

class ConfigStore : public QObject
{
    Q_OBJECT

public:
    explicit ConfigStore(QObject *parent = nullptr);

    const LauncherConfig &config() const;
    LauncherConfig &config();

    QString configFilePath() const;
    bool load();
    bool save() const;

    // The directory that holds kill-config.json, java/, plugins/, temp/ etc.
    QString configDir() const { return m_configDir; }

    // Storage modes:
    //   "install" -> <exe dir>/.kill
    //   "appdata" -> %APPDATA%/Kill
    //   "custom"  -> arbitrary directory chosen by the user
    QString resolveDirForMode(const QString &mode) const;
    QString currentStorageMode() const { return m_storageMode; }

    // Change the config location. Writes the bootstrap pointer file so the
    // next launch knows where to look, then reloads (or re-saves) the config.
    // Returns false on failure (invalid dir / cannot write).
    bool setConfigDir(const QString &newDir, const QString &mode, QString *error = nullptr);

    // Legacy path detection: the old per-user AppData location that earlier
    // versions used. Returns an empty string if it doesn't exist.
    static QString legacyAppDataDir();

    // One-click migration: copy everything from `fromDir` into the current
    // config dir. Returns the number of files copied, or -1 on error.
    int migrateFrom(const QString &fromDir, QString *error = nullptr);

signals:
    void configReloaded();

private:
    QString defaultMinecraftDir() const;
    static QString bootstrapFilePath();
    void readBootstrap();
    void writeBootstrap();

    LauncherConfig m_config;
    QString m_configDir;
    QString m_storageMode = "install";
};
