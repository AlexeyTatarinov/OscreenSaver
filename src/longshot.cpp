#include "longshot.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <string>
#include <thread>

#include "i18n.h"
#include "scroll_stitch.h"
#include "toolbar.h"

namespace {

const wchar_t kStatusClass[] = L"OscreenSaver.LongShot";
constexpr UINT WM_LS_PROGRESS = WM_APP + 20;  // wParam = height so far
constexpr UINT WM_LS_FINISH = WM_APP + 21;    // lParam = LongShotResult*
constexpr UINT WM_LS_MODE = WM_APP + 22;      // wParam = Mode

constexpr int kMaxHeight = 30000;   // pixels; protects memory on endless pages
constexpr int kMaxSteps = 600;
constexpr DWORD kRedrawWait = 280;  // ms after a scroll before taking the next picture
constexpr DWORD kExtraWait = 450;   // one more wait when the page seems to stop or jump

// Auto: the program turns the wheel itself. Manual: the window did not react to that (some programs
// ignore it), so the user scrolls with the wheel and the program takes pictures while they do.
enum Mode : WPARAM { Auto = 0, Manual = 1, ManualTooFast = 2 };
Mode g_mode = Auto;

std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false};
HWND g_status = nullptr;
HWND g_notify = nullptr;
UINT g_doneMessage = 0;
int g_height = 0;
float g_scale = 1.f;

// Copies a part of the screen as it is now.
std::vector<uint32_t> Capture(const RECT& r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        BitBlt(memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
        GdiFlush();
        std::copy(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + pixels.size(), pixels.begin());
        SelectObject(memory, old);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return pixels;
}

// Turns the mouse wheel down by `notches` clicks where the cursor is.
void WheelDown(int notches) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA * notches);
    SendInput(1, &in, sizeof(in));
}

bool WaitOrStop(DWORD ms) {
    for (DWORD t = 0; t < ms; t += 20) {
        if (g_stop) return true;
        Sleep(20);
    }
    return g_stop;
}

void Work(RECT area, HWND status) {
    auto* result = new LongShotResult;
    const int w = area.right - area.left, h = area.bottom - area.top;
    POINT oldCursor = {};
    GetCursorPos(&oldCursor);
    const POINT center = {(area.left + area.right) / 2, (area.top + area.bottom) / 2};

    WaitOrStop(400);  // the frozen screen has just closed: let the windows under it redraw
    std::vector<uint32_t> frame = Capture(area);
    scroll::Stitcher stitcher(scroll::Frame{frame.data(), w, h});
    PostMessageW(status, WM_LS_PROGRESS, static_cast<WPARAM>(h), 0);

    auto add = [&](DWORD extraWait) {
        frame = Capture(area);
        auto r = stitcher.Add(scroll::Frame{frame.data(), w, h});
        if (r != scroll::Stitcher::Result::Added && extraWait && !WaitOrStop(extraWait)) {
            // Smooth scrolling may still be moving, or the page is loading: look once more.
            frame = Capture(area);
            r = stitcher.Add(scroll::Frame{frame.data(), w, h});
        }
        if (r == scroll::Stitcher::Result::Added) {
            result->scrolled = true;
            PostMessageW(status, WM_LS_PROGRESS, static_cast<WPARAM>(stitcher.Height()), 0);
        }
        return r;
    };

    // 1. Automatic: the wheel is turned at the middle of the area.
    int notches = 2;
    bool automatic = true;
    for (int step = 0; step < kMaxSteps && !g_stop; ++step) {
        SetCursorPos(center.x, center.y);  // the wheel scrolls the window under the cursor
        WheelDown(notches);
        if (WaitOrStop(kRedrawWait)) break;
        const auto r = add(kExtraWait);
        if (r != scroll::Stitcher::Result::Added) {
            // At the very first step: the window ignores the wheel from a program -> manual mode.
            // Later: the end of the page.
            automatic = step == 0 ? false : automatic;
            break;
        }
        // Next step: about half of the visible part, so pictures always overlap enough.
        const int perNotch = std::max(1, stitcher.LastShift() / notches);
        notches = std::clamp(stitcher.ContentHeight() / 2 / perNotch, 1, 10);
        if (stitcher.Height() >= kMaxHeight) break;
    }
    SetCursorPos(oldCursor.x, oldCursor.y);

    // 2. Manual: the user turns the wheel; a picture is taken several times a second.
    if (!automatic && !g_stop) {
        PostMessageW(status, WM_LS_MODE, Manual, 0);
        DWORD idle = 0;
        Mode shown = Manual;
        while (!g_stop && idle < 120000 && stitcher.Height() < kMaxHeight) {
            if (WaitOrStop(150)) break;
            const auto r = add(0);
            const Mode now = r == scroll::Stitcher::Result::NoMatch ? ManualTooFast : Manual;
            if (now != shown) {
                PostMessageW(status, WM_LS_MODE, now, 0);
                shown = now;
            }
            idle = r == scroll::Stitcher::Result::Added ? 0 : idle + 150;
        }
    }

    result->pixels = stitcher.Picture(result->width, result->height);
    PostMessageW(status, WM_LS_FINISH, 0, reinterpret_cast<LPARAM>(result));
}

