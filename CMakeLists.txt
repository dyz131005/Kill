cmake_minimum_required(VERSION 3.21)
project(Kill VERSION 1.3.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(KILL_DIST_DIR "${CMAKE_SOURCE_DIR}/dist/Kill")

option(KILL_STATIC_QT "Build against a static Qt toolchain" OFF)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

set(KILL_SOURCES
    src/main.cpp
    src/launcher_types.h
    src/config_store.h
    src/config_store.cpp
    src/launcher_core.h
    src/launcher_core.cpp
    src/main_window.h
    src/main_window.cpp
    src/download_progress_dialog.h
    src/download_progress_dialog.cpp
    src/download_manager.h
    src/download_manager.cpp
    resources.qrc
)

qt_add_executable(Kill WIN32 ${KILL_SOURCES})

# Windows 可执行文件资源：exe 图标 + 版本信息（需 rc 编译器，不能靠 qrc）
if(WIN32)
    target_sources(Kill PRIVATE resources/app.rc)
endif()

target_include_directories(Kill PRIVATE src)
target_link_libraries(Kill PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

if(WIN32)
    target_link_libraries(Kill PRIVATE userenv shell32)
endif()

install(TARGETS Kill RUNTIME DESTINATION .)

# 部署所有必需的DLL和Qt组件
if(WIN32)
    # 首先查找windeployqt
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        PATHS ${Qt6_DIR}/../../bin
        DOC "Path to the windeployqt tool"
    )

    if(WINDEPLOYQT_EXECUTABLE)
        message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "windeployqt not found! DLL deployment disabled.")
    endif()

    # OpenSSL 运行库：部分 HTTPS 场景（如模组下载）会走 openssl 后端，
    # 缺失时运行期报 libssl 相关错误。优先使用 Qt 安装目录下的 libssl/libcrypto。
    set(KILL_OPENSSL_LIBSSL "")
    set(KILL_OPENSSL_LIBCRYPTO "")
    # Qt6_DIR 指向 <qt>/lib/cmake/Qt6，先推导出 Qt 安装根
    get_filename_component(_kill_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    file(GLOB _kill_ssl_candidates
        "${_kill_qt_prefix}/../../Tools/*/opt/bin/libssl-*.dll"
        "${_kill_qt_prefix}/../../Tools/*/bin/libssl-*.dll"
        "${_kill_qt_prefix}/../../Tools/*/*/bin/libssl-*.dll"
        "${_kill_qt_prefix}/bin/libssl-*.dll"
        "${_kill_qt_prefix}/../bin/libssl-*.dll"
    )
    if(NOT _kill_ssl_candidates)
        file(GLOB_RECURSE _kill_ssl_candidates
            "${_kill_qt_prefix}/../../Tools/*/libssl-*.dll"
        )
    endif()
    if(_kill_ssl_candidates)
        list(GET _kill_ssl_candidates 0 KILL_OPENSSL_LIBSSL)
        get_filename_component(_kill_ssl_dir "${KILL_OPENSSL_LIBSSL}" DIRECTORY)
        set(_kill_ssl_name_we "")
        get_filename_component(_kill_ssl_name_we "${KILL_OPENSSL_LIBSSL}" NAME_WE)
        # libssl-1_1-x64 -> libcrypto-1_1-x64 ；libssl-3-x64 -> libcrypto-3-x64
        string(REGEX REPLACE "^libssl" "libcrypto" _kill_crypto_name "${_kill_ssl_name_we}")
        set(_kill_crypto_path "${_kill_ssl_dir}/${_kill_crypto_name}.dll")
        if(EXISTS "${_kill_crypto_path}")
            set(KILL_OPENSSL_LIBCRYPTO "${_kill_crypto_path}")
        endif()
        message(STATUS "Found OpenSSL libssl: ${KILL_OPENSSL_LIBSSL}")
        if(KILL_OPENSSL_LIBCRYPTO)
            message(STATUS "Found OpenSSL libcrypto: ${KILL_OPENSSL_LIBCRYPTO}")
        endif()
    else()
        message(WARNING "OpenSSL libssl/libcrypto not found; HTTPS via OpenSSL backend may fail at runtime.")
    endif()

    add_custom_command(TARGET Kill POST_BUILD
        # 创建输出目录
        COMMAND ${CMAKE_COMMAND} -E make_directory "${KILL_DIST_DIR}"
        # 复制更新日志（关于页面运行期从程序目录读取）
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/updatelog.txt"
            "${KILL_DIST_DIR}/updatelog.txt"
        # 复制程序图标（安装包快捷方式 / 关于页面等引用）
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/icon.ico"
            "${KILL_DIST_DIR}/icon.ico"
        # 复制主可执行文件
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Kill>"
            "${KILL_DIST_DIR}/"
        # 使用windeployqt部署所有Qt DLL、插件和其他必需文件
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            "${KILL_DIST_DIR}/$<TARGET_FILE_NAME:Kill>"
        COMMENT "Deploying Qt dependencies using windeployqt"
    )

    # 复制 OpenSSL 运行库到 dist（存在才复制）
    if(KILL_OPENSSL_LIBSSL)
        add_custom_command(TARGET Kill POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${KILL_OPENSSL_LIBSSL}"
                "${KILL_DIST_DIR}/"
            COMMENT "Copying OpenSSL libssl to dist"
        )
    endif()
    if(KILL_OPENSSL_LIBCRYPTO)
        add_custom_command(TARGET Kill POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${KILL_OPENSSL_LIBCRYPTO}"
                "${KILL_DIST_DIR}/"
            COMMENT "Copying OpenSSL libcrypto to dist"
        )
    endif()
endif()
