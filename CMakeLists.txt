cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# 1. 找 FFmpeg
find_package(FFmpeg REQUIRED)

# 2. 包含头文件
include_directories(${CMAKE_SOURCE_DIR}/src)

# 3. 【核心修复】自动扫描 src 下所有的 .cpp 和 .hpp
# 不再手动列出文件名，这样就不会因为缺文件而报错了
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp")

# 4. 生成 GUI 程序 (WIN32)
add_executable(RetroRec WIN32 ${SOURCES})

# 5. 链接库
target_link_libraries(RetroRec PRIVATE 
    FFmpeg::avcodec 
    FFmpeg::avformat 
    FFmpeg::avutil 
    FFmpeg::swscale
    d3d11
    dxgi
    d3dcompiler
)
