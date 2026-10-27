#include "plugin_manager.h"
#include "plugin_guard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QProcess>
#include <QWidget>

#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

PluginManager::PluginManager(QObject *parent) : QObject(parent) {}

PluginManager::~PluginManager()
{
    unloadAll();
}

void PluginManager::log(const QString &msg)
{
    emit logMessage(QStringLiteral("[Plugin] %1").arg(msg));
}

QString PluginManager::permissionSummary(quint64 permissions)
{
    QStringList parts;
    auto add = [&](quint64 bit, const char *name) {
        if (permissions & bit) parts << QString::fromUtf8(name);
    };
    add(KILL_PERM_LOG, "日志");
    add(KILL_PERM_UI, "界面");
    add(KILL_PERM_READ_CONFIG, "读配置");
    add(KILL_PERM_REGISTER_HOOK, "事件钩子");
    add(KILL_PERM_WRITE_FILE, "写文件");
    add(KILL_PERM_DELETE_FILE, "删文件");
    add(KILL_PERM_RUN_PROCESS, "运行进程");
    add(KILL_PERM_NETWORK, "网络");
    add(KILL_PERM_WRITE_CONFIG, "改配置");
    if (PluginGuard::hasForbiddenPermission(permissions)) parts << "包含禁止权限";
    if (parts.isEmpty()) return "无";
    return parts.join("、");
}

void PluginManager::scan(const QString &pluginsDir)
{
    m_pluginsDir = pluginsDir;
    m_authFilePath = QDir(pluginsDir).filePath("auth.json");
    loadAuth();
    m_entries.clear();
    QDir dir(pluginsDir);
    if (!dir.exists()) dir.mkpath(".");
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.dll", QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        PluginEntry entry;
        entry.filePath = fi.absoluteFilePath();
#ifdef Q_OS_WIN
        HMODULE mod = ::LoadLibraryW(reinterpret_cast<LPCWSTR>(fi.absoluteFilePath().utf16()));
        if (!mod) {
            entry.loadError = QString("无法加载 DLL (错误码 %1)").arg(::GetLastError());
            entry.name = fi.fileName();
            m_entries.append(entry);
            continue;
        }
        auto query = reinterpret_cast<KillPluginQueryFn>(
            reinterpret_cast<void *>(::GetProcAddress(mod, "kill_plugin_query")));
        if (!query) {
            entry.loadError = "缺少 kill_plugin_query 导出";
            entry.name = fi.fileName();
            ::FreeLibrary(mod);
            m_entries.append(entry);
            continue;
        }
        const KillPluginInfo *info = query();
        if (!info || !info->id) {
            entry.loadError = "kill_plugin_query 返回无效元信息";
            entry.name = fi.fileName();
            ::FreeLibrary(mod);
            m_entries.append(entry);
            continue;
        }
        entry.id = QString::fromUtf8(info->id);
        entry.name = QString::fromUtf8(info->name ? info->name : info->id);
        entry.version = QString::fromUtf8(info->version ? info->version : "");
        entry.author = QString::fromUtf8(info->author ? info->author : "");
        entry.description = QString::fromUtf8(info->description ? info->description : "");
        entry.permissions = info->permissions;
        entry.apiVersion = info->api_version;
        ::FreeLibrary(mod);
        if (entry.apiVersion != KILL_PLUGIN_API_VERSION) {
            entry.loadError = QString("API 版本不匹配 (插件 %1, 宿主 %2)")
                                  .arg(entry.apiVersion).arg(KILL_PLUGIN_API_VERSION);
        }
        entry.forbidden = PluginGuard::hasForbiddenPermission(entry.permissions);
        entry.authorized = isAuthorized(entry.id);
        m_entries.append(entry);
#else
        Q_UNUSED(fi);
#endif
    }
    log(QString("扫描完成，发现 %1 个插件").arg(m_entries.size()));
}

void PluginManager::loadAuth()
{
    m_auth = QJsonObject();
    QFile f(m_authFilePath);
    if (f.open(QIODevice::ReadOnly)) m_auth = QJsonDocument::fromJson(f.readAll()).object();
}

void PluginManager::saveAuth()
{
    QFile f(m_authFilePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_auth).toJson(QJsonDocument::Indented));
}

bool PluginManager::isAuthorized(const QString &pluginId) const
{
    return m_auth.value(pluginId).toBool(false);
}

void PluginManager::setAuthorized(const QString &pluginId, bool authorized)
{
    m_auth.insert(pluginId, authorized);
    saveAuth();
    for (PluginEntry &e : m_entries)
        if (e.id == pluginId) e.authorized = authorized;
}

