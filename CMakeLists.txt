cmake_minimum_required(VERSION 3.10.2)
project("MagiskProtectedNative")

# 1. 编译开机早期自愈程序 (确保中间是斜杠 /)
add_executable(my_post_fs jni/post_fs.c)

# 2. 编译后台常驻服务程序 (确保中间是斜杠 /)
add_executable(my_service jni/service.c)
