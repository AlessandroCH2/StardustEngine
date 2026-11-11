#ifdef _WIN32
#include "TitleBar.h"
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace Stardust::TitleBar {

static WNDPROC s_Original = nullptr;
static float s_Height = 0, s_StartX = 0, s_EndX = 0, s_HoleStart = 0, s_HoleEnd = 0;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCALCSIZE && wParam) {
        // Let Windows size the side and bottom resize borders, but give the top back to the client area
        auto* params = (NCCALCSIZE_PARAMS*)lParam;
        const LONG top = params->rgrc[0].top;
        LRESULT result = CallWindowProcW(s_Original, hwnd, message, wParam, lParam);
        params->rgrc[0].top = top;
        if (IsZoomed(hwnd)) params->rgrc[0].top += GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        return result;
    }
    if (message == WM_NCHITTEST) {
        LRESULT hit = CallWindowProcW(s_Original, hwnd, message, wParam, lParam);
        if (hit != HTCLIENT) return hit;
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &point);
        if (!IsZoomed(hwnd) && point.y < 5) return HTTOP;
        if (point.y < s_Height && point.x >= s_StartX && point.x < s_EndX && !(point.x >= s_HoleStart && point.x < s_HoleEnd)) return HTCAPTION;
        return HTCLIENT;
    }
    return CallWindowProcW(s_Original, hwnd, message, wParam, lParam);
}

void SetDragArea(float height, float startX, float endX) { s_Height = height; s_StartX = startX; s_EndX = endX; }
void SetDragHole(float startX, float endX) { s_HoleStart = startX; s_HoleEnd = endX; }

void Install(void* nativeWindow) {
    HWND hwnd = (HWND)nativeWindow;
    if (s_Original) return;
    s_Original = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);
    const COLORREF border = RGB(14, 15, 17); // Windows 11 draws this around the window
    const DWORD DWMWA_BORDER_COLOR_ID = 34;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR_ID, &border, sizeof(border));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

} // namespace Stardust::TitleBar

#else
#include "TitleBar.h"
// Linux keeps the title bar of the window manager
namespace Stardust::TitleBar {
void Install(void*) {}
void SetDragArea(float, float, float) {}
void SetDragHole(float, float) {}
}
#endif
