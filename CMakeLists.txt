cmake_minimum_required(VERSION 3.10)
project(thread_pool)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 搜索当前目录下的所有源文件，并将名称保存到变量SRC_FILES中
aux_source_directory(${PROJECT_SOURCE_DIR}/src SRC_FILES)

# 指定可执行文件输出路径  bin是放置可执行文件的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 指定库文件输出路径 lib是放置库文件的路径
# LIBRARY_OUTPUT_PATH 可执行文件生成的路径(动态库、可执行文件等)
# set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 头文件路径
include_directories(${PROJECT_SOURCE_DIR}/include)

# 制作可执行文件
add_executable(thread_pool_project ${SRC_FILES})

# 制作动态库
# add_library(thread_pool SHARED ${SRC_FILES})

# 链接线程库
target_link_libraries(thread_pool_project pthread)

# 编译的时候
# 系统            本地/自己的
# /usr/lib      /usr/local/lib      找.a .so 
# /usr/include  /usr/local/include  找.h .hpp

#[[
运行的时候
/etc/ld.so.conf 文件 include /etc/ld.so.conf.d/*.conf
        |            意思是去找下面文件夹里面的文件
        v            根据里面的文件内容（文件路径）
/etc/ld.so.conf.d/ 文件夹

ldconfig  更新缓存：读取 /etc/ld.so.conf配置文件生成ld.so.cache的缓存文件。
运行在ld.so.cache文件找
]]

#[[
死锁问题解决:
ps -u 查看进程的PID
gdb attach PID // 进入调试模式
info threads // 查看线程
bt // 查看函数调用栈
thread 3 // 切换线程
]]