#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>
#include <functional>

#include "plugin_api.h"

class QWidget;
class QNetworkAccessManager;

struct PluginEntry {
    QString filePath;
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    quint64 permissions = 0;
    quint32 apiVersion = 0;
    bool forbidden = false;
    bool authorized = false;
    bool loaded = false;
    QString loadError;
    void *dllHandle = nullptr;
    void *apiPtr = nullptr;
};

struct PluginPageRegistration {
    QString pluginId;
    QString category;
    QString title;
    std::function<QWidget *()> factory;
};

struct PluginHookRegistration {
    QString pluginId;
    QString event;
    std::function<void(const QString &payloadJson)> callback;
};

struct PluginWidgetInjection {
    QString pluginId;
    int slot = 0;
    QString title;
    std::function<QWidget *()> factory;
};

struct PluginButtonInjection {
    QString pluginId;
    int slot = 0;
    QString text;
    std::function<void()> onClick;
};

struct PluginSettingInjection {
    QString pluginId;
    QString label;
    std::function<QWidget *()> factory;
};

struct PluginPageOverride {
    QString pluginId;
    int slot = 0;
    std::function<QWidget *()> factory;
};

class PluginManager : public QObject
{
    Q_OBJECT
public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    void scan(const QString &pluginsDir);
    QVector<PluginEntry> entries() const { return m_entries; }

    bool isAuthorized(const QString &pluginId) const;
    void setAuthorized(const QString &pluginId, bool authorized);

    bool loadPlugin(int index, QString *error);
    void loadAllAuthorized();
    void unloadAll();

    void fireHook(const QString &event, const QJsonObject &payload);

    QVector<PluginPageRegistration> pages() const { return m_pages; }

    void setNetworkManager(QNetworkAccessManager *nam) { m_nam = nam; }
    void setGameDirectory(const QString &dir) { m_gameDir = dir; }

    QVector<PluginWidgetInjection> widgetInjections() const { return m_widgetInjections; }
    QVector<PluginButtonInjection> buttonInjections() const { return m_buttonInjections; }
    QVector<PluginSettingInjection> settingInjections() const { return m_settingInjections; }
    QVector<PluginPageOverride> pageOverrides() const { return m_pageOverrides; }
    QStringList injectedStyles() const { return m_injectedStyles; }
    static QString permissionSummary(quint64 permissions);

signals:
    void logMessage(const QString &message);

private:
    QString m_pluginsDir;
    QString m_authFilePath;
    QString m_gameDir;
    QNetworkAccessManager *m_nam = nullptr;
    QVector<PluginEntry> m_entries;
    QVector<PluginPageRegistration> m_pages;
    QVector<PluginHookRegistration> m_hooks;
    QVector<PluginWidgetInjection> m_widgetInjections;
    QVector<PluginButtonInjection> m_buttonInjections;
    QVector<PluginSettingInjection> m_settingInjections;
    QVector<PluginPageOverride> m_pageOverrides;
    QStringList m_injectedStyles;
    QJsonObject m_auth;

    void loadAuth();
    void saveAuth();
    void log(const QString &msg);

    static void cb_log(void *host, int level, const char *message);
    static void *cb_register_page(void *host, const char *category, const char *title,
                                  void *(*factory)(void *), void *ud);
    static void *cb_register_hook(void *host, const char *event,
                                  void (*callback)(void *, const char *, const char *), void *ud);
    static const char *cb_get_config_json(void *host);
    static int cb_write_file(void *host, const char *path, const void *data, size_t size);
    static int cb_read_file(void *host, const char *path, void *out, size_t max_size, size_t *out_size);
    static int cb_delete_file(void *host, const char *path);
    static int cb_run_process(void *host, const char *program, const char *args_json, int wait);
    static int cb_http_request(void *host, const char *method, const char *url,
                               const char *headers_json, const void *body, size_t body_size);
    static void *cb_inject_widget(void *host, int slot, const char *title, void *(*factory)(void *), void *ud);
    static void *cb_register_button(void *host, int slot, const char *text, void (*on_click)(void *), void *ud);
    static void *cb_register_setting(void *host, const char *label, void *(*factory)(void *), void *ud);
    static int cb_inject_style(void *host, const char *qss);
    static int cb_override_page(void *host, int slot, void *(*factory)(void *), void *ud);
};
