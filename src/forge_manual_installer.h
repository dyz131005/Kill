#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

class QNetworkAccessManager;
class LauncherCore;

// HMCL-style manual Forge/NeoForge installation.
// Does NOT run the installer's --installClient network flow. Instead it parses
// install_profile.json, downloads every needed library/artifact from a CN
// mirror, then executes the install processors locally (like HMCL's
// ForgeNewInstallTask). This works fully offline of the blocked
// maven.neoforged.net host.
namespace ForgeManual {

struct Options {
    QString gameVersion;      // e.g. "1.21.1"
    QString loaderType;       // "forge" or "neoforge"
    QString loaderVersion;    // e.g. "21.1.1"
    QString installerJar;     // path to the downloaded installer jar
    QString mcDir;            // .minecraft root
    QString javaPath;         // java.exe to run processors
    QString libraryDirectory; // <mcDir>/libraries
    QNetworkAccessManager *net = nullptr;
    // Progress / logging callback.
    std::function<void(const QString &)> log;
    // Download one URL to a path using mirrors; returns true on success.
    std::function<bool(const QString &url, const QString &dest, const QString &sha1)> download;
};

struct Result {
    bool ok = false;
    QString error;
    QString versionJsonPath;   // path to the generated loader version json
    QString versionJsonName;   // file name (e.g. neoforge-21.1.1.json)
};

Result install(const Options &opts);

} // namespace ForgeManual
