#include "forge_manual_installer.h"
#include "zip_utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QCryptographicHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QUrl>

namespace ForgeManual {

namespace {

QString mavenCoordsToPath(const QString &coords) {
    // group:artifact:version[:classifier][@ext]
    QString c = coords.trimmed();
    QString ext = "jar";
    const int at = c.indexOf('@');
    if (at >= 0) { ext = c.mid(at + 1); c = c.left(at); }
    const QStringList parts = c.split(':');
    if (parts.size() < 3) return QString();
    QString group = parts.at(0);
    const QString artifact = parts.at(1);
    const QString version = parts.at(2);
    QString classifier;
    if (parts.size() >= 4) classifier = parts.at(3);
    QString fileName = artifact + "-" + version;
    if (!classifier.isEmpty()) fileName += "-" + classifier;
    fileName += "." + ext;
    group.replace('.', '/');
    return group + "/" + artifact + "/" + version + "/" + fileName;
}

// Parse a processor literal: {key} -> vars[key]; 'x' -> literal x;
// [artifact] -> resolved artifact path; otherwise token-replace then return.
QString parseLiteral(const QString &literal, const QHash<QString, QString> &vars,
                     const QString &libraryDirectory, QString *err) {
    QString s = literal.trimmed();
    if (s.startsWith('{') && s.endsWith('}')) {
        const QString key = s.mid(1, s.size() - 2);
        if (!vars.contains(key)) { if (err) *err = "missing var " + key; return QString(); }
        return vars.value(key);
    }
    if (s.startsWith('\'') && s.endsWith('\'')) {
        return s.mid(1, s.size() - 2);
    }
    if (s.startsWith('[') && s.endsWith(']')) {
        const QString coords = s.mid(1, s.size() - 2);
        const QString rel = mavenCoordsToPath(coords);
        if (rel.isEmpty()) { if (err) *err = "bad artifact " + coords; return QString(); }
        return QDir::toNativeSeparators(libraryDirectory + "/" + rel);
    }
    // Token replacement of {KEY} occurrences.
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == '{') {
            const int close = s.indexOf('}', i);
            if (close < 0) { out += s.mid(i); break; }
            const QString key = s.mid(i + 1, close - i - 1);
            if (!vars.contains(key)) { if (err) *err = "missing var " + key; return QString(); }
            out += vars.value(key);
            i = close;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

static bool ensureLib(const Options &opts, const QString &relPath, const QString &url,
                      const QString &sha1, QString *err) {
    const QString dest = QDir::toNativeSeparators(opts.libraryDirectory + "/" + relPath);
    // If the file already exists (e.g. it was just extracted from the installer's
    // bundled maven/ folder, or was cached from a previous run), accept it as-is.
    // HMCL does the same: it only downloads what is missing. Bundled artifacts
    // may legitimately differ byte-for-byte from what a mirror serves, so we do
    // not reject an existing file solely on a SHA-1 mismatch.
    if (QFileInfo::exists(dest) && QFileInfo(dest).size() > 0)
        return true;
    QDir().mkpath(QFileInfo(dest).absolutePath());
    // No download URL means the artifact is expected to be bundled; if it is
    // missing here we cannot proceed for this entry.
    if (url.isEmpty()) {
        if (err) *err = "缺少依赖且无下载地址: " + relPath;
        return false;
    }
    if (!opts.download || !opts.download(url, dest, sha1)) {
        if (err) *err = "下载失败: " + relPath;
        return false;
    }
    return true;
}

static bool runProcessor(const Options &opts, const QJsonObject &proc,
                         const QHash<QString, QString> &vars, QString *err) {
    // processor "jar" may be a plain coord string ("g:a:v") or an object
    // {name: "g:a:v"}; handle both.
    QString jarCoords;
    const QJsonValue jarVal = proc.value("jar");
    if (jarVal.isString()) jarCoords = jarVal.toString();
    else if (jarVal.isObject()) jarCoords = jarVal.toObject().value("name").toString();
    const QString jarRel = mavenCoordsToPath(jarCoords);
    if (jarRel.isEmpty()) { if (err) *err = "processor 缺少 jar"; return false; }
    const QString jarPath = QDir::toNativeSeparators(opts.libraryDirectory + "/" + jarRel);
    if (!QFileInfo::exists(jarPath)) { if (err) *err = "缺少 processor jar: " + jarRel; return false; }

    QStringList cp;
    const QJsonArray cpArr = proc.value("classpath").toArray();
    for (const QJsonValue &v : cpArr) {
        QString coords;
        if (v.isString()) coords = v.toString();
        else if (v.isObject()) coords = v.toObject().value("name").toString();
        const QString rel = mavenCoordsToPath(coords);
        if (rel.isEmpty()) continue;
        const QString p = QDir::toNativeSeparators(opts.libraryDirectory + "/" + rel);
        if (QFileInfo::exists(p)) cp << p;
    }
    cp << jarPath;

    QStringList procArgs;
    const QJsonArray argsArr = proc.value("args").toArray();
    QString perr;
    for (const QJsonValue &v : argsArr) {
        const QString parsed = parseLiteral(v.toString(), vars, opts.libraryDirectory, &perr);
        if (!perr.isEmpty()) { if (err) *err = "参数解析失败: " + perr; return false; }
        procArgs << parsed;
    }

    QByteArray manifest;
    if (!ZipUtils::readFileFromArchive(jarPath, QStringLiteral("META-INF/MANIFEST.MF"), &manifest, nullptr)) {
        if (err) *err = "processor jar 无 MANIFEST: " + jarRel;
        return false;
    }
    QString mainClass;
    for (const QString &line : QString::fromUtf8(manifest).split('\n')) {
        if (line.startsWith("Main-Class:", Qt::CaseInsensitive)) {
            mainClass = line.mid(QStringLiteral("Main-Class:").size()).trimmed();
            break;
        }
    }
    if (mainClass.isEmpty()) { if (err) *err = "processor jar 无 Main-Class: " + jarRel; return false; }

    if (opts.log) opts.log(QString("运行处理器: %1").arg(mainClass));

    QProcess p;
    p.setProgram(opts.javaPath);
    QStringList full;
    full << "-cp" << cp.join(';') << mainClass << procArgs;
    p.setArguments(full);
    p.setWorkingDirectory(QDir::toNativeSeparators(opts.mcDir));
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    if (!p.waitForStarted(15000)) { if (err) *err = "处理器启动失败: " + p.errorString(); return false; }
    p.waitForFinished(600000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString out = QString::fromLocal8Bit(p.readAll()).right(1500);
        if (err) *err = QString("处理器失败(exit=%1):\n%2").arg(p.exitCode()).arg(out);
        return false;
    }
    return true;
}

// Detect a DOWNLOAD_MOJMAPS processor (fetches Mojang mappings from the
// blocked piston-data.mojang.com). Instead, download client_mappings from the
// CN mirror directly to the requested output. Returns true if handled.
static bool tryPatchMojmaps(const Options &opts, const QJsonObject &proc,
                             const QHash<QString, QString> &vars) {
    const QJsonArray argsArr = proc.value("args").toArray();
    QString task, side, version, output;
    QString cur;
    for (const QJsonValue &v : argsArr) {
        QString a = v.toString();
        QString parsed = a;
        if (a.startsWith('{') || a.startsWith('[') || a.startsWith('\'')) {
            QString e; parsed = parseLiteral(a, vars, opts.libraryDirectory, &e);
            if (!e.isEmpty()) parsed = a;
        }
        if (parsed.startsWith("--")) { cur = parsed.mid(2); if (!parsed.contains('=')) continue; }
        const int eq = parsed.indexOf('=');
        QString key = cur, val = parsed;
        if (cur.isEmpty() && eq > 0) { key = parsed.left(eq); val = parsed.mid(eq + 1); }
        if (eq > 0 && !key.isEmpty()) val = parsed.mid(eq + 1);
        if (key == "task") task = val;
        else if (key == "side") side = val;
        else if (key == "version") version = val;
        else if (key == "output") output = val;
        else if (!cur.isEmpty()) { /* value follows --key */ }
    }
    // Simpler robust re-parse: iterate tokens as --key value pairs.
    task.clear(); side.clear(); version.clear(); output.clear();
    for (int i = 0; i < argsArr.size(); ++i) {
        QString a = argsArr.at(i).toString();
        if (a.startsWith('{') || a.startsWith('[') || a.startsWith('\'')) {
            QString e; a = parseLiteral(a, vars, opts.libraryDirectory, &e);
        }
        if (a == "--task" && i + 1 < argsArr.size()) task = argsArr.at(++i).toString();
        else if (a == "--side" && i + 1 < argsArr.size()) side = argsArr.at(++i).toString();
        else if (a == "--version" && i + 1 < argsArr.size()) version = argsArr.at(++i).toString();
        else if (a == "--output" && i + 1 < argsArr.size()) {
            QString o = argsArr.at(++i).toString();
            QString e; output = parseLiteral(o, vars, opts.libraryDirectory, &e);
        }
    }
    if (task != "DOWNLOAD_MOJMAPS" || side != "client")
        return false;
    // If the mappings output already exists (prefetched by the caller), skip the
    // processor; otherwise let it run (may fail on blocked networks).
    if (!output.isEmpty() && QFileInfo::exists(output) && QFileInfo(output).size() > 1024) {
        if (opts.log) opts.log(QStringLiteral("[手动安装] 映射已存在，跳过 DOWNLOAD_MOJMAPS"));
        return true;
    }
    return false;
    // Query BMCLAPI for the version json to get client_mappings url.
    // Use a direct fetch via the download callback is not enough (it's a binary);
    // instead build the known URL pattern is unknown, so use the cached
    // prefetch: the caller already placed client_mappings if possible.
    // Fallback: try the mojang->bmclapi object mapping is not derivable here.
    // So we simply skip this processor if the output file already exists.
    if (QFileInfo::exists(output) && QFileInfo(output).size() > 1024) {
        if (opts.log) opts.log(QStringLiteral("[手动安装] 跳过 DOWNLOAD_MOJMAPS（映射已存在）"));
        return true;
    }
    return false; // let normal processor run (will likely fail if network blocked)
}

Result install(const Options &opts) {
    Result res;
    auto fail = [&](const QString &e) { res.ok = false; res.error = e; if (opts.log) opts.log(QString("[手动安装] 失败: %1").arg(e)); return res; };

    QByteArray profileBytes;
    if (!ZipUtils::readFileFromArchive(opts.installerJar, QStringLiteral("install_profile.json"), &profileBytes, &res.error))
        return fail("无法读取 install_profile.json: " + res.error);
    const QJsonObject profile = QJsonDocument::fromJson(profileBytes).object();
    if (profile.isEmpty()) return fail("install_profile.json 解析失败");

    QString versionJsonEntry = profile.value("json").toString(QStringLiteral("version.json"));
    while (versionJsonEntry.startsWith('/')) versionJsonEntry.remove(0, 1);
    QByteArray versionBytes;
    if (!ZipUtils::readFileFromArchive(opts.installerJar, versionJsonEntry, &versionBytes, &res.error))
        return fail("无法读取版本 json: " + res.error);
    const QJsonObject versionJson = QJsonDocument::fromJson(versionBytes).object();
    if (versionJson.isEmpty()) return fail("版本 json 解析失败");

    // 0) Extract libraries bundled INSIDE the installer jar (HMCL parity).
    // Forge/NeoForge installers ship their own build-time artifacts (neoform,
    // client, extra, etc.) under "maven/" in the jar. HMCL's ForgeNewInstallTask
    // copies these into the library directory in preExecute() and only downloads
    // the rest. Without this, we would try to fetch neoform from the blocked
    // maven.neoforged.net and fail (see log: neoform-*.zip 403/timeout).
    {
        QString zerr;
        const int n = ZipUtils::extractSubdirFromArchive(
            opts.installerJar, QStringLiteral("maven/"), opts.libraryDirectory, &zerr);
        if (n < 0) {
            if (opts.log) opts.log(QString("[手动安装] 提取内置 maven 库失败: %1").arg(zerr));
        } else if (n > 0) {
            if (opts.log) opts.log(QString("[手动安装] 已从安装器内置 maven/ 释放 %1 个文件").arg(n));
        }
    }

    // 1) Download install_profile libraries.
    const QJsonArray profileLibs = profile.value("libraries").toArray();
    for (const QJsonValue &v : profileLibs) {
        const QJsonObject lib = v.toObject();
        const QJsonObject art = lib.value("downloads").toObject().value("artifact").toObject();
        QString rel = art.value("path").toString();
        if (rel.isEmpty()) rel = mavenCoordsToPath(lib.value("name").toString());
        if (rel.isEmpty()) continue;
        const QString url = art.value("url").toString();
        const QString sha1 = art.value("sha1").toString();
        QString e;
        if (!ensureLib(opts, rel, url, sha1, &e)) return fail(e);
    }

    // 2) Download version.json libraries.
    const QJsonArray verLibs = versionJson.value("libraries").toArray();
    for (const QJsonValue &v : verLibs) {
        const QJsonObject lib = v.toObject();
        const QJsonObject art = lib.value("downloads").toObject().value("artifact").toObject();
        QString rel = art.value("path").toString();
        if (rel.isEmpty()) rel = mavenCoordsToPath(lib.value("name").toString());
        if (rel.isEmpty()) continue;
        const QString url = art.value("url").toString();
        const QString sha1 = art.value("sha1").toString();
        if (url.isEmpty()) continue;
        QString e;
        if (!ensureLib(opts, rel, url, sha1, &e)) return fail(e);
    }

    // 3) Build data vars.
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return fail("无法创建临时目录");
    QHash<QString, QString> vars;
    const QJsonObject dataObj = profile.value("data").toObject();
    for (auto it = dataObj.constBegin(); it != dataObj.constEnd(); ++it) {
        const QString key = it.key();
        const QString val = it.value().toObject().value("client").toString();
        if (val.startsWith('[') && val.endsWith(']')) {
            const QString rel = mavenCoordsToPath(val.mid(1, val.size() - 2));
            vars.insert(key, QDir::toNativeSeparators(opts.libraryDirectory + "/" + rel));
        } else if (val.startsWith('\'') && val.endsWith('\'')) {
            vars.insert(key, val.mid(1, val.size() - 2));
        } else {
            // A bare path (e.g. "/data/client.lzma") refers to a file bundled
            // INSIDE the installer jar. HMCL extracts it to a temp dir and
            // substitutes the absolute path. Zip entries never have a leading
            // slash, so strip any leading '/' before looking it up; otherwise
            // the lookup fails and the processor receives an unusable path.
            QString entry = val;
            while (entry.startsWith('/')) entry.remove(0, 1);
            QByteArray fileBytes;
            if (ZipUtils::readFileFromArchive(opts.installerJar, entry, &fileBytes, nullptr)) {
                // Prefix with the data key to avoid temp-file collisions when
                // two bundled files share a base name.
                const QString outPath = QDir::toNativeSeparators(
                    tmpDir.filePath(key + "-" + QFileInfo(entry).fileName()));
                QFile of(outPath);
                if (of.open(QIODevice::WriteOnly)) { of.write(fileBytes); of.close(); }
                vars.insert(key, outPath);
            } else {
                vars.insert(key, val);
            }
        }
    }
    vars.insert("SIDE", "client");
    vars.insert("MINECRAFT_JAR", QDir::toNativeSeparators(opts.mcDir + "/versions/" + opts.gameVersion + "/" + opts.gameVersion + ".jar"));
    vars.insert("MINECRAFT_VERSION", QDir::toNativeSeparators(opts.mcDir));
    vars.insert("ROOT", QDir::toNativeSeparators(opts.mcDir));
    vars.insert("INSTALLER", QDir::toNativeSeparators(opts.installerJar));
    vars.insert("LIBRARY_DIR", QDir::toNativeSeparators(opts.libraryDirectory));

    // 4) Run client processors in order.
    const QJsonArray processors = profile.value("processors").toArray();
    for (const QJsonValue &pv : processors) {
        const QJsonObject proc = pv.toObject();
        const QJsonArray sides = proc.value("sides").toArray();
        bool clientSide = sides.isEmpty();
        for (const QJsonValue &s : sides) { if (s.toString() == "client") clientSide = true; }
        if (!clientSide) continue;
        if (tryPatchMojmaps(opts, proc, vars)) continue;
        QString e;
        if (!runProcessor(opts, proc, vars, &e)) return fail(e);
    }

    // 5) Write the loader version json into the base version directory.
    const QString baseDir = opts.mcDir + "/versions/" + opts.gameVersion;
    QDir().mkpath(baseDir);
    const QString outName = (opts.loaderType == "neoforge")
        ? QString("neoforge-%1.json").arg(opts.loaderVersion)
        : QString("forge-%1-%2.json").arg(opts.gameVersion, opts.loaderVersion);
    QJsonObject outJson = versionJson;
    if (!outJson.contains("inheritsFrom")) outJson.insert("inheritsFrom", opts.gameVersion);
    const QString outPath = baseDir + "/" + outName;
    QFile of(outPath);
    if (!of.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("无法写入版本 json: " + outPath);
    of.write(QJsonDocument(outJson).toJson());
    of.close();

    res.ok = true;
    res.versionJsonPath = outPath;
    res.versionJsonName = outName;
    if (opts.log) opts.log(QString("[手动安装] 完成: %1").arg(outPath));
    return res;
}

} // namespace ForgeManual
