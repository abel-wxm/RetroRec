#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include "RecorderEngine.hpp"

// 全局引擎
retrorec::RecorderEngine g_engine;

// UI 控件 ID
#define IDC_BTN_START 101
#define IDC_BTN_PAUSE 102
#define IDC_BTN_STOP  103
#define IDC_BTN_PAINT 104
#define IDC_BTN_CLEAR 105

// 窗口句柄
HWND hBtnStart, hBtnPause, hBtnStop, hBtnPaint, hBtnClear;

// 格式化时间
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
    bool paint = g_engine.isPaintMode();

    // 按钮状态控制
    EnableWindow(hBtnStart, !rec);
    EnableWindow(hBtnPause, rec);
    EnableWindow(hBtnStop, rec);
    
    // 涂鸦按钮文字变化
    SetWindowTextA(hBtnPaint, paint ? "Exit Paint" : "Paint Mode");
    SetWindowTextA(hBtnPause, paused ? "Resume" : "Pause");

    // 强制重绘显示时间
    InvalidateRect(hWnd, nullptr, FALSE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // 创建按钮 (不再是只有文字，有了真正的交互)
        int y = 50;
        hBtnStart = CreateWindowA("BUTTON", "Start Rec", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
            10, y, 90, 30, hWnd, (HMENU)IDC_BTN_START, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        
        hBtnPause = CreateWindowA("BUTTON", "Pause", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            110, y, 80, 30, hWnd, (HMENU)IDC_BTN_PAUSE, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        hBtnStop = CreateWindowA("BUTTON", "Stop", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            200, y, 80, 30, hWnd, (HMENU)IDC_BTN_STOP, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        hBtnPaint = CreateWindowA("BUTTON", "Paint Mode", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            290, y, 100, 30, hWnd, (HMENU)IDC_BTN_PAINT, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            
        hBtnClear = CreateWindowA("BUTTON", "Clear", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            400, y, 60, 30, hWnd, (HMENU)IDC_BTN_CLEAR, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        EnableWindow(hBtnPause, FALSE);
        EnableWindow(hBtnStop, FALSE);
        SetTimer(hWnd, 1, 1000, NULL); // UI 刷新定时器
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
            MessageBoxA(hWnd, "Saved!", "RetroRec", MB_OK);
            break;
        case IDC_BTN_PAINT:
            g_engine.setPaintMode(!g_engine.isPaintMode());
            UpdateUI(hWnd);
            break;
        case IDC_BTN_CLEAR:
            g_engine.clearStrokes();
            break;
        }
    } break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        
        SetBkMode(hdc, TRANSPARENT);
        HFONT hFont = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, TEXT("Segoe UI"));
        SelectObject(hdc, hFont);

        std::string status;
        if (g_engine.isRecording()) {
            if (g_engine.isPaused()) {
                status = "PAUSED - " + formatTime(g_engine.getDuration());
                SetTextColor(hdc, RGB(200, 200, 0));
            } else {
                status = "RECORDING - " + formatTime(g_engine.getDuration());
                SetTextColor(hdc, RGB(255, 0, 0));
            }
        } else {
            status = "Ready to Record";
            SetTextColor(hdc, RGB(0, 0, 0));
        }

        // 显示提示文字
        TextOutA(hdc, 15, 15, status.c_str(), status.length());
        
        // 如果开启了涂鸦模式，显示提示
        if (g_engine.isPaintMode()) {
            SetTextColor(hdc, RGB(255, 0, 0));
            TextOutA(hdc, 300, 15, "[PAINT ON]", 10);
        }

        DeleteObject(hFont);
        EndPaint(hWnd, &ps);
    } break;

    case WM_TIMER:
        if (g_engine.isRecording()) {
            InvalidateRect(hWnd, nullptr, FALSE); // 刷新时间显示
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

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, "RetroRecClass", nullptr };
    RegisterClassExA(&wcex);

    // 创建一个扁平的工具栏窗口
    HWND hWnd = CreateWindowA("RetroRecClass", "RetroRec Toolbar", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 
        100, 100, 500, 140, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 0;

    g_engine.initialize();
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 设置窗口置顶，方便录制时操作
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
