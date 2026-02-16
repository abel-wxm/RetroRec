// ==========================================
// VERSION: 2026-02-16_22-56_FIX_FINAL
// ==========================================
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <string>
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
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        auto strokes = g_engine.getStrokes(); auto zones = g_engine.getMosaicZones();
        HPEN hp = CreatePen(PS_SOLID, 3, RGB(255, 0, 0)); HPEN ho = (HPEN)SelectObject(hdc, hp);
        for (const auto& p : strokes) { MoveToEx(hdc, p.x, p.y, NULL); LineTo(hdc, p.x+1, p.y+1); }
        SelectObject(hdc, ho); DeleteObject(hp);
        HPEN hmp = CreatePen(PS_DASH, 1, RGB(0, 0, 255)); HBRUSH hnb = (HBRUSH)GetStockObject(NULL_BRUSH);
        SelectObject(hdc, hmp); SelectObject(hdc, hnb);
        for (const auto& r : zones) Rectangle(hdc, r.x, r.y, r.x + r.w, r.y + r.h);
        DeleteObject(hmp); EndPaint(hWnd, &ps);
    } break;
    case WM_NCHITTEST: return (g_engine.isPaintMode() || g_engine.isMosaicMode()) ? HTCLIENT : HTTRANSPARENT;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE: if (wParam & MK_LBUTTON) { POINT pt; GetCursorPos(&pt); if (g_engine.isPaintMode()) g_engine.addStroke(pt.x, pt.y); else if (g_engine.isMosaicMode()) g_engine.addMosaic(pt.x-10, pt.y-10, 20, 20); } break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK ToolbarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindow("BUTTON", "Rec", WS_VISIBLE|WS_CHILD, 10, 10, 50, 30, hWnd, (HMENU)IDC_START, 0, 0);
        CreateWindow("BUTTON", "Stop", WS_VISIBLE|WS_CHILD, 65, 10, 50, 30, hWnd, (HMENU)IDC_STOP, 0, 0);
        CreateWindow("BUTTON", "Pause", WS_VISIBLE|WS_CHILD, 120, 10, 55, 30, hWnd, (HMENU)IDC_PAUSE, 0, 0);
        CreateWindow("BUTTON", "Pen", WS_VISIBLE|WS_CHILD, 185, 10, 50, 30, hWnd, (HMENU)IDC_PEN, 0, 0);
        CreateWindow("BUTTON", "Mosaic", WS_VISIBLE|WS_CHILD, 240, 10, 60, 30, hWnd, (HMENU)IDC_MOSAIC, 0, 0);
        CreateWindow("BUTTON", "Clear", WS_VISIBLE|WS_CHILD, 305, 10, 50, 30, hWnd, (HMENU)IDC_CLEAR, 0, 0);
        CreateWindow("BUTTON", "RetroFix", WS_VISIBLE|WS_CHILD, 360, 10, 75, 30, hWnd, (HMENU)IDC_RETRO, 0, 0);
        SetTimer(hWnd, 1, 33, NULL); break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_START: g_engine.startRecording(); break;
        case IDC_STOP: g_engine.stopRecording(); MessageBox(hWnd, "Saved!", "RetroRec", MB_OK); break;
        case IDC_PAUSE: if (g_engine.isPaused()) g_engine.resumeRecording(); else g_engine.pauseRecording(); break;
        case IDC_PEN: g_engine.togglePaintMode(); break;
        case IDC_MOSAIC: g_engine.toggleMosaicMode(); break;
        case IDC_CLEAR: g_engine.clearEffects(); break;
        case IDC_RETRO: g_engine.applyRetroactiveMosaic(); MessageBox(hWnd, "Retro-Mosaic Applied!", "RetroRec", MB_OK); break;
        } break;
    case WM_TIMER: InvalidateRect(hOverlay, NULL, TRUE); break;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    WNDCLASSEX wc1 = { sizeof(WNDCLASSEX), CS_HREDRAW|CS_VREDRAW, ToolbarProc, 0,0
