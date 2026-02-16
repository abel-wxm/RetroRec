cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# --- 1. 暴力查找 FFmpeg (不猜了，全都试一遍) ---
find_package(FFMPEG QUIET)
if (NOT FFMPEG_FOUND)
    find_package(FFmpeg REQUIRED)
endif()

# --- 2. 打印调试信息 (让我们知道到底找到了什么) ---
message(STATUS "FFmpeg Found: ${FFMPEG_FOUND}")
message(STATUS "FFmpeg Include: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFmpeg Libs: ${FFMPEG_LIBRARIES}")

# --- 3. 强行指定头文件路径 (防止找不到 avcodec.h) ---
include_directories(${CMAKE_SOURCE_DIR}/src)
if (FFMPEG_INCLUDE_DIRS)
    include_directories(${FFMPEG_INCLUDE_DIRS})
endif()

# --- 4. 指定源文件 ---
add_executable(RetroRec WIN32 
    src/main_prototype.cpp 
    src/RecorderEngine.hpp
)

# --- 5. 暴力链接 (不管是变量还是 Target，能连的都连上) ---
if (FFMPEG_LIBRARIES)
    target_link_libraries(RetroRec PRIVATE ${FFMPEG_LIBRARIES})
else()
    # 如果变量为空，说明可能是模块化安装，尝试链接具体模块
    target_link_libraries(RetroRec PRIVATE 
        FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale
    )
endif()

# 别忘了系统的 DirectX 库
target_link_libraries(RetroRec PRIVATE d3d11 dxgi d3dcompiler)
