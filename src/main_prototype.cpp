// ==========================================
// VERSION: V1.0_2026-02-16_18-00 (UI Overlay)
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
#define IDC_PEN 3
#define IDC_MOSAIC 4

LRESULT CALLBACK OverlayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        auto strokes = g_engine.getStrokes();
        auto zones = g_engine.getMosaicZones();

        HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        for (const auto& p : strokes) {
            MoveToEx(hdc, p.x, p.y, NULL);
            LineTo(hdc, p.x+1, p.y+1);
        }
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

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
    case WM_NCHITTEST: {
        if (g_engine.isPaintMode() || g_engine.isMosaicMode()) return HTCLIENT;
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
        CreateWindow("BUTTON", "Record", WS_VISIBLE | WS_CHILD, 10, 10, 60, 30, hWnd, (HMENU)IDC_START, 0, 0);
        CreateWindow("BUTTON", "Stop", WS_VISIBLE | WS_CHILD, 80, 10, 60, 30, hWnd, (HMENU)IDC_STOP, 0, 0);
        CreateWindow("BUTTON", "Pen", WS_VISIBLE | WS_CHILD, 150, 10, 60, 30, hWnd, (HMENU)IDC_PEN, 0, 0);
        CreateWindow("BUTTON", "Mosaic", WS_VISIBLE | WS_CHILD, 220, 10, 60, 30, hWnd, (HMENU)IDC_MOSAIC, 0, 0);
        SetTimer(hWnd, 1, 30, NULL); 
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_START: g_engine.startRecording(); break;
        case IDC_STOP: g_engine.stopRecording(); MessageBox(hWnd, "Saved!", "RetroRec", MB_OK); break;
        case IDC_PEN: g_engine.togglePaintMode(); break;
        case IDC_MOSAIC: g_engine.toggleMosaicMode(); break;
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

    hToolbar = CreateWindowEx(WS_EX_TOPMOST, "ToolbarClass", "RetroRec V1.0", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 100, 100, 320, 100, 0, 0, hInstance, 0);
    ShowWindow(hToolbar, SW_SHOW);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    hOverlay = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, 
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