bool PluginManager::loadPlugin(int index, QString *error)
{
    if (index < 0 || index >= m_entries.size()) {
        if (error) *error = "无效的插件索引";
        return false;
    }
    PluginEntry &entry = m_entries[index];
    if (entry.loaded) return true;
    if (entry.forbidden) {
        if (error) *error = "插件申请了被禁止的权限，拒绝加载";
        return false;
    }
    if (!entry.loadError.isEmpty()) {
        if (error) *error = entry.loadError;
        return false;
    }
#ifdef Q_OS_WIN
    HMODULE mod = ::LoadLibraryW(reinterpret_cast<LPCWSTR>(entry.filePath.utf16()));
    if (!mod) {
        entry.loadError = QString("无法加载 DLL (错误码 %1)").arg(::GetLastError());
        if (error) *error = entry.loadError;
        return false;
    }
    auto query = reinterpret_cast<KillPluginQueryFn>(
        reinterpret_cast<void *>(::GetProcAddress(mod, "kill_plugin_query")));
    auto loadFn = reinterpret_cast<KillPluginLoadFn>(
        reinterpret_cast<void *>(::GetProcAddress(mod, "kill_plugin_load")));
    if (!query || !loadFn) {
        entry.loadError = "缺少必要导出 (kill_plugin_query / kill_plugin_load)";
        ::FreeLibrary(mod);
        if (error) *error = entry.loadError;
        return false;
    }
    const KillPluginInfo *info = query();
    if (!info || !info->id) {
        entry.loadError = "元信息无效";
        ::FreeLibrary(mod);
        if (error) *error = entry.loadError;
        return false;
    }
    KillHostApi *api = new KillHostApi;
    memset(api, 0, sizeof(KillHostApi));
    api->struct_size = sizeof(KillHostApi);
    api->host = this;
    api->log = &PluginManager::cb_log;
    api->register_page = &PluginManager::cb_register_page;
    api->register_hook = &PluginManager::cb_register_hook;
    api->get_config_json = &PluginManager::cb_get_config_json;
    const bool authorized = entry.authorized;
    if (authorized) {
        api->write_file = &PluginManager::cb_write_file;
        api->read_file = &PluginManager::cb_read_file;
        api->delete_file = &PluginManager::cb_delete_file;
        api->run_process = &PluginManager::cb_run_process;
        api->http_request = &PluginManager::cb_http_request;
    }
    api->inject_widget = &PluginManager::cb_inject_widget;
    api->register_button = &PluginManager::cb_register_button;
    api->register_setting = &PluginManager::cb_register_setting;
    api->inject_style = &PluginManager::cb_inject_style;
    api->override_page = &PluginManager::cb_override_page;
    const int rc = loadFn(api);
    if (rc != 0) {
        entry.loadError = QString("kill_plugin_load 返回失败码 %1").arg(rc);
        ::FreeLibrary(mod);
        if (error) *error = entry.loadError;
        return false;
    }
    entry.dllHandle = reinterpret_cast<void *>(mod);
    entry.apiPtr = reinterpret_cast<void *>(api);
    entry.loaded = true;
    log(QString("已加载插件: %1 (%2)").arg(entry.name, entry.id));
    return true;
#else
    Q_UNUSED(entry);
    if (error) *error = "仅支持 Windows";
    return false;
#endif
}

void PluginManager::loadAllAuthorized()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        PluginEntry &e = m_entries[i];
        if (e.forbidden || !e.loadError.isEmpty()) continue;
        const bool needsConfirm = (e.permissions & KILL_PERM_CONFIRM_MASK) != 0;
        if (needsConfirm && !e.authorized) continue;
        QString err;
        if (!loadPlugin(i, &err)) log(QString("加载 %1 失败: %2").arg(e.name, err));
    }
}

void PluginManager::unloadAll()
{
#ifdef Q_OS_WIN
    for (PluginEntry &e : m_entries) {
        if (e.loaded && e.dllHandle) {
            HMODULE mod = reinterpret_cast<HMODULE>(e.dllHandle);
            auto unloadFn = reinterpret_cast<KillPluginUnloadFn>(
                reinterpret_cast<void *>(::GetProcAddress(mod, "kill_plugin_unload")));
            if (unloadFn) unloadFn();
            ::FreeLibrary(mod);
            e.dllHandle = nullptr;
            e.loaded = false;
        }
        if (e.apiPtr) {
            delete reinterpret_cast<KillHostApi *>(e.apiPtr);
            e.apiPtr = nullptr;
        }
    }
#endif
    m_pages.clear();
    m_hooks.clear();
}

void PluginManager::fireHook(const QString &event, const QJsonObject &payload)
{
    const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    for (const PluginHookRegistration &h : m_hooks) {
        if (h.event.compare(event, Qt::CaseInsensitive) == 0 && h.callback)
            h.callback(json);
    }
}

// ============================ Host API 回调实现 ============================

void PluginManager::cb_log(void *host, int level, const char *message)
{
    auto *self = static_cast<PluginManager *>(host);
    Q_UNUSED(level);
    self->log(QString("[plugin] %1").arg(QString::fromUtf8(message ? message : "")));
}

