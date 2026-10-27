#pragma once

// Kill Launcher 原生插件 API (C ABI)
// 插件 DLL 导出 kill_plugin_query / kill_plugin_load / kill_plugin_unload。
// 危险操作不暴露函数指针；UI 注入按权限门控。

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILL_PLUGIN_API_VERSION 2

typedef enum KillPerm {
    KILL_PERM_NONE            = 0,
    KILL_PERM_LOG             = 1ull << 0,
    KILL_PERM_UI              = 1ull << 1,
    KILL_PERM_READ_CONFIG     = 1ull << 2,
    KILL_PERM_REGISTER_HOOK   = 1ull << 3,
    KILL_PERM_WRITE_FILE      = 1ull << 10,
    KILL_PERM_DELETE_FILE     = 1ull << 11,
    KILL_PERM_RUN_PROCESS     = 1ull << 12,
    KILL_PERM_NETWORK         = 1ull << 13,
    KILL_PERM_WRITE_CONFIG    = 1ull << 14,
    KILL_PERM_UI_OVERRIDE     = 1ull << 15,
    KILL_PERM_FORBIDDEN_MASK  = 0xFFull << 32
} KillPerm;

#define KILL_PERM_SAFE_MASK (KILL_PERM_LOG | KILL_PERM_UI | KILL_PERM_READ_CONFIG | KILL_PERM_REGISTER_HOOK)
#define KILL_PERM_CONFIRM_MASK (KILL_PERM_WRITE_FILE | KILL_PERM_DELETE_FILE | KILL_PERM_RUN_PROCESS | KILL_PERM_NETWORK | KILL_PERM_WRITE_CONFIG | KILL_PERM_UI_OVERRIDE)

typedef enum KillPermLevel {
    KILL_PERM_LEVEL_SAFE = 0,
    KILL_PERM_LEVEL_CONFIRM = 1,
    KILL_PERM_LEVEL_FORBIDDEN = 2
} KillPermLevel;

typedef struct KillPluginInfo {
    const char *id;
    const char *name;
    const char *version;
    const char *author;
    const char *description;
    uint64_t    permissions;
    uint32_t    api_version;
} KillPluginInfo;

typedef enum KillUiSlot {
    KILL_SLOT_TOPBAR        = 0,
    KILL_SLOT_HOME          = 1,
    KILL_SLOT_HOME_BOTTOM   = 2,
    KILL_SLOT_SETTINGS      = 3,
    KILL_SLOT_DOWNLOAD      = 4,
    KILL_SLOT_MANAGE        = 5,
    KILL_SLOT_FLOATING      = 6
} KillUiSlot;

typedef struct KillHostApi {
    uint32_t struct_size;
    void    *host;
    void (*log)(void *host, int level, const char *message);
    void *(*register_page)(void *host, const char *category, const char *title,
                           void *(*factory)(void *plugin_userdata), void *plugin_userdata);
    void *(*register_hook)(void *host, const char *event,
                           void (*callback)(void *host, const char *event, const char *payload_json),
                           void *plugin_userdata);
    const char *(*get_config_json)(void *host);
    int  (*write_file)(void *host, const char *path, const void *data, size_t size);
    int  (*read_file)(void *host, const char *path, void *out, size_t max_size, size_t *out_size);
    int  (*delete_file)(void *host, const char *path);
    int  (*run_process)(void *host, const char *program, const char *args_json, int wait);
    int  (*http_request)(void *host, const char *method, const char *url,
                         const char *headers_json, const void *body, size_t body_size);
    void *(*inject_widget)(void *host, int slot, const char *title,
                           void *(*factory)(void *plugin_userdata), void *plugin_userdata);
    void *(*register_button)(void *host, int slot, const char *text,
                             void (*on_click)(void *plugin_userdata), void *plugin_userdata);
    void *(*register_setting)(void *host, const char *label,
                              void *(*factory)(void *plugin_userdata), void *plugin_userdata);
    int  (*inject_style)(void *host, const char *qss);
    int  (*override_page)(void *host, int slot,
                          void *(*factory)(void *plugin_userdata), void *plugin_userdata);
} KillHostApi;

typedef const KillPluginInfo *(*KillPluginQueryFn)(void);
typedef int (*KillPluginLoadFn)(const KillHostApi *api);
typedef void (*KillPluginUnloadFn)(void);

#define KILL_PLUGIN_EXPORT extern "C" __declspec(dllexport)

#ifdef __cplusplus
}
#endif
