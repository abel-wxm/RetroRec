cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找 FFmpeg (全套组件)
find_package(FFMPEG QUIET)
if (NOT FFMPEG_FOUND)
    find_package(FFmpeg REQUIRED)
endif()

# 2. 包含头文件
include_directories(${CMAKE_SOURCE_DIR}/src)
if (FFMPEG_INCLUDE_DIRS)
    include_directories(${FFMPEG_INCLUDE_DIRS})
endif()

# 3. 源文件
add_executable(RetroRec WIN32 
    src/main_prototype.cpp 
    src/RecorderEngine.hpp
)

# 4. 链接库 (新增 swresample 和 Windows 音频库)
# 注意：vcpkg 的 ffmpeg 通常包含 swresample
if (FFMPEG_LIBRARIES)
    target_link_libraries(RetroRec PRIVATE 
        ${FFMPEG_LIBRARIES}
        d3d11 dxgi d3dcompiler
        mmdevapi # Windows 音频设备 API
    )
else()
    target_link_libraries(RetroRec PRIVATE 
        FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
        d3d11 dxgi d3dcompiler
        mmdevapi
    )
endif()
