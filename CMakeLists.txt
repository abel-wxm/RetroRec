cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找 FFmpeg (关键修正：使用 FFmpeg 混合大小写，这是 vcpkg 的标准写法)
find_package(FFmpeg REQUIRED)

# 2. 包含头文件
include_directories(${CMAKE_SOURCE_DIR}/src)

# 3. 指定源文件 (保持不变)
add_executable(RetroRec WIN32 
    src/main_prototype.cpp 
    src/RecorderEngine.hpp
)

# 4. 链接库 (⚠️ 核心修复：这里显式链接了 FFmpeg 的具体模块)
# 之前缺了这几行，所以编译器找不到 avcodec 等函数
target_link_libraries(RetroRec PRIVATE 
    FFmpeg::avcodec 
    FFmpeg::avformat 
    FFmpeg::avutil 
    FFmpeg::swscale
    d3d11
    dxgi
    d3dcompiler
)
