#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "RecorderEngine.hpp"

retrorec::RecorderEngine g_engine;

// 窗口回调
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        
        // 显示操作指南
        std::string msg;
        if (g_engine.isRecording()) {
            msg = "🔴 RECORDING... (Press F10 to Stop)";
            SetTextColor(hdc, RGB(255, 0, 0)); // 录制时变红字
        } else {
            msg = g_engine.isReady() 
                ? "RetroRec v1.0 Ready!\n\n[F9] Start Recording\n[F10] Stop Recording" 
                : "Initializing GPU...";
            SetTextColor(hdc, RGB(0, 0, 0));
        }
        
        DrawTextA(hdc, msg.c_str(), -1, &rect, DT_CENTER | DT_VCENTER, DT_CENTER);
        EndPaint(hWnd, &ps);
    } break;

    case WM_KEYDOWN: // 键盘监听
        if (wParam == VK_F9) { // 按下 F9
            if (!g_engine.isRecording()) {
                // 录制到当前目录下的 output.mp4
                if (g_engine.startRecording("output.mp4")) {
                    InvalidateRect(hWnd, nullptr, TRUE); // 刷新界面文字
                }
            }
        }
        else if (wParam == VK_F10) { // 按下 F10
            if (g_engine.isRecording()) {
                g_engine.stopRecording();
                InvalidateRect(hWnd, nullptr, TRUE); // 刷新界面文字
                MessageBoxA(hWnd, "Video saved to 'output.mp4'", "RetroRec", MB_OK);
            }
        }
        break;

    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, "RetroRecClass", nullptr };
    RegisterClassExA(&wcex);
    HWND hWnd = CreateWindowA("RetroRecClass", "RetroRec v1.0", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 600, 400, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return 0;

    g_engine.initialize();
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // --- 核心修改：游戏级主循环 ---
    // 使用 PeekMessage 而不是 GetMessage，这样即使没有鼠标移动，
    // 我们也能在空闲时间不断调用 g_engine.captureFrame()
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // 空闲时：如果不忙着处理窗口消息，就去抓屏
            if (g_engine.isRecording()) {
                g_engine.captureFrame();
            }
        }
    }
    return (int)msg.wParam;
}
