# Kill

> ## 📢 关于下载功能的严正公告
>
> 近期，由于个别人士的恶意举报行为，部分启动器相继关停了下游下载入口。我们对此**表示强烈谴责与坚决反对**。
>
> 恶意举报不是「维护秩序」，而是**对开源社区和 MC 生态的严重破坏**。这种行为损害的是所有玩家的利益：它让免费工具被迫收缩、让新玩家无从入门、让生态被无谓的恐惧笼罩。**一颗老鼠屎，不该坏掉一整锅粥。**
>
> **Kill 的立场，绝不妥协：**
>
> - **我们坚决反对恶意举报。** 任何以举报为手段打压正常开源项目的行径，都是对社区繁荣发展的严重破坏，我们对此深恶痛绝。
> - **我们不下架、不关闭、不隐藏任何下载功能。** 版本下载、模组 / 整合包 / 资源包 / 数据包下载入口将**永久保持开放**。
> - **我们会持续投入并维护下载能力。** 原版版本清单获取、BMCLAPI 镜像加速、Fabric / Forge 加载器安装、Modrinth / CurseForge 资源下载——整条链路照常运转，一天不关。
> - **我们尊重每一位用户。** 用不用、用哪个，是你的自由；做好工具、守住功能，是我们的责任。
>
> **只要 Kill 还在一天，下载功能就一天不会消失。** 恶意举报可以关掉入口，但关不掉我们继续写下去的意志。
>
> 感谢每一位真正热爱 MC、真正支持开源社区的玩家。
>
> ## 📢 关于 PCL2 作者账号被封的声明
>
> 2026 年 10 月 22 日前后，PCL2 作者龙腾猫跃的 GitHub 账号被封禁。在此之前的四天里，PCL 先后经历了「停止提供下载功能」的公告、每分钟 1.54 亿次请求的 DDoS 攻击（总计 21 亿次，来自超过 10000 个 IPv4 地址，直接损失超过 6000 元），以及网络上针对其公告系统的「后门」指控。四件事在四天内接连发生，节奏紧凑得让人无法视作巧合。
>
> **Kill 对此的态度，必须说清楚。**
>
> 我们不打算在这里给谁定罪。GitHub 至今没有公开封禁的具体理由，龙腾猫跃本人也未对封禁原因作出确切说明。在没有官方结论的前提下，把矛头指向某个具体的人或群体，既不负责任，也解决不了任何问题。但**这不代表我们可以对已经发生的事实保持沉默**。
>
> **我们痛斥的，是一种手段。** 以举报为武器，把一个仍在活跃维护、仍有人在用的开源项目从平台上抹去；以 DDoS 为手段，让一个个人开发者为自己的服务器账单买单；以「可能存在的风险」为由，在没有任何司法或平台裁定之前，就把一个项目钉在耻辱柱上。这套组合拳打下来，输的不是某一个开发者，而是所有还在写代码、还在分享、还在免费为社区做东西的人。一个生态如果让「举报」成为成本最低、见效最快的攻击方式，那它迟早会把所有认真做事的人赶走。留下来的，只会是最会举报的人。
>
> **我们同样要说清楚：指出技术风险本身没有错。** 有人指出 PCL 的公告系统在技术上具备写入设置项的能力，这不是造谣，代码确实存在这样的路径。发现问题、公开讨论、推动修复，这是开源社区赖以运转的机制。但「指出问题」和「借此把一个项目连根拔起」是两件性质完全不同的事。前者是建设，后者是破坏。把两者混为一谈，用技术讨论的正当性去掩盖举报打压的实质，这是我们无法接受的。
>
> **因此，Kill 在此正式呼吁：**
>
> - **呼吁 GitHub 尽快复核并恢复 PCL2 作者的账号。** 如果封禁是基于具体违规，请公开说明依据；如果只是批量举报触发的自动化处置，请尽快纠正。一个拥有数十万用户的项目的维护者，不应该在没有明确说明的情况下从平台上消失。
> - **呼吁 GitHub 完善针对恶意批量举报的识别与申诉机制。** 举报功能的初衷是保护社区，不是让少数人用来清除异己。当举报成为一种可以批量、匿名、低成本使用的攻击工具时，平台有责任把这道口子堵上。
> - **呼吁所有还在做开源项目的人，把这件事记在心里。** 今天被封的是 PCL，明天可能是任何一个名字。我们无法预知下一次会轮到谁，但我们可以选择在这一次不装作没看见。
>
> **Kill 不站队，但 Kill 有立场。** 我们不参与对任何具体个人或组织的指控，也不传播未经证实的信息。我们只是认为：**一个健康的社区，不应该让举报者赢。**
>
> 这句话写在 README 里，不是一时情绪，而是留给将来某一天回看时的一个记号。


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
- **版本管理页（对标 HMCL GameListPage）**：「版本」子页为实例卡片列表，顶部工具栏提供刷新 / 安装新游戏 / 打开目录 / 删除与搜索框；每张实例卡片显示类型图标、实例名与状态（当前选中 / 已安装），右侧箭头按钮，底部 1px 分隔线，选中态浅蓝背景。列表支持关键字过滤，并与版本下拉双向同步。

