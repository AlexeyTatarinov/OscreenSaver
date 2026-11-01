#include "overlay.h"

#include "capture.h"

namespace {

const wchar_t kOverlayClass[] = L"OscreenSaver.Overlay";

HWND g_overlay = nullptr;
Snapshot g_snapshot;

void PaintSnapshot(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(memory, g_snapshot.bitmap);
    const RECT& p = ps.rcPaint;
    BitBlt(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, memory, p.left, p.top, SRCCOPY);
    SelectObject(memory, old);
    DeleteDC(memory);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintSnapshot(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;  // the frozen image covers everything; avoids flicker
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);  // close without saving
            }
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_DESTROY:
            FreeSnapshot(g_snapshot);
            g_overlay = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool RegisterOverlayClass(HINSTANCE instance) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.lpszClassName = kOverlayClass;
    return RegisterClassExW(&wc) != 0;
}

bool IsOverlayOpen() {
    return g_overlay != nullptr;
}

void OpenOverlay(HINSTANCE instance) {
    if (g_overlay) {
        return;
    }
    HMONITOR monitor = MonitorUnderCursor();
    if (!monitor) {
        // TODO(stage 2): show the monitor picker. For now fall back to the primary monitor.
        monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    if (!CaptureMonitor(monitor, g_snapshot)) {
        return;
    }
    const RECT& b = g_snapshot.bounds;
    g_overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClass, L"OscreenSaver", WS_POPUP,
                                b.left, b.top, g_snapshot.Width(), g_snapshot.Height(), nullptr, nullptr,
                                instance, nullptr);
    if (!g_overlay) {
        FreeSnapshot(g_snapshot);
        return;
    }
    ShowWindow(g_overlay, SW_SHOW);
    SetForegroundWindow(g_overlay);
    SetFocus(g_overlay);
}
