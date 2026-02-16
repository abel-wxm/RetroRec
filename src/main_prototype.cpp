// ==========================================
// VERSION: V1.1_FIX_UI_PERF_2026-02-16
// ==========================================
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include "RecorderEngine.hpp"

retrorec::RecorderEngine g_engine;
HWND hToolbar, hOverlay;

#define IDC_START 1
#define IDC_STOP 2
#define IDC_PAUSE 3
#define IDC_PEN 4
#define IDC_MOSAIC 5
#define IDC_CLEAR 6
#define IDC_RETRO 7

LRESULT CALLBACK OverlayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        auto strokes = g_engine.getStrokes();
        auto zones = g_engine.getMosaicZones();

        // 绘制红线
        HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        for (const auto& p : strokes) {
            MoveToEx(hdc, p.x, p.y, NULL);
            LineTo(hdc, p.x+1, p.y+1);
        }
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

        // 绘制蓝色虚线框 (马赛克指示)
        HPEN hMPen = CreatePen(PS_DASH, 1, RGB(0, 0, 255));
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        SelectObject(hdc, hMPen);
        SelectObject(hdc, hNull);
        for (const auto& r : zones) {
            Rectangle(hdc, r.x, r.y, r.x + r.w, r.y + r.h);
        }
        DeleteObject(hMPen);
        EndPaint(hWnd, &ps);
    } break;
    
    // --- 关键修复：鼠标穿透控制 ---
    case WM_NCHITTEST: {
        // 如果开启了绘图/马赛克，拦截鼠标，视为客户区
        if (g_engine.isPaintMode() || g_engine.isMosaicMode()) return HTCLIENT;
        // 否则完全穿透，操作下面窗口
        return HTTRANSPARENT; 
    } break;

    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE: {
        if (wParam & MK_LBUTTON) {
            POINT pt; GetCursorPos(&pt);
            if (g_engine.isPaintMode()) g_engine.addStroke(pt.x, pt.y);
            else if (g_engine.isMosaicMode()) g_engine.addMosaic(pt.x-10, pt.y-10, 20, 20);
        }
    } break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK ToolbarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindow("BUTTON", "Rec", WS_VISIBLE | WS_CHILD, 10, 10, 50, 30, hWnd, (HMENU)IDC_START, 0, 0);
        CreateWindow("BUTTON", "Stop", WS_VISIBLE | WS_CHILD, 65, 10, 50, 30, hWnd, (HMENU)IDC_STOP, 0, 0);
        CreateWindow("BUTTON", "Pause", WS_VISIBLE | WS_CHILD, 120, 10, 50, 30, hWnd, (HMENU)IDC_PAUSE, 0, 0);
        CreateWindow("BUTTON", "Pen", WS_VISIBLE | WS_CHILD, 180, 10, 50, 30, hWnd, (HMENU)IDC_PEN, 0, 0);
        CreateWindow("BUTTON", "Mosaic", WS_VISIBLE | WS_CHILD, 235, 10, 60, 30, hWnd, (HMENU)IDC_MOSAIC, 0, 0);
        CreateWindow("BUTTON", "Clear", WS_VISIBLE | WS_CHILD, 300, 10, 50, 30, hWnd, (HMENU)IDC_CLEAR, 0, 0);
        CreateWindow("BUTTON", "RetroFix", WS_VISIBLE | WS_CHILD, 360, 10, 70, 30, hWnd, (HMENU)IDC_RETRO, 0, 0);
        SetTimer(hWnd, 1, 30, NULL);
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_START: g_engine.startRecording(); break;
        case IDC_STOP: g_engine.stopRecording(); MessageBox(hWnd, "Saved!", "RetroRec", MB_OK); break;
        case IDC_PAUSE: 
            if (g_engine.isPaused()) g_engine.resumeRecording(); else g_engine.pauseRecording(); 
            break;
        case IDC_PEN: g_engine.togglePaintMode(); break;
        case IDC_MOSAIC: g_engine.toggleMosaicMode(); break;
        case IDC_CLEAR: g_engine.clearEffects(); break;
        case IDC_RETRO: 
            g_engine.applyRetroactiveMosaic(); 
            MessageBox(hWnd, "Retroactive Mosaic Applied!", "RetroRec", MB_OK);
            break;
        }
        break;
    case WM_TIMER:
        InvalidateRect(hOverlay, NULL, TRUE);
        break;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    WNDCLASSEX wc1 = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, ToolbarProc, 0, 0, hInstance, 0, LoadCursor(0, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), 0, "ToolbarClass", 0 };
    RegisterClassEx(&wc1);
    WNDCLASSEX wc2 = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, OverlayProc, 0, 0, hInstance, 0, LoadCursor(0, IDC_ARROW), 0, 0, "OverlayClass", 0 };
    RegisterClassEx(&wc2);

    // --- 修复点：窗口居中 ---
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 450, h = 100;
    hToolbar = CreateWindowEx(WS_EX_TOPMOST, "ToolbarClass", "RetroRec V1.1", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 
        (sw - w)/2, 100, w, h, 0, 0, hInstance, 0);
    ShowWindow(hToolbar, SW_SHOW);

    // Overlay 必须全屏且不带 WS_EX_TRANSPARENT (动态控制穿透)
    hOverlay = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, 
        "OverlayClass", "", WS_POPUP, 0, 0, sw, sh, hToolbar, 0, hInstance, 0);
    SetLayeredWindowAttributes(hOverlay, 0, 0, LWA_COLORKEY);
    ShowWindow(hOverlay, SW_SHOW);

    g_engine.initialize();

    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        g_engine.captureFrame();
    }
    return (int)msg.wParam;
}
