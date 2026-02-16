cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找 FFmpeg (同时尝试大写和小写，防止找不到)
find_package(FFMPEG QUIET)
if (NOT FFMPEG_FOUND)
    find_package(FFmpeg REQUIRED)
endif()

# 2. 包含头文件
include_directories(${CMAKE_SOURCE_DIR}/src)

# 3. 【绝对稳妥的做法】手动列出你现在仅有的两个文件
# 只要这两个文件在，它就绝对不会报错
add_executable(RetroRec WIN32 
    src/main_prototype.cpp 
    src/RecorderEngine.hpp
)

# 4. 链接库
# 使用变量链接，这样无论 vcpkg 怎么定义都能连上
target_link_libraries(RetroRec PRIVATE 
    ${FFMPEG_LIBRARIES}
    d3d11
    dxgi
    d3dcompiler
)
