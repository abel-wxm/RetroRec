#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <iostream>
#include "RecorderEngine.hpp" // 引入刚才写的引擎

// 全局引擎实例
retrorec::RecorderEngine g_engine;

// ============================================================================
// 窗口回调函数
// ============================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rect;
            GetClientRect(hWnd, &rect);
            
            // 核心逻辑：根据引擎状态显示不同的文字
            const char* msg = g_engine.isReady() 
                ? "RetroRec v1.0 Ready!\n[DXGI Capture Initialized Successfully]" 
                : "Initializing GPU... (Check your monitor)";
                
            DrawTextA(hdc, msg, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ============================================================================
// 程序入口
// ============================================================================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    // 1. 注册窗口类
    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.hInstance      = hInstance;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszClassName  = L"RetroRecWindowClass";

    if (!RegisterClassExW(&wcex)) return 0;

    // 2. 创建窗口
    HWND hWnd = CreateWindowW(L"RetroRecWindowClass", L"RetroRec v1.0", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 0;

    // 3. 尝试初始化引擎 (这会触发 DXGI 抓屏初始化)
    g_engine.initialize();

    // 4. 显示窗口
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 5. 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
}
