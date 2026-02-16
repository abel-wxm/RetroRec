#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "RecorderEngine.hpp"

retrorec::RecorderEngine g_engine;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        // 显示状态
        const char* msg = g_engine.isReady() ? "RetroRec v1.0 Ready! [GPU Capture OK]" : "Initializing GPU Failed...";
        DrawTextA(hdc, msg, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
    } break;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, "RetroRecClass", nullptr };
    RegisterClassExA(&wcex);
    HWND hWnd = CreateWindowA("RetroRecClass", "RetroRec v1.0", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return 0;

    g_engine.initialize(); // 启动引擎
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
