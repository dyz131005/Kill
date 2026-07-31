# Kill

一个基于 **Qt Widgets + QSS + CMake + C++20** 的 Minecraft Java 版启动器项目，面向 Windows（MinGW / MSVC）环境，默认优先使用 **bangbang93 / BMCLAPI** 镜像源加速资源下载。支持 Fabric / Forge 模组加载器、Modrinth & CurseForge 模组搜索下载、版本隔离、实例目录管理和 Win32 级别的详细日志诊断。

---

## ✨ 功能特性

### 🏠 启动与账户
- **离线账户登录**：快速配置自定义玩家名。
- **Microsoft 正版账户 OAuth 导入**：支持 Azure AD 桌面客户端授权流程。
- **Java 自动探测 / 自动安装 / 自定义路径**：支持多版本 JDK 共存，可一键下载符合 Minecraft 版本要求的 JRE。
- **启动参数与内存设置**：Xmx / Xms、JVM 参数、游戏参数独立配置。
- **原版 Minecraft 版本清单获取与下载**：支持正式版、快照版，并按精确 MC 版本分组。
- **镜像源切换**：Mojang 官方 / bangbang93（BMCLAPI）一键切换，支持文件完整性校验开关。

### 🔧 模组加载器 & 依赖库
- **Fabric / Forge 支持**：使用各自官方安装器（`fabric-installer.jar` / `forge-installer.jar`）安装到指定 Minecraft 版本。
- **Maven 坐标解析**：`mavenCoordsToPath()` / `mavenBuildDownloadUrl()` 统一处理 Fabric/Forge 依赖库的 Maven 路径推导与下载地址拼接。
- **依赖库完整下载**：`installLibraries()` 同时支持 `downloads.artifact.url` 直链和 Maven 仓库推导地址，保证 `KnotClient` 等启动类不丢失。
- **类路径 / 模块路径构建**：`buildClasspath()` / `buildModulePath()` 优先使用 `downloads.artifact.path`，缺失时自动通过 Maven 坐标推导本地库路径。
- **版本继承解析**：递归合并继承链（`inheritsFrom`）中的 libraries、arguments、assetsIndex、mainClass 等元数据。

### 📦 模组 / 资源包 / 整合包
- **Modrinth 搜索与下载**：内置搜索、分页、版本详情、多文件下载队列。
- **CurseForge 搜索与下载**：可配置 CF API Key 后启用，未填 Key 时自动隐藏。
- **模组版本按精确 MC 版本分组**：参考 HMCL 实现 `ModVersionEntry`，区分「模组自身版本号 / 支持的 MC 版本 / 版本类型 / 加载器类型」，同一版本同时归入多个支持的 MC 版本分组。
- **目标目录统一管理**：模组 / 资源包 / 整合包均可选择安装到「当前实例（版本隔离）」或「全局游戏目录」。
- **模组下载路径与加载路径一致**：统一通过 `LauncherCore::versionModsDirectory()` 计算最终目录，避免出现「下载到 `versions/X/mods` 但 Fabric 读的是 `instances/X/mods`」导致模组加载不到的问题。

### 🗂️ 实例 & 版本隔离
- **版本隔离**开关：开启后每个版本拥有独立 `.minecraft`（位于 `instances/<versionId>/`），关则共用全局游戏目录。
- **实例工作目录**：`LauncherCore::instancesRoot()` 集中管理实例根；`effectiveVersionProfile()` 统一返回实际生效的配置档。

### 📋 日志 & 诊断
- **多候选目录日志写入**：优先顺序为 `%AppData%\KillLauncher\Kill\logs` → 程序目录 → 文档目录 → 临时目录，保证任何情况下都能写下日志。
- **Win32 API 级别报错**：路径逐段诊断（`mw_diagnosePathHierarchy`）、`CreateFileW` 真实错误码（`mw_win32ErrorMessage`）、路径字节十六进制转储（`mw_diagnoseHexDump`）、目录写入多参数矩阵探测，定位「系统找不到指定的文件」「拒绝访问」等因安全中心 ASR / Minifilter（如 `gameflt.sys`）拦截导致的写入失败。
- **日志写入防重入**：自定义 Guard 结构体防止日志写入失败再打日志造成的死循环。

