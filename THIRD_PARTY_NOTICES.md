# 第三方组件与许可声明

本项目（Kill Launcher）在构建与运行中使用了以下第三方组件。

---

## 1. Qt 6（LGPLv3）

- 组件：Qt 6.8.4（模块：Core、Gui、Widgets、Network、Svg，及 Core 私有头）
- 许可：GNU Lesser General Public License v3.0（LGPLv3）
- 上游：https://www.qt.io/
- 本项目以静态链接方式使用 Qt，产物为单文件 Kill.exe。

### 1.1 本项目对 Qt 源码所做的修改
为在 Windows 7 RTM 上运行，本项目对所用 Qt 源码做了以下修改（完整源码见仓库 qt-static-src/）：

1. qtbase/src/network/socket/qnativesocketengine_win.cpp
   - 在 Win8 以下不传 WSA_FLAG_NO_HANDLE_INHERIT（该 flag 仅 Win8+ 支持），改为创建后用 SetHandleInformation 清除继承位。修复 Win7 上 WSASocket 返回 WSAEPROTOTYPE 导致 Protocol type not supported。
2. 采用 crystalidea/qt6windows7 的 Win7 兼容 backport 补丁（qtbase 部分），包括但不限于 qeventdispatcher_win.cpp、qfunctions_win.cpp、qdnslookup_win.cpp、qt_winrtbase_p.h、qfutex_p.h 等。

上述修改后的 Qt 源码同样以 LGPLv3 授权提供，见 qt-static-src/。

### 1.2 LGPLv3 义务说明
- 本项目已提供所用 Qt 的完整源码（qt-static-src/）。
- 用户可修改 qt-static-src/ 中的 Qt 源码并重新链接生成本项目，步骤见 RELINKING.md。
- LGPLv3 协议全文见 LICENSE.LGPLv3。

---

## 2. OpenSSL（Apache-2.0）

- 组件：OpenSSL 3.0.x（静态链接，供 Qt Network 的 OpenSSL TLS 后端使用）
- 许可：Apache License 2.0
- 上游：https://www.openssl.org/

---

## 3. Qt 内置/捆绑的第三方库

静态 Qt 内部捆绑了以下组件（随 Qt 一起以各自协议提供）：

| 组件 | 许可 |
|------|------|
| zlib | zlib License |
| PCRE2 | BSD-3-Clause |
| libpng | libpng License |
| libjpeg-turbo | BSD-3-Clause / IJG |
| FreeType | FreeType License (FTL) / GPLv2 |
| HarfBuzz | MIT |
| double-conversion | BSD-3-Clause |

各组件完整许可文本见 Qt 源码对应目录（如 qt-static-src/qtbase/src/3rdparty/）。

---

## 4. 本项目自身代码

本项目自身源代码以 MIT License 发布，全文见 LICENSE。
（MIT 与 LGPLv3 兼容；Qt 部分仍受 LGPLv3 约束。）

---

## 5. 说明

- 本声明不改变各第三方组件原有许可。
- 若你分发本项目的二进制产物，请一并提供本文件、LICENSE.LGPLv3，并提供获取 qt-static-src/ 的方式。
