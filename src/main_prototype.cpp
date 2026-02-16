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
        
        // 显示文字
        std::string msg;
        if (g_engine.isRecording()) {
            msg = "RECORDING... (Press F10 to Stop)";
            SetTextColor(hdc, RGB(255, 0, 0)); // 红字
        } else {
            msg = g_engine.isReady() 
                ? "RetroRec v1.0 Ready!\n\n[F9] Start Recording\n[F10] Stop Recording" 
                : "Initializing GPU...";
            SetTextColor(hdc, RGB(0, 0, 0)); // 黑字
        }
        
        // --- 核心修复在这里 ---
        // 之前多写了一个逗号，现在改成了正确的 5 个参数
        DrawTextA(hdc, msg.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // 修复了这里
        
        EndPaint(hWnd, &ps);
    } break;

    case WM_KEYDOWN:
        if (wParam == VK_F9) { // F9 开始
            if (!g_engine.isRecording()) {
                // 启动录制，保存为 output.mp4
                if (g_engine.startRecording("output.mp4")) {
                    InvalidateRect(hWnd, nullptr, TRUE);
                }
            }
        }
        else if (wParam == VK_F10) { // F10 停止
            if (g_engine.isRecording()) {
                g_engine.stopRecording();
                InvalidateRect(hWnd, nullptr, TRUE);
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

    // 游戏级循环
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            if (g_engine.isRecording()) {
                g_engine.captureFrame(); // 录制时不闲着
            }
        }
    }
    return (int)msg.wParam;
}