### 📋 日志 & 诊断
- **多候选目录日志写入**：优先顺序为 `%AppData%\KillLauncher\Kill\logs` → 程序目录 → 文档目录 → 临时目录，保证任何情况下都能写下日志。
- **Win32 API 级别报错**：路径逐段诊断（`mw_diagnosePathHierarchy`）、`CreateFileW` 真实错误码（`mw_win32ErrorMessage`）、路径字节十六进制转储（`mw_diagnoseHexDump`）、目录写入多参数矩阵探测，定位「系统找不到指定的文件」「拒绝访问」等因安全中心 ASR / Minifilter（如 `gameflt.sys`）拦截导致的写入失败。
- **日志写入防重入**：自定义 Guard 结构体防止日志写入失败再打日志造成的死循环。

### 🌐 下载系统
- **HMCL 风格版本下载页**：顶部工具卡片（搜索框 + 类型筛选 + 刷新按钮同处一行），下方整宽版本列表，每项显示类型图标、版本号与发布时间两行信息；类型筛选对齐 HMCL（正式版 / 快照版 / 愚人节版 / 远古版 / 所有版本），默认选中正式版。
- **内嵌安装流程（对标 HMCL InstallersPage）**：点击版本项直接切换到安装页（主窗口内，非独立弹窗），左上角返回按钮；页面自上而下为实例名称卡片（含清空 / 恢复默认名称）、游戏版本信息条、加载器卡片网格、底部右对齐安装按钮。
- **加载器卡片网格**：按 HMCL 规格实现（180×126 尺寸、4px 圆角、16px 间距、32px 图标居中），状态显示未安装 / 已选版本号；卡片按游戏版本裁剪——1.12.2 提供 Forge / Cleanroom / LiteLoader / LegacyFabric / OptiFine，≤1.13.2 提供 Forge / LiteLoader / OptiFine / LegacyFabric，更高版本提供 Forge / NeoForge / Fabric / Quilt / OptiFine。
- **加载中 / 加载失败状态页**：版本列表加载时显示提示，失败可点击重试。
- **版本图标随程序打包**：图标资源经 Qt RCC 嵌入 `Kill.exe`，无需额外文件。
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
```

---

## 🔨 构建方式

本项目不使用 `.pro`，直接使用 CMake + Qt6 + C++20，默认构建静态 Qt 单文件。

### 1. 环境要求

- Qt：静态构建的 Qt 6.8（本仓库使用 `D:\program\qt-static\6.8.4\msvc2022_64`），模块至少包含 `Core`、`Gui`、`Widgets`、`Network`、`Svg`
- 编译器：MSVC（VS 2022 / 2026，默认 `/utf-8` 已在 CMake 中打开）
- CMake：3.21 及以上
- Ninja：推荐作为生成器
- Python 3：用于编译后自动修补 Win7 兼容的导入表

### 2. 构建示例

```powershell
cmake -S . -B build_static -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="D:/program/qt-static/6.8.4/msvc2022_64" `
  -DKILL_STATIC_QT=ON
cmake --build build_static --parallel
```

构建产物直接输出到 `dist/`：

```text
dist/
├─ Kill.exe      # 静态链接单文件，无 Qt DLL 依赖
└─ icon.ico
```

### 3. 直接运行

```powershell
.\dist\Kill.exe
```

### 4. 动态构建（仅开发调试）

若需用动态 Qt 快速调试，关闭静态开关即可，此时会调用 `windeployqt` 部署 Qt 运行库：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="D:/Qt/6.8.0/mingw_64" -DKILL_STATIC_QT=OFF
cmake --build build -j8
```

### 5. 关于静态 Qt 的 Win7 兼容

MSVC 14.4x 的静态 UCRT 硬链接了 Windows 8 才引入的 `GetSystemTimePreciseAsFileTime`，在 Win7 上启动会报「无法定位输入点」。构建流程会在链接完成后自动调用 `scripts/patch_win7_import.py`，把导入名改写为 Win7 可用的 `GetSystemTimeAsFileTime`，无需手动处理。

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
| CurseForge 页面一直空 | 没填 CF Core API Key | 设置里填 Key 后重启 |

