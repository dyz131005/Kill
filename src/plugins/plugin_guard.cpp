#include "plugin_guard.h"
#include "plugin_api.h"

#include <QDir>
#include <QFileInfo>

namespace PluginGuard {

int permissionLevel(quint64 perm)
{
    if (perm & KILL_PERM_FORBIDDEN_MASK) return KILL_PERM_LEVEL_FORBIDDEN;
    static const quint64 confirmMask =
        KILL_PERM_WRITE_FILE | KILL_PERM_DELETE_FILE | KILL_PERM_RUN_PROCESS |
        KILL_PERM_NETWORK | KILL_PERM_WRITE_CONFIG;
    if (perm & confirmMask) return KILL_PERM_LEVEL_CONFIRM;
    return KILL_PERM_LEVEL_SAFE;
}

bool hasForbiddenPermission(quint64 permissions)
{
    static const quint64 knownMask =
        KILL_PERM_SAFE_MASK | KILL_PERM_CONFIRM_MASK | KILL_PERM_FORBIDDEN_MASK;
    if (permissions & KILL_PERM_FORBIDDEN_MASK) return true;
    if ((permissions & ~knownMask) != 0) return true;
    return false;
}

QStringList forbiddenPathPrefixes()
{
    QString winDir = qEnvironmentVariable("SystemRoot");
    if (winDir.isEmpty()) winDir = "C:/Windows";
    QString progData = qEnvironmentVariable("ProgramData");
    if (progData.isEmpty()) progData = "C:/ProgramData";
    QStringList list;
    list << (winDir + "/system32")
         << (winDir + "/syswow64")
         << (winDir + "/system")
         << (winDir + "/boot")
         << (progData + "/microsoft/crypto")
         << "c:/windows/system32/config"
         << "c:/windows/system32/config/sam";
    for (QString &s : list) s = s.toLower().replace('\\', '/');
    return list;
}

QStringList forbiddenCommandKeywords()
{
    return QStringList{
        "nsudo", "nsudo.exe", "trustedinstaller", "takeown", "icacls",
        "reg add", "reg delete", "regedit", "bcdedit", "diskpart",
        "format ", "vssadmin", "wbadmin", "cipher /w"
    };
}

static QString normalize(const QString &p)
{
    QString s = QDir::fromNativeSeparators(p).toLower();
    s = QDir::cleanPath(s);
    return s;
}

bool isPathAllowed(const QString &path, bool forDelete, QString *reason)
{
    const QString p = normalize(path);
    if (p.isEmpty()) {
        if (reason) *reason = "empty path";
        return false;
    }
    for (const QString &prefix : forbiddenPathPrefixes()) {
        if (p == prefix || p.startsWith(prefix + "/")) {
            if (reason) *reason = QString("protected system path blocked: %1").arg(path);
            return false;
        }
    }
    static const QStringList rootDirs = {
        "c:/", "c:/windows", "c:/program files", "c:/program files (x86)",
        "c:/programdata", "c:/users"
    };
    for (const QString &r : rootDirs) {
        if (p == r) {
            if (reason) *reason = QString("root-level dir blocked: %1").arg(path);
            return false;
        }
    }
    if (forDelete && p.endsWith(":/")) {
        if (reason) *reason = "drive root delete blocked";
        return false;
    }
    return true;
}

bool isCommandAllowed(const QString &program, const QStringList &args, QString *reason)
{
    QString combined = program.toLower();
    for (const QString &a : args) combined += " " + a.toLower();
    const QString exeName = QFileInfo(program).fileName().toLower();
    for (const QString &kw : forbiddenCommandKeywords()) {
        if (exeName == kw || exeName == kw + ".exe" || combined.contains(kw)) {
            if (reason) *reason = QString("blocked command: %1 %2").arg(program, args.join(' '));
            return false;
        }
    }
    if (combined.contains("system32") &&
        (combined.contains("del ") || combined.contains("rd ") ||
         combined.contains("rmdir ") || combined.contains("rm "))) {
        if (reason) *reason = "system32 delete blocked";
        return false;
    }
    return true;
}

} // namespace PluginGuard
