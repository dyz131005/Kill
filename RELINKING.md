# 如何重新链接（LGPLv3 合规说明）

本项目以静态链接方式使用 Qt 6.8.4（LGPLv3）。根据 LGPLv3，你有权：

- 获取所用 Qt 的完整源码；
- 修改 Qt 源码；
- 用你修改后的 Qt 重新链接本项目，得到可运行的 exe。

下面说明如何操作。

---

## 1. 获取 Qt 源码

本项目使用的静态 Qt 源码位于仓库的：

`
qt-static-src/
`

若该目录未包含在仓库中，请从对应 release 页面的源码附件获取，或按下方「上游获取」方式拉取同版本 Qt 6.8.4 并应用本项目的 Win7 兼容补丁。

上游获取：
- Qt 6.8.4 源码：https://download.qt.io/official_releases/qt/6.8/6.8.4/submodules/
- 本项目所用的 Win7 backport 补丁来自：https://github.com/crystalidea/qt6windows7
- 本项目额外修改：见 THIRD_PARTY_NOTICES.md 第 1.1 节

---

## 2. 修改 Qt（可选）

直接编辑 qt-static-src/ 下的任意 Qt 源码即可。例如修改 qtbase/src/... 后重新构建该模块。

---

## 3. 构建静态 Qt（若你改了 Qt 源码）

本项目的静态 Qt 构建产物路径（示例）：

`
D:/program/qt-static-ssl/6.8.4/msvc2022_64
`

改完 Qt 源码后，重新编译并安装静态 Qt 到该路径（或你自己的路径）。

---

## 4. 重新链接本项目

本项目提供一键构建脚本 build_static.bat。它会：

1. 初始化 MSVC 环境；
2. 用你当前的静态 Qt 路径做 CMake 配置；
3. 编译并静态链接出 dist/Kill.exe；
4. 自动修补 Win7 导入表。

在仓库根目录执行：

`bat
build_static.bat
`

如需指向你自己编译的 Qt，修改 build_static.bat 中的 QT_STATIC_DIR 变量即可。

---

## 5. 验证

构建成功后，产物在：

`
dist/Kill.exe
`

它是单文件静态链接 exe，可直接运行。

---

## 6. 说明

- 本项目的应用层源码以 MIT 发布，Qt 部分以 LGPLv3 提供。
- 你无需向本项目回传你对 Qt 的修改（LGPLv3 不要求），但若你分发修改后的 Qt，需遵守 LGPLv3。
- 完整第三方声明见 THIRD_PARTY_NOTICES.md，LGPLv3 全文见 LICENSE.LGPLv3。