void *PluginManager::cb_register_page(void *host, const char *category, const char *title,
                                      void *(*factory)(void *), void *ud)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!factory) return nullptr;
    PluginPageRegistration reg;
    reg.category = QString::fromUtf8(category ? category : "home");
    reg.title = QString::fromUtf8(title ? title : "插件页面");
    reg.pluginId = QString("plugin");
    reg.factory = [factory, ud]() -> QWidget * {
        return static_cast<QWidget *>(factory(ud));
    };
    self->m_pages.append(reg);
    self->log(QString("插件注册页面: %1 (%2)").arg(reg.title, reg.category));
    return reinterpret_cast<void *>(1);
}

void *PluginManager::cb_register_hook(void *host, const char *event,
                                      void (*callback)(void *, const char *, const char *), void *ud)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!callback) return nullptr;
    PluginHookRegistration reg;
    reg.event = QString::fromUtf8(event ? event : "");
    reg.pluginId = QString("plugin");
    reg.callback = [callback, ud](const QString &payloadJson) {
        const QByteArray ba = payloadJson.toUtf8();
        callback(ud, nullptr, ba.constData());
    };
    self->m_hooks.append(reg);
    return reinterpret_cast<void *>(1);
}

const char *PluginManager::cb_get_config_json(void *host)
{
    Q_UNUSED(host);
    static QByteArray s_buf;
    s_buf = "{}";
    return s_buf.constData();
}

int PluginManager::cb_write_file(void *host, const char *path, const void *data, size_t size)
{
    auto *self = static_cast<PluginManager *>(host);
    const QString p = QString::fromUtf8(path ? path : "");
    QString reason;
    if (!PluginGuard::isPathAllowed(p, false, &reason)) {
        self->log(QString("拒绝写文件: %1").arg(reason));
        return -2;
    }
    QFile f(p);
    QDir().mkpath(QFileInfo(p).absolutePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return -1;
    f.write(static_cast<const char *>(data), static_cast<qint64>(size));
    f.close();
    return 0;
}

int PluginManager::cb_read_file(void *host, const char *path, void *out, size_t max_size, size_t *out_size)
{
    auto *self = static_cast<PluginManager *>(host);
    const QString p = QString::fromUtf8(path ? path : "");
    QString reason;
    if (!PluginGuard::isPathAllowed(p, false, &reason)) {
        self->log(QString("拒绝读文件: %1").arg(reason));
        return -2;
    }
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QByteArray data = f.read(static_cast<qint64>(max_size));
    f.close();
    if (out && max_size > 0) memcpy(out, data.constData(), static_cast<size_t>(data.size()));
    if (out_size) *out_size = static_cast<size_t>(data.size());
    return 0;
}

int PluginManager::cb_delete_file(void *host, const char *path)
{
    auto *self = static_cast<PluginManager *>(host);
    const QString p = QString::fromUtf8(path ? path : "");
    QString reason;
    if (!PluginGuard::isPathAllowed(p, true, &reason)) {
        self->log(QString("拒绝删除: %1").arg(reason));
        return -2;
    }
    if (!self->m_gameDir.isEmpty()) {
        const QString normGame = QDir::cleanPath(QDir::fromNativeSeparators(self->m_gameDir)).toLower();
        const QString normP = QDir::cleanPath(QDir::fromNativeSeparators(p)).toLower();
        if (normP == normGame) {
            self->log("拒绝删除整个游戏目录");
            return -2;
        }
    }
    if (!QFileInfo::exists(p)) return -1;
    if (QFileInfo(p).isDir()) return QDir(p).removeRecursively() ? 0 : -1;
    return QFile::remove(p) ? 0 : -1;
}

int PluginManager::cb_run_process(void *host, const char *program, const char *args_json, int wait)
{
    auto *self = static_cast<PluginManager *>(host);
    const QString prog = QString::fromUtf8(program ? program : "");

    QStringList args;
    const QJsonArray arr = QJsonDocument::fromJson(QByteArray(args_json ? args_json : "[]")).array();
    for (const QJsonValue &v : arr) args << v.toString();
    QString reason;
    if (!PluginGuard::isCommandAllowed(prog, args, &reason)) {
        self->log(QString("拒绝运行进程: %1").arg(reason));
        return -2;
    }
    QProcess proc;
    proc.start(prog, args);
    if (!proc.waitForStarted(5000)) return -1;
    if (wait) {
        proc.waitForFinished(60000);
        return proc.exitCode();
    }
    proc.closeWriteChannel();
    return 0;
}

int PluginManager::cb_http_request(void *host, const char *method, const char *url,
                                   const char *headers_json, const void *body, size_t body_size)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!self->m_nam) return -1;
    QNetworkRequest req(QUrl(QString::fromUtf8(url ? url : "")));
    const QJsonObject headers = QJsonDocument::fromJson(QByteArray(headers_json ? headers_json : "{}")).object();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
    QByteArray payload;
    if (body && body_size > 0) payload = QByteArray(static_cast<const char *>(body), static_cast<int>(body_size));
    const QString m = QString::fromUtf8(method ? method : "GET").toUpper();
    QNetworkReply *reply = nullptr;
    if (m == "POST") reply = self->m_nam->post(req, payload);
    else reply = self->m_nam->get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    return code;
}

#include "../plugin_ui_callbacks.inc"
