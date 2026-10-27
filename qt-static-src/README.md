# 静态 Qt 源码（LGPLv3 合规）

本项目以静态链接方式使用 Qt 6.8.4（LGPLv3）。根据 LGPLv3，必须向用户提供所用 Qt 的完整源码，以便用户修改后重新链接。

## 目录内容

`
qt-static-src/
├── qtbase-everywhere-opensource-src-6.8.4.tar.xz   # QtBase 上游源码
├── qtsvg-everywhere-opensource-src-6.8.4.tar.xz    # QtSvg 上游源码
└── patches/
    ├── qt6windows7/                                 # crystalidea 的 Win7 backport 补丁集
    └── kill-socket-win7.patch                       # 本项目额外的 socket Win7 修复
`

## 如何得到「本项目实际编译所用的 Qt 源码」

1. 解压 `qtbase-everywhere-opensource-src-6.8.4.tar.xz` 和 `qtsvg-everywhere-opensource-src-6.8.4.tar.xz`；
2. 把 `patches/qt6windows7/qtbase/` 覆盖到解压后的 qtbase 源码对应路径；
3. 应用 `patches/kill-socket-win7.patch`（针对 `qtbase/src/network/socket/qnativesocketengine_win.cpp`）；
4. 得到的就是本项目静态编译所用的 Qt 源码。

OpenSSL 3.0.x 为静态链接的第三方依赖，源码从 https://www.openssl.org/source/ 获取（Apache-2.0）。

## 重新链接

详见仓库根目录 `RELINKING.md`。

## 上游来源

- Qt 6.8.4：https://download.qt.io/official_releases/qt/6.8/6.8.4/submodules/
- qt6windows7 backport：https://github.com/crystalidea/qt6windows7
- OpenSSL：https://www.openssl.org/source/
