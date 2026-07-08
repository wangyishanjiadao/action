cmake_minimum_required(VERSION 3.10.2)
project("my_service")

# 确保这里的路径是 jni/service.c，带有一个正斜杠 /
add_executable(my_service jni/service.c)
