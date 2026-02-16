cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找依赖包 (FFmpeg 等)
find_package(FFmpeg REQUIRED)

# 2. 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/src)

# 3. 自动搜集 src 文件夹及其子文件夹下的所有代码
# 这样以后你新建文件，不需要改这里，它自己能找到
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp" "src/core/*.cpp" "src/core/*.hpp")

# 4. 生成可执行文件
# ⚠️ 关键修改：加上 "WIN32" 关键字，告诉系统这是一个 GUI 程序
add_executable(RetroRec WIN32 ${SOURCES})

# 5. 链接库 (加入了 d3d11, dxgi 等系统库，为抓屏做准备)
target_link_libraries(RetroRec PRIVATE 
    FFmpeg::avcodec 
    FFmpeg::avformat 
    FFmpeg::avutil 
    FFmpeg::swscale
    d3d11
    dxgi
    d3dcompiler
)
