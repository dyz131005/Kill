cmake_minimum_required(VERSION 3.21)
project(Kill VERSION 0.1.0 LANGUAGES CXX)

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

    add_custom_command(TARGET Kill POST_BUILD
        # 创建输出目录
        COMMAND ${CMAKE_COMMAND} -E make_directory "${KILL_DIST_DIR}"
        # 复制更新日志（关于页面运行期从程序目录读取）
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/updatelog.txt"
            "${KILL_DIST_DIR}/updatelog.txt"
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
endif()
