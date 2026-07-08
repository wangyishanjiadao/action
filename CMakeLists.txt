cmake_minimum_required(VERSION 3.22)
project(AutoInstallProtected C)

set(CMAKE_C_STANDARD 99)

# 包含 jni 目录和根目录（用于读取生成的加密头文件）
include_directories(jni ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# 编译 service 二进制 (对应 jni/service.c)
add_executable(my_service jni/service.c)

# 编译 post_fs 二进制 (对应 jni/post_fs.c)
add_executable(my_post_fs jni/post_fs.c)