### 🌐 下载系统
- **多任务并发下载队列**：`DownloadManager` + `DownloadProgressDialog` 统一管理进度、重试、取消。
- **临时文件创建 & 目录创建**：加强了错误处理，输出 Win32 错误描述；使用 `QDir().mkpath()` + 原生 `CreateDirectoryW` 逐层构建目录，每级失败打印具体错误码。

---

## 🏗️ 技术栈

| 模块 | 技术 |
|------|------|
| UI | Qt 6 Widgets + QSS（`assets/style.qss`） |
| 网络 | Qt6::Network（HTTP / HTTPS） |
| 构建 | CMake 3.21+ / C++20 |
| 编译器 | MinGW g++ 或 MSVC（需启用 `/utf-8`） |
| 打包 | Inno Setup 6（`build.iss`） |
| 资源打包 | Qt RCC（`resources.qrc`） |

---

## 📁 项目结构

```text
Kill/
├─ assets/
│  └─ style.qss                 # 全局 QSS 样式（深色主题 / 卡片 / 按钮等）
├─ src/
│  ├─ main.cpp                  # 入口：应用实例、日志初始化、主窗口
│  ├─ launcher_types.h          # 基础类型定义：VersionManifest、Library、ModVersionEntry 等
│  ├─ config_store.h / .cpp     # 配置持久化（账号、Java、镜像源、内存、版本隔离等）
│  ├─ launcher_core.h / .cpp    # 核心：版本解析、Maven 坐标、依赖下载、类路径、启动参数、工作目录
│  ├─ main_window.h / .cpp      # 主窗口：版本列表、账号、设置、模组搜索、下载调度、版本分组 UI
│  ├─ download_manager.h / .cpp # 下载器：分块队列、临时文件、校验、并发控制
│  └─ download_progress_dialog.h / .cpp   # 下载进度弹窗
├─ dist/
│  └─ Kill/                     # windeployqt 自动部署产物（Kill.exe + Qt DLL + 插件）
│     └─ logs/                  # 运行期日志
├─ hmcl-reference/HMCL/         # HMCL 源码参考（用于版本分组 / 模组解析对齐 HMCL 行为）
├─ build/                       # CMake 构建目录
├─ CMakeLists.txt
├─ resources.qrc                # Qt 资源：图标、QSS 等
├─ icon.ico
├─ fabric-installer.jar         # Fabric 官方安装器
├─ forge-installer.jar          # Forge 官方安装器
├─ count_simple.ps1             # 代码行数统计脚本
└─ build.iss                    # Inno Setup 打包脚本（整机/用户安装双模式）
```

---

## 🔨 构建方式

本项目不使用 `.pro`，直接使用 **CMake + Qt6 + C++20**。

### 1. 环境要求

- **Qt**：6.6 及以上，模块至少包含 `Core`、`Gui`、`Widgets`、`Network`
- **编译器**：
  - MinGW：g++ 11+（推荐 MinGW-UCrt 64-bit）
  - MSVC：VS 2022（默认 `/utf-8` 已在 CMake 中打开）
- **CMake**：3.21 及以上

静态单文件可选：
```cmake
cmake ... -DKILL_STATIC_QT=ON
```
是否能真的单文件，仍取决于你使用的 Qt 是否为静态构建版本。

