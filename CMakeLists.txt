# ==========================================
# VERSION: V1.0_2026-02-16_18-00 (Final)
# ==========================================
cmake_minimum_required(VERSION 3.10)
project(RetroRec)

set(CMAKE_CXX_STANDARD 17)

find_package(FFMPEG QUIET)
if (NOT FFMPEG_FOUND)
    find_package(FFmpeg REQUIRED)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)
if (FFMPEG_INCLUDE_DIRS)
    include_directories(${FFMPEG_INCLUDE_DIRS})
endif()

add_executable(RetroRec WIN32 
    src/main_prototype.cpp 
    src/RecorderEngine.hpp
)

if (FFMPEG_LIBRARIES)
    target_link_libraries(RetroRec PRIVATE 
        ${FFMPEG_LIBRARIES}
        d3d11 dxgi d3dcompiler user32 gdi32 mmdevapi dwmapi ole32
    )
else()
    target_link_libraries(RetroRec PRIVATE 
        FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
        d3d11 dxgi d3dcompiler user32 gdi32 mmdevapi dwmapi ole32
    )
endif()
