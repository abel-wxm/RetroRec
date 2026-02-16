#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "RecorderEngine.hpp"

// 全局引擎实例
retrorec::RecorderEngine g_engine;

// 窗口回调函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        
        // 设置字体和背景透明
        SetBkMode(hdc, TRANSPARENT);
        
        // 根据状态显示不同颜色的文字
        std::string msg;
        if (g_engine.isRecording()) {
            msg = "🔴 RECORDING... (Press F12 to Stop)";
            SetTextColor(hdc, RGB(255, 0, 0)); // 红色
        } else {
            msg = g_engine.isReady() 
                ? "RetroRec v0.9 Ready!\n\n[F12] Start/Stop Recording" 
                : "Initializing GPU... Please Wait";
            SetTextColor(hdc, RGB(0, 0, 0)); // 黑色
        }
        
        // 绘制文字 (这里是修复后的 5 参数版本)
        DrawTextA(hdc, msg.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        EndPaint(hWnd, &ps);
    } break;

    case WM_KEYDOWN:
        // 改用 F12 作为开始/停止热键 (避开 F10 系统冲突)
        if (wParam == VK_F12) { 
            if (!g_engine.isRecording()) {
                // 启动录制 (不需要传文件名了，引擎自动生成)
                if (g_engine.startRecording()) {
                    InvalidateRect(hWnd, nullptr, TRUE); // 刷新界面变红
                }
            } else {
                // 停止录制
                g_engine.stopRecording();
                InvalidateRect(hWnd, nullptr, TRUE); // 刷新界面恢复
                // 弹窗提示 (这里没法轻易拿到文件名了，就提示已保存)
                MessageBoxA(hWnd, "Video saved with timestamp!", "RetroRec", MB_OK);
            }
        }
        break;

    case WM_DESTROY: 
        PostQuitMessage(0); 
        break;
        
    default: 
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 主入口
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // 1. 注册窗口类
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, "RetroRecClass", nullptr };
    RegisterClassExA(&wcex);

    // 2. 创建窗口
    HWND hWnd = CreateWindowA("RetroRecClass", "RetroRec v1.0", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 600, 400, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return 0;

    // 3. 初始化引擎
    g_engine.initialize();

    // 4. 显示窗口
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 5. 游戏级消息循环 (PeekMessage)
    // 这种循环方式允许我们在“没有消息”的时候也能干活（录制视频）
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // 空闲时间：如果正在录制，就抓取一帧
            if (g_engine.isRecording()) {
                g_engine.captureFrame();
            }
        }
    }
    return (int)msg.wParam;
}
