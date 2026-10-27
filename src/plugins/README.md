# Kill Launcher 插件系统

Kill 支持用原生 DLL 扩展启动器。插件可加界面、监听事件、读写文件等，但危险能力都受权限系统约束。

## 目录约定

插件放在 exe 同目录的 plugins/ 下：

Kill.exe / plugins/ / auth.json（权限授权记录，自动生成）/ hello_plugin.dll（你的插件）

启动器启动时扫描 plugins/*.dll，读取元信息，按授权情况加载。

## 插件契约

插件 DLL 必须导出 3 个 C 符号（见 plugin_api.h）：

- kill_plugin_query()  读元信息（无副作用）
- kill_plugin_load(api) 加载，返回 0 成功
- kill_plugin_unload()  卸载

## 权限三级

1. Safe（安全，免确认）：日志、注册界面、事件钩子、读配置。
2. Confirm（危险，首次需用户确认）：写/删文件、运行进程、网络、改配置。首次加载弹确认，同意后记入 plugins/auth.json，以后自动放行。
3. Forbidden（极度危险，直接禁止）：提权工具(nsudo)、删 system32、删游戏根目录、改 SAM/注册表 hive 等。API 根本不暴露；若插件在 permissions 里声明禁止位则整个插件拒绝加载；即使已授权，运行时调用仍经 PluginGuard 二次校验，命中黑名单一律拒绝。

### 禁止清单（节选）
- 路径：C:\Windows\System32、SysWOW64、System32\config（SAM 等 hive）、ProgramData\Microsoft\Crypto 等
- 命令：nsudo、takeown、icacls、reg add/delete、regedit、bcdedit、diskpart、format、vssadmin、wbadmin 等
- 行为：删除整个游戏根目录、删除 system32 等

## 宿主 API（KillHostApi）

宿主只把已获授权的能力填进函数指针表，未授权的一律为 NULL。

- log / register_page / register_hook / get_config_json —— Safe
- read_file / write_file / delete_file / run_process / http_request —— Confirm

## UI 注入能力（API v2，需 KILL_PERM_UI，override 类需 KILL_PERM_UI_OVERRIDE）

插件可大幅修改界面：

- inject_style(qss) —— 注入全局 QSS，覆盖外观/主题（需 UI_OVERRIDE）
- inject_widget(slot, title, factory) —— 向 slot 注入一个 QWidget（factory 返回 QWidget）
- register_button(slot, text, onClick) —— 加一个按钮
- register_setting(label, factory) —— 向设置区加一个设置项控件
- override_page(slot, factory) —— 用插件 widget 覆盖内置页面

slot 取值见 plugin_api.h 的 KillUiSlot（TOPBAR/HOME/HOME_BOTTOM/SETTINGS/DOWNLOAD/MANAGE/FLOATING）。

注意：inject_widget / override_page / register_setting 的 factory 必须返回 Qt 的 QWidget，
因此这类插件需链接 Qt（或用 Qt 编译）。纯 C++ 插件可用 inject_style + register_button。

示例见 example/plugin-ui-demo/（演示注入样式 + 按钮）。

## 安全设计要点
- 危险能力通过函数指针表按授权下发，未授权拿不到指针，物理上无法调用。
- 所有文件/进程操作在宿主回调里再次过 PluginGuard，双保险。
- 删除额外比对游戏根目录，禁止整树删除。
- 元信息在 query 阶段读取，此时不加载、不执行插件逻辑。

## 最小插件示例

见 example/plugin-hello/。编译（MSVC）：

cl /LD /EHsc /utf-8 /I <kill源码>/src/plugins hello_plugin.cpp /Fe:hello_plugin.dll

把生成的 dll 放进 plugins/，重启启动器即可。

## 事件钩子

register_hook(event, cb) 注册后，宿主在对应时机回调：
- app.startup  启动器启动完成
- game.before_launch  启动游戏前
- game.after_exit  游戏退出后

回调收到 JSON 字符串 payload。

## 限制
- 仅 Windows。
- 插件与宿主需使用兼容 MSVC CRT（建议同版本编译）。
- 插件与 Kill.exe 共享进程内存，能做的事很大——所以危险操作才要沙箱。请只安装信任来源的插件。