// ---------- Status window ----------

std::wstring StatusText() {
    wchar_t text[200];
    const Str id = g_mode == Auto ? Str::LongStatus : g_mode == Manual ? Str::LongManual : Str::LongTooFast;
    swprintf(text, 200, T(id), g_height);
    return text;
}

LRESULT CALLBACK StatusProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LS_PROGRESS:
            g_height = static_cast<int>(wp);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        case WM_LS_MODE:
            g_mode = static_cast<Mode>(wp);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        case WM_LS_FINISH:
            g_running = false;
            PostMessageW(g_notify, g_doneMessage, 0, lp);
            DestroyWindow(w);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            RECT r;
            GetClientRect(w, &r);
            HBRUSH back = CreateSolidBrush(RGB(44, 44, 44));
            FillRect(dc, &r, back);
            DeleteObject(back);
            HBRUSH edge = CreateSolidBrush(toolbar::AccentColor());
            FrameRect(dc, &r, edge);
            DeleteObject(edge);
            NONCLIENTMETRICSW ncm = {sizeof(ncm)};
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
            HGDIOBJ old = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            const std::wstring text = StatusText();
            DrawTextW(dc, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, old);
            DeleteObject(font);
            EndPaint(w, &ps);
            return 0;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;  // clicks go through
        case WM_DESTROY:
            g_status = nullptr;
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

// Small window with the progress, placed where it does not cover the captured area.
HWND CreateStatus(const RECT& area) {
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = StatusProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kStatusClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    HMONITOR monitor = MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    const RECT& wa = mi.rcWork;
    g_scale = GetDpiForSystem() / 96.f;
    const int width = static_cast<int>(560 * g_scale), height = static_cast<int>(36 * g_scale);
    const int gap = static_cast<int>(8 * g_scale);
    const int x = wa.left + (wa.right - wa.left - width) / 2;
    int y = wa.top + gap;  // top of the screen...
    if (y + height + gap > area.top && y < area.bottom) y = wa.bottom - height - gap;  // ...or bottom
    if (y < area.bottom && y + height > area.top) y = std::max<int>(wa.top, area.top - height - gap);
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kStatusClass, L"OscreenSaver",
                             WS_POPUP, x, y, width, height, nullptr, nullptr, instance, nullptr);
    ShowWindow(w, SW_SHOWNOACTIVATE);
    UpdateWindow(w);
    return w;
}

}  // namespace

bool IsLongShotRunning() { return g_running; }

void StopLongShot() { g_stop = true; }

void StartLongShot(const RECT& area, HWND notify, UINT doneMessage) {
    if (g_running || area.right - area.left < 32 || area.bottom - area.top < 48) return;
    g_running = true;
    g_stop = false;
    g_mode = Auto;
    g_notify = notify;
    g_doneMessage = doneMessage;
    g_height = area.bottom - area.top;
    g_status = CreateStatus(area);
    std::thread(Work, area, g_status).detach();
}
