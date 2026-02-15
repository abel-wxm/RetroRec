cmake_minimum_required(VERSION 3.15)
project(RetroRec)

# 设置 C++ 标准为 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 寻找 FFmpeg 库 (这是最难配置的部分，我们用 vcpkg 来自动处理)
find_package(FFMPEG REQUIRED)

# 寻找 Windows 系统原生库 (DirectX, Audio, etc.)
# 这些是 Windows 自带的，不需要额外下载，但需要告诉编译器去链接它们
set(SYSTEM_LIBS
    d3d11
    dxgi
    ole32
    mmdevapi
    uuid
    dwmapi
    shcore
)

# 包含头文件路径
include_directories(${CMAKE_SOURCE_DIR}/src)

# 定义可执行文件，以及它包含的所有源文件
add_executable(RetroRec
    src/main_prototype.cpp
    src/RecorderEngine.hpp
    src/core/DXGICapture.hpp
    src/core/RingBuffer.hpp
    src/core/VideoEncoder.hpp
    src/core/AudioCapture.hpp
    src/ui/OverlaySystem.hpp
)

# 链接库文件
target_link_libraries(RetroRec PRIVATE ${SYSTEM_LIBS} ${FFMPEG_LIBRARIES})

# 复制 dll 到输出目录 (Windows 运行需要)
if(WIN32)
    add_custom_command(TARGET RetroRec POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:RetroRec>
        ${CMAKE_BINARY_DIR}/RetroRec.exe
    )
endif()