### 2. MinGW 构建示例

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="D:/Qt/6.8.0/mingw_64"
cmake --build build -j8
```

构建完成后 `build/Kill.exe` 会被自动复制到 `dist/Kill/`，并调用 `windeployqt` 部署所有 Qt 运行库 DLL 和插件（`platforms/`、`imageformats/`、`iconengines/`、`tls/`、`styles/` 等）。

### 3. 直接运行

```powershell
.\dist\Kill\Kill.exe
```

---

## 📦 打包成安装程序（Inno Setup）

项目根目录提供了 `build.iss`，使用 **Inno Setup 6** 编译即可生成 `dist\Kill安装程序.exe`。

脚本基于 adsCleaner / gunmu 等项目的同套模板，主要特性：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| **安装范围** | 为所有用户安装（管理员） | `PrivilegesRequiredOverridesAllowed=dialog` 弹窗，默认整机；也可切「仅当前用户」无需 UAC |
| **整机安装默认路径** | `C:\Program Files\Kill` | 通过 `{autopf}` 自动展开 |
| **用户安装默认路径** | `%LOCALAPPDATA%\Programs\Kill`（AppData 树下）| 通过 `{autopf}` 自动展开 |
| **自定义安装路径** | ✅ 始终显示 | `DisableDirPage=no` 强制开启，不会因检测到旧版本而被跳过 |
| **桌面快捷方式** | ⬜ 默认不勾选 | 用户自选 |
| **开始菜单文件夹** | ☑ 默认创建 | 可取消勾选以关闭创建（`Tasks.startmenu`） |

### 编译安装包

1. 安装 [Inno Setup 6](https://jrsoftware.org/isdl.php)
2. 打开 `build.iss` → Build → Compile，或命令行：
   ```powershell
   & "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" .\build.iss
   ```
3. 输出：`dist\Kill安装程序.exe`

---

## 🔐 正版登录（Microsoft OAuth）

需要自己提供 Microsoft OAuth **Client ID**（Azure AD 桌面应用）。流程：

1. 在 Azure Portal 创建「桌面和移动设备」应用，重定向 URI 填 `https://login.live.com/oauth20_desktop.srf`
2. 在启动器「账户」设置填入 **Client ID**
3. 点击「打开授权页」，浏览器中完成登录
4. 复制最终以 `login.live.com/oauth20_desktop.srf?...` 开头的完整回调 URL
5. 粘贴回启动器，自动解析 `code` 并换得 access_token / uuid / playerName

---

## 🦑 CurseForge API Key

CurseForge 官方接口对第三方启动器要求 CF Core API Key，未填写时：
- CurseForge 搜索/下载入口自动隐藏
- 仍可正常使用 Modrinth

---

## 🧪 常见问题定位

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 启动后 Fabric 报错 `ClassNotFoundException: KnotClient` | Fabric 依赖库（如 ASM、Mixin）未下载或未进入 classpath | 看 `dist\Kill\logs\` 里 `installLibraries` 段日志；检查 `libraries\org\ow2\asm\...` 等目录是否有 jar |
| 模组安装了但游戏里加载不到 | 以前 Kill 下载到 `versions/X/mods`，但 Fabric 读的是 `instances/X/mods` | 用最新版 Kill 重新下载（已统一走 `versionModsDirectory()`），或手动把 jar 搬到 `instances/<versionId>/mods/` |
| 日志里提示「无法创建目录 / 拒绝访问」但资源管理器能写 | Windows 安全中心 ASR 规则或 `gameflt.sys` 等 Minifilter 拦截特定进程路径 | 看日志输出的「路径逐段诊断 + Win32 错误码」；建议把实例 / 游戏目录迁到 D 盘 |
| 安装包向导里看不到「改路径」页 | 旧版本 AppId 残留导致进入维护模式 | 先卸载旧版 Kill，或在 `build.iss` 换一个 AppId GUID 重新编译 |
| CurseForge 页面一直空 | 没填 CF Core API Key | 设置里填 Key 后重启 |

---

## 📜 免责声明

本项目仅供学习和个人使用。Minecraft、Mojang、Microsoft、Modrinth、CurseForge、Forge、Fabric 等相关商标与服务归各自所有者所有。
