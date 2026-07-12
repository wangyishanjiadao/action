cmake_minimum_required(VERSION 3.10)
project(mp3player C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =========================================================================
# 自动下载 miniaudio.h（单文件音频库）
# =========================================================================
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio.h")
    message(STATUS "正在下载 miniaudio.h ...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio.h"
        SHOW_PROGRESS
        STATUS dl_status
    )
    list(GET dl_status 0 dl_code)
    if(NOT dl_code EQUAL 0)
        message(FATAL_ERROR "miniaudio.h 下载失败")
    endif()
    message(STATUS "miniaudio.h 下载完成")
endif()

# =========================================================================
# 可执行文件
# =========================================================================
add_executable(mp3player main.c)

# 数学库 (miniaudio 内部使用)
target_link_libraries(mp3player PRIVATE m)

# 平台相关
if(ANDROID)
    # pthread 已内置于 Android Bionic libc，无需额外链接
    # miniaudio 通过 dlopen 动态加载音频后端（AAudio / OpenSL ES）
    message(STATUS "目标: Android ${ANDROID_ABI} API ${ANDROID_PLATFORM}")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(mp3player PRIVATE Threads::Threads)
    message(STATUS "目标: ${CMAKE_SYSTEM_NAME}")
endif()

# =========================================================================
# 安装（可选）
# =========================================================================
install(TARGETS mp3player RUNTIME DESTINATION bin)
