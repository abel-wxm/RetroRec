// ==========================================
// VERSION: 2026-02-16_14-45 (Fix Build)
// ==========================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include "RecorderEngine.hpp"

retrorec::RecorderEngine g_engine;

#define IDC_BTN_START 101
#define IDC_BTN_PAUSE 102
#define IDC_BTN_STOP  103
#define IDC_BTN_PEN   104
#define IDC_BTN_MOSAIC 105
#define IDC_BTN_CLEAR 106

HWND hBtnStart, hBtnPause, hBtnStop, hBtnPen, hBtnMosaic, hBtnClear;

std::string formatTime(double s) {
    int total = (int)s;
    int m = total / 60;
    int sec = total % 60;
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << m << ":" << std::setw(2) << std::setfill('0') << sec;
    return ss.str();
}

void UpdateUI(HWND hWnd) {
    bool rec = g_engine.isRecording();
    bool paused = g_engine.isPaused();
    bool pen = g_engine.isPaintMode();
    bool mosaic = g_engine.isMosaicMode();

    EnableWindow(hBtnStart, !rec);
    EnableWindow(hBtnPause, rec);
    EnableWindow(hBtnStop, rec);
    EnableWindow(hBtnPen, rec);
    EnableWindow(hBtnMosaic, rec);
    EnableWindow(hBtnClear, rec);

    SetWindowTextA(hBtnPause, paused ? "Resume" : "Pause");
    SetWindowTextA(hBtnPen, pen ? "[ PEN ON ]" : "Pen");
    SetWindowTextA(hBtnMosaic, mosaic ? "[ MOSAIC ]" : "Mosaic");

    InvalidateRect(hWnd, nullptr, FALSE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        int x = 10; int w = 80; int h = 30; int gap = 5;
        
        hBtnStart = CreateWindowA("BUTTON", "Record", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
            x, 40, w, h, hWnd, (HMENU)IDC_BTN_START, ((LPCREATESTRUCT)lParam)->hInstance, NULL); x += w + gap;
        
        hBtnPause = CreateWindowA("BUTTON", "Pause", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            x, 40, w, h, hWnd, (HMENU)IDC_BTN_PAUSE, ((LPCREATESTRUCT)lParam)->hInstance, NULL); x += w + gap;

        hBtnStop = CreateWindowA("BUTTON", "Stop", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            x, 40, w, h, hWnd, (HMENU)IDC_BTN_STOP, ((LPCREATESTRUCT)lParam)->hInstance, NULL); x += w + gap + 15;

        hBtnPen = CreateWindowA("BUTTON", "Pen", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            x, 40, w, h, hWnd, (HMENU)IDC_BTN_PEN, ((LPCREATESTRUCT)lParam)->hInstance, NULL); x += w + gap;

        hBtnMosaic = CreateWindowA("BUTTON", "Mosaic", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            x, 40, w, h, hWnd, (HMENU)IDC_BTN_MOSAIC, ((LPCREATESTRUCT)lParam)->hInstance, NULL); x += w + gap;
            
        hBtnClear = CreateWindowA("BUTTON", "Clear", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            x, 40, w, h, hWnd, (HMENU)IDC_BTN_CLEAR, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        UpdateUI(hWnd);
        SetTimer(hWnd, 1, 1000, NULL);
    } break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDC_BTN_START:
            if (g_engine.startRecording()) UpdateUI(hWnd);
            break;
        case IDC_BTN_PAUSE:
            if (g_engine.isPaused()) g_engine.resumeRecording();
            else g_engine.pauseRecording();
            UpdateUI(hWnd);
            break;
        case IDC_BTN_STOP:
            g_engine.stopRecording();
            UpdateUI(hWnd);
            MessageBoxA(hWnd, "Saved Successfully!", "RetroRec", MB_OK);
            break;
        case IDC_BTN_PEN:
            g_engine.togglePaintMode();
            UpdateUI(hWnd);
            break;
        case IDC_BTN_MOSAIC:
            g_engine.toggleMosaicMode();
            UpdateUI(hWnd);
            break;
        case IDC_BTN_CLEAR:
            g_engine.clearEffects();
            break;
        }
    } break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        
        SetBkMode(hdc, TRANSPARENT);
        HFONT hFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, TEXT("Segoe UI"));
        SelectObject(hdc, hFont);

        std::string status;
        if (g_engine.isRecording()) {
            status = g_engine.isPaused() ? "PAUSED" : "REC: " + formatTime(g_engine.getDuration());
            SetTextColor(hdc, RGB(255, 0, 0));
        } else {
            status = "RetroRec v1.0 - Ready";
            SetTextColor(hdc, RGB(0, 0, 0));
        }

        // 使用 TextOutA 替代 DrawTextA 避免任何参数歧义
        TextOutA(hdc, 15, 10, status.c_str(), (int)status.length());
        
        std::string hint = "";
        if (g_engine.isPaintMode()) hint = " [PEN MODE]";
        else if (g_engine.isMosaicMode()) hint = " [MOSAIC MODE]";
        
        if (!hint.empty()) {
            SetTextColor(hdc, RGB(0, 0, 200));
            TextOutA(hdc, 200, 10, hint.c_str(), (int)hint.length());
        }

        DeleteObject(hFont);
        EndPaint(hWnd, &ps);
    } break;

    case WM_TIMER:
        if (g_engine.isRecording()) InvalidateRect(hWnd, nullptr, FALSE);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, "RetroRecClass", nullptr };
    RegisterClassExA(&wcex);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    HWND hWnd = CreateWindowA("RetroRecClass", "RetroRec Toolbar", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 
        (screenW - 600) / 2, 0, 600, 120, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 0;

    g_engine.initialize();
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            g_engine.captureFrame();
        }
    }
    return (int)msg.wParam;
}
