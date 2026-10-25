cmake_minimum_required(VERSION 3.21)
project(Kill VERSION 1.6.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 构建产物直接输出到 dist（不再经由安装程序）
set(KILL_DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${KILL_DIST_DIR}")

# 默认使用静态 Qt 单文件构建
option(KILL_STATIC_QT "Build against a static Qt toolchain" ON)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)
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

if(WIN32)
    target_sources(Kill PRIVATE resources/app.rc)
endif()

target_include_directories(Kill PRIVATE src)
target_link_libraries(Kill PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)

if(WIN32)
    target_link_libraries(Kill PRIVATE userenv shell32)
endif()

# ===== 构建后处理 =====
if(WIN32)
    add_custom_command(TARGET Kill POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${KILL_DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/icon.ico"
            "${KILL_DIST_DIR}/icon.ico"
        COMMENT "Copying icon.ico to dist"
    )

    if(KILL_STATIC_QT)
        message(STATUS "Static Qt build: single-file output, skipping windeployqt.")

        # MSVC 14.4x 的静态 CRT 硬链接了 Win8+ 的 GetSystemTimePreciseAsFileTime，
        # 在 Win7 上会报「无法定位输入点」。编译后把导入名改写为
        # GetSystemTimeAsFileTime，使 Win7 可正常加载。
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_custom_command(TARGET Kill POST_BUILD
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/scripts/patch_win7_import.py"
                    "$<TARGET_FILE:Kill>"
                COMMENT "Patching Win7-incompatible import (GetSystemTimePreciseAsFileTime)"
            )
        else()
            message(WARNING "Python3 not found: Win7 import patch skipped, exe may fail on Windows 7.")
        endif()
    else()
        find_program(WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            PATHS ${Qt6_DIR}/../../bin
            DOC "Path to the windeployqt tool"
        )
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET Kill POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    "${KILL_DIST_DIR}/$<TARGET_FILE_NAME:Kill>"
                COMMENT "Deploying Qt dependencies using windeployqt"
            )
        else()
            message(WARNING "windeployqt not found! DLL deployment disabled.")
        endif()
    endif()
endif()
