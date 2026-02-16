#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include "RecorderEngine.hpp"

// 全局引擎
retrorec::RecorderEngine g_engine;

// --- 状态控制 ---
bool g_is_counting_down = false;
int g_countdown_num = 0;
const UINT_PTR TIMER_ID_COUNTDOWN = 1; 
const UINT_PTR TIMER_ID_UPDATE_UI = 2; // 用于刷新时间显示

// 辅助函数：把秒数变成 00:00:00
std::string formatDuration(double seconds) {
    int total_sec = (int)seconds;
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << h << ":"
       << std::setw(2) << std::setfill('0') << m << ":"
       << std::setw(2) << std::setfill('0') << s;
    return ss.str();
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        
        SetBkMode(hdc, TRANSPARENT);
        // 使用更清晰的字体
        HFONT hFont = CreateFont(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, TEXT("Segoe UI"));
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        std::string msg;
        
        if (g_is_counting_down) {
            // 1. 倒计时状态 (橙色)
            msg = "Get Ready...\nStarting in  " + std::to_string(g_countdown_num);
            SetTextColor(hdc, RGB(255, 128, 0)); 
        } 
        else if (g_engine.isRecording()) {
            if (g_engine.isPaused()) {
                // 2. 暂停状态 (黄色)
                msg = "⏸️ PAUSED\n" + formatDuration(g_engine.getRecordingDuration()) + "\n(Press F11 to Resume)";
                SetTextColor(hdc, RGB(200, 200, 0)); 
            } else {
                // 3. 录制状态 (红色)
                msg = "🔴 REC  " + formatDuration(g_engine.getRecordingDuration()) + "\n(F11 Pause / F12 Stop)";
                SetTextColor(hdc, RGB(220, 0, 0)); 
            }
        } 
        else {
            // 4. 待机状态 (黑色)
            msg = "RetroRec v0.95\n\n[F12] Start Recording\n(Auto-Minimize & Countdown)\n\n[F11] Pause/Resume";
            SetTextColor(hdc, RGB(0, 0, 0));
        }
        
        // 绘制文字 (5 参数版本，确保不报错)
        DrawTextA(hdc, msg.c_str(), -1, &rect, DT_CENTER | DT_VCENTER, DT_CENTER);
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        EndPaint(hWnd, &ps);
    } break;

    case WM_TIMER:
        if (wParam == TIMER_ID_COUNTDOWN) {
            g_countdown_num--;
            if (g_countdown_num <= 0) {
                // --- 倒计时结束 ---
                KillTimer(hWnd, TIMER_ID_COUNTDOWN);
                g_is_counting_down = false;
                
                // 1. 自动最小化，防止录到自己
                ShowWindow(hWnd, SW_MINIMIZE);
                Sleep(200); // 等动画播完
                
                // 2. 开始录制
                if (g_engine.startRecording()) {
                    // 开启 UI 刷新 (每秒刷新时间)
                    SetTimer(hWnd, TIMER_ID_UPDATE_UI, 1000, nullptr);
                }
            }
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        else if (wParam == TIMER_ID_UPDATE_UI) {
            // 录制时刷新界面 (如果窗口被还原，能看到时间在走)
            if (g_engine.isRecording() && !IsIconic(hWnd)) {
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        break;

    case WM_KEYDOWN:
        // --- F12: 开始 / 停止 ---
        if (wParam == VK_F12) { 
            if (g_engine.isRecording()) {
                // 停止录制
                g_engine.stopRecording();
                KillTimer(hWnd, TIMER_ID_UPDATE_UI);
                
                // 自动弹回窗口
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
                
                InvalidateRect(hWnd, nullptr, TRUE);
                MessageBoxA(hWnd, "Video Saved Successfully!", "RetroRec", MB_OK);
            } 
            else if (!g_is_counting_down) {
                // 启动倒计时
                g_is_counting_down = true;
                g_countdown_num = 3;
                SetTimer(hWnd, TIMER_ID_COUNTDOWN, 1000, nullptr);
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        // --- F11: 暂停 / 继续 ---
        else if (wParam == VK_F11) {
            if (g_engine.isRecording()) {
                if (g_engine.isPaused()) {
                    g_engine.resumeRecording();
                } else {
                    g_engine.pauseRecording();
                }
                InvalidateRect(hWnd, nullptr, TRUE);
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

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), nullptr, "RetroRecClass", nullptr };
    RegisterClassExA(&wcex);

    HWND hWnd = CreateWindowA("RetroRecClass", "RetroRec v0.95", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 600, 360, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return 0;

    g_engine.initialize();

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // 空闲时录制
            if (g_engine.isRecording()) {
                g_engine.captureFrame();
            } else {
                Sleep(1); 
            }
        }
    }
    return (int)msg.wParam;
}
