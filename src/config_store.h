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

signals:
    void configReloaded();

private:
    QString defaultMinecraftDir() const;

    LauncherConfig m_config;
};
