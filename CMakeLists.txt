cmake_minimum_required(VERSION 3.10)
project(AutoInstallProtected C)

set(CMAKE_C_STANDARD 99)

# 包含 jni 目录和根目录（用于读取生成的加密头文件）
include_directories(jni ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# 编译 my_service 二进制
add_executable(my_service jni/my_service.c)

# 编译 my_post_fs 二进制
add_executable(my_post_fs jni/my_post_fs.c)
