cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找依赖包 (FFmpeg 等)
find_package(FFmpeg REQUIRED)

# 2. 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/src)

# 3. 智能搜集代码 (核心修复：自动扫描，不报错！)
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp" "src/core/*.cpp" "src/core/*.hpp" "src/ui/*.hpp")

# 4. 生成可执行文件 (WIN32 模式)
add_executable(RetroRec WIN32 ${SOURCES})

# 5. 链接库 (修复链接问题)
target_link_libraries(RetroRec PRIVATE 
    FFmpeg::avcodec 
    FFmpeg::avformat 
    FFmpeg::avutil 
    FFmpeg::swscale
    d3d11
    dxgi
    d3dcompiler
)
