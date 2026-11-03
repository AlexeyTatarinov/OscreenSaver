#include "pin.h"

#include <windowsx.h>

#include <algorithm>
#include <string>

#include "app.h"
#include "i18n.h"
#include "save.h"
#include "toolbar.h"

namespace {

const wchar_t kClass[] = L"OscreenSaver.Pin";

enum MenuId : UINT { IdCopy = 1, IdSave, IdActualSize, IdClose };

struct Pin {
    Snapshot picture;
    HDC pictureDc = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    float zoom = 1.f;
    COLORREF accent = RGB(0, 120, 215);
};

Pin* Of(HWND w) { return reinterpret_cast<Pin*>(GetWindowLongPtrW(w, GWLP_USERDATA)); }

SIZE WindowSize(const Pin& p) {
    return SIZE{static_cast<LONG>(p.picture.Width() * p.zoom) + 2, static_cast<LONG>(p.picture.Height() * p.zoom) + 2};
}

void ApplyZoom(HWND w, Pin& p, float zoom) {
    p.zoom = std::clamp(zoom, 0.1f, 8.f);
    const SIZE size = WindowSize(p);
    SetWindowPos(w, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(w, nullptr, FALSE);
}

void Paint(HWND w, Pin& p) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    RECT client;
    GetClientRect(w, &client);
    // Thin accent frame, so the pinned picture is not mistaken for a part of the screen.
    HBRUSH frame = CreateSolidBrush(p.accent);
    FrameRect(dc, &client, frame);
    DeleteObject(frame);
    SetStretchBltMode(dc, p.zoom < 1.f ? HALFTONE : COLORONCOLOR);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, 1, 1, client.right - 2, client.bottom - 2, p.pictureDc, 0, 0, p.picture.Width(),
               p.picture.Height(), SRCCOPY);
    EndPaint(w, &ps);
}

void ShowMenu(HWND w, Pin& p, POINT at) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IdCopy, T(Str::PinCopy));
    AppendMenuW(menu, MF_STRING, IdSave, T(Str::PinSave));
    AppendMenuW(menu, MF_STRING | (p.zoom == 1.f ? MF_GRAYED : 0), IdActualSize, T(Str::PinActualSize));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdClose, T(Str::PinClose));
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, w, nullptr);
    DestroyMenu(menu);
    const RECT all = {0, 0, p.picture.Width(), p.picture.Height()};
    switch (cmd) {
        case IdCopy:
            CopyAreaToClipboard(p.picture, all, w);
            NotifyCopied();
            break;
        case IdSave: {
            std::wstring path;
            if (SaveArea(p.picture, all, path)) NotifySaved(path);
            else NotifySaveFailed();
            break;
        }
        case IdActualSize:
            ApplyZoom(w, p, 1.f);
            break;
        case IdClose:
            DestroyWindow(w);
            break;
    }
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    Pin* p = Of(w);
    switch (msg) {
        case WM_NCHITTEST:
            return HTCAPTION;  // the whole window can be dragged
        case WM_NCLBUTTONDBLCLK:
            DestroyWindow(w);
            return 0;
        case WM_NCRBUTTONUP:
            if (p) ShowMenu(w, *p, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_MOUSEWHEEL:
            if (p) ApplyZoom(w, *p, p->zoom * (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.15f : 1.f / 1.15f));
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(w);
            if (p && wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                const RECT all = {0, 0, p->picture.Width(), p->picture.Height()};
                CopyAreaToClipboard(p->picture, all, w);
                NotifyCopied();
            }
            return 0;
        case WM_PAINT:
            if (p) {
                Paint(w, *p);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            if (p) {
                SelectObject(p->pictureDc, p->oldBitmap);
                DeleteDC(p->pictureDc);
                FreeSnapshot(p->picture);
                delete p;
                SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            }
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

void PinPicture(Snapshot picture, POINT at) {
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    auto* p = new Pin;
    p->picture = picture;
    p->accent = toolbar::AccentColor();
    p->pictureDc = CreateCompatibleDC(nullptr);
    p->oldBitmap = SelectObject(p->pictureDc, p->picture.bitmap);
    const SIZE size = WindowSize(*p);
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"OscreenSaver", WS_POPUP, at.x - 1, at.y - 1,
                             size.cx, size.cy, nullptr, nullptr, instance, nullptr);
    if (!w) {
        SelectObject(p->pictureDc, p->oldBitmap);
        DeleteDC(p->pictureDc);
        FreeSnapshot(p->picture);
        delete p;
        return;
    }
    SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
}
