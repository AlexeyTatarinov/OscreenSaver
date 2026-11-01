#include "overlay.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#include "app.h"
#include "capture.h"
#include "i18n.h"
#include "save.h"

namespace {

const wchar_t kOverlayClass[] = L"OscreenSaver.Overlay";

const COLORREF kAccent = RGB(0, 120, 215);
const COLORREF kLabelBack = RGB(32, 32, 32);
const COLORREF kLabelText = RGB(255, 255, 255);

// Handles around the selection: 0..7 clockwise from top-left. 8 = inside. -1 = outside.
enum Hit { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Inside, Outside = -1 };
enum class Drag { None, Create, Move, Resize };

struct Overlay {
    HWND hwnd = nullptr;
    Snapshot shot;
    Snapshot dim;
    bool picking = false;  // monitor picker mode: right-click chooses this monitor

    bool hasSelection = false;
    RECT selection = {};  // normalized, in window pixels (= snapshot pixels)

    Drag drag = Drag::None;
    int dragHandle = Outside;
    POINT dragStart = {};
    RECT selectionAtStart = {};
    bool moved = false;

    HDC backDc = nullptr;  // off-screen buffer: draws without flicker
    HBITMAP backBitmap = nullptr;
    HGDIOBJ backOld = nullptr;
    HDC shotDc = nullptr;
    HGDIOBJ shotOld = nullptr;
    HDC dimDc = nullptr;
    HGDIOBJ dimOld = nullptr;
    HFONT font = nullptr;
    int unit = 1;  // DPI-scaled size of one "point" of decoration
};

std::vector<Overlay*> g_overlays;

}  // namespace
void OverlayKey(unsigned vk);
namespace {

int Scaled(const Overlay& o, int value) { return value * o.unit; }

RECT Normalize(LONG x1, LONG y1, LONG x2, LONG y2) {
    return RECT{std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2)};
}

RECT ClientRect(const Overlay& o) { return RECT{0, 0, o.shot.Width(), o.shot.Height()}; }

RECT Clamp(const Overlay& o, RECT r) {
    r.left = std::clamp<LONG>(r.left, 0, o.shot.Width());
    r.right = std::clamp<LONG>(r.right, 0, o.shot.Width());
    r.top = std::clamp<LONG>(r.top, 0, o.shot.Height());
    r.bottom = std::clamp<LONG>(r.bottom, 0, o.shot.Height());
    return r;
}

POINT HandleCenter(const RECT& r, int handle) {
    const LONG cx = (r.left + r.right) / 2;
    const LONG cy = (r.top + r.bottom) / 2;
    switch (handle) {
        case TopLeft: return {r.left, r.top};
        case Top: return {cx, r.top};
        case TopRight: return {r.right, r.top};
        case Right: return {r.right, cy};
        case BottomRight: return {r.right, r.bottom};
        case Bottom: return {cx, r.bottom};
        case BottomLeft: return {r.left, r.bottom};
        default: return {r.left, cy};
    }
}

int HitTest(const Overlay& o, POINT p) {
    if (!o.hasSelection) {
        return Outside;
    }
    const int grab = Scaled(o, 6);
    for (int h = TopLeft; h <= Left; ++h) {
        const POINT c = HandleCenter(o.selection, h);
        if (std::abs(p.x - c.x) <= grab && std::abs(p.y - c.y) <= grab) {
            return h;
        }
    }
    return PtInRect(&o.selection, p) ? Inside : Outside;
}

// Everything that drawing the selection can touch: frame, handles and the size label.
RECT DecorationBounds(const Overlay& o, const RECT& s) {
    const int margin = Scaled(o, 8);
    RECT r = {s.left - margin, s.top - Scaled(o, 30), std::max(s.right, s.left + Scaled(o, 140)) + margin,
              s.bottom + margin};
    return r;
}

void InvalidateSelection(Overlay& o, const RECT& before, bool hadBefore) {
    RECT dirty = DecorationBounds(o, o.selection);
    if (hadBefore) {
        const RECT old = DecorationBounds(o, before);
        UnionRect(&dirty, &dirty, &old);
    }
    InvalidateRect(o.hwnd, &dirty, FALSE);
}

void SetSelection(Overlay& o, const RECT& r) {
    const RECT before = o.selection;
    const bool hadBefore = o.hasSelection;
    o.selection = Clamp(o, r);
    o.hasSelection = true;
    if (!hadBefore) {
        InvalidateRect(o.hwnd, nullptr, FALSE);  // the hint disappears
    } else {
        InvalidateSelection(o, before, true);
    }
}

void ClearSelection(Overlay& o) {
    o.hasSelection = false;
    InvalidateRect(o.hwnd, nullptr, FALSE);
}

void DrawLabel(const Overlay& o, HDC dc, const wchar_t* text, RECT anchor, bool centered) {
    SelectObject(dc, o.font);
    RECT size = {0, 0, 0, 0};
    DrawTextW(dc, text, -1, &size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    const int padX = Scaled(o, 8);
    const int padY = Scaled(o, 4);
    const int w = size.right + padX * 2;
    const int h = size.bottom + padY * 2;
    RECT box;
    if (centered) {
        const int cx = (anchor.left + anchor.right) / 2;
        const int cy = (anchor.top + anchor.bottom) / 2;
        box = {cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h};
    } else {
        // Above the top-left corner; inside the selection if there is no room above.
        int top = anchor.top - h - Scaled(o, 4);
        if (top < 0) top = anchor.top + Scaled(o, 4);
        box = {anchor.left, top, anchor.left + w, top + h};
    }
    HBRUSH back = CreateSolidBrush(kLabelBack);
    FillRect(dc, &box, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kLabelText);
    DrawTextW(dc, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawSelection(const Overlay& o, HDC dc) {
    const RECT& s = o.selection;
    // Clear (unmasked) picture inside the selection.
    BitBlt(dc, s.left, s.top, s.right - s.left, s.bottom - s.top, o.shotDc, s.left, s.top, SRCCOPY);

    // Frame.
    HPEN pen = CreatePen(PS_SOLID, Scaled(o, 1), kAccent);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, s.left, s.top, s.right, s.bottom);

    // Handles: small white squares with a blue border.
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    const int half = Scaled(o, 4);
    for (int h = TopLeft; h <= Left; ++h) {
        const POINT c = HandleCenter(s, h);
        Rectangle(dc, c.x - half, c.y - half, c.x + half + 1, c.y + half + 1);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    wchar_t size[48];
    swprintf(size, 48, L"%ld × %ld", s.right - s.left, s.bottom - s.top);
    DrawLabel(o, dc, size, s, false);
}

void Paint(Overlay& o) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(o.hwnd, &ps);
    const RECT& p = ps.rcPaint;
    const int w = p.right - p.left;
    const int h = p.bottom - p.top;

    // Only the damaged part is redrawn, so dragging stays light even on 4K screens.
    const int saved = SaveDC(o.backDc);
    IntersectClipRect(o.backDc, p.left, p.top, p.right, p.bottom);
    BitBlt(o.backDc, p.left, p.top, w, h, o.dimDc, p.left, p.top, SRCCOPY);  // grey mask everywhere

    const RECT all = ClientRect(o);
    if (o.picking) {
        DrawLabel(o, o.backDc, T(Str::HintPickMonitor), all, true);
    } else if (o.hasSelection) {
        DrawSelection(o, o.backDc);
    } else {
        RECT top = {0, 0, all.right, Scaled(o, 80)};
        DrawLabel(o, o.backDc, T(Str::HintSelect), top, true);
    }
    RestoreDC(o.backDc, saved);

    BitBlt(dc, p.left, p.top, w, h, o.backDc, p.left, p.top, SRCCOPY);
    EndPaint(o.hwnd, &ps);
}

void CloseAll() {
    const std::vector<Overlay*> copy = g_overlays;
    for (Overlay* o : copy) {
        DestroyWindow(o->hwnd);
    }
}

// Picker: the user right-clicked this monitor. Keep it, close the others.
void ChooseMonitor(Overlay& chosen) {
    const std::vector<Overlay*> copy = g_overlays;
    for (Overlay* o : copy) {
        if (o != &chosen) DestroyWindow(o->hwnd);
    }
    chosen.picking = false;
    InvalidateRect(chosen.hwnd, nullptr, FALSE);
    SetFocus(chosen.hwnd);
}

void Finish(Overlay& o) {
    if (!o.hasSelection || o.selection.right <= o.selection.left || o.selection.bottom <= o.selection.top) {
        return;
    }
    std::wstring path;
    const bool saved = SaveAreaAsJpg(o.shot, o.selection, path);
    CopyAreaToClipboard(o.shot, o.selection, o.hwnd);
    CloseAll();
    if (saved) {
        NotifySaved(path);
    } else {
        NotifySaveFailed();
    }
}

void OnMouseDown(Overlay& o, POINT p) {
    SetCapture(o.hwnd);
    o.dragStart = p;
    o.selectionAtStart = o.selection;
    o.moved = false;
    const int hit = HitTest(o, p);
    if (hit >= TopLeft && hit <= Left) {
        o.drag = Drag::Resize;
        o.dragHandle = hit;
    } else if (hit == Inside) {
        o.drag = Drag::Move;
    } else {
        o.drag = Drag::Create;
    }
}

void OnMouseMove(Overlay& o, POINT p) {
    if (o.drag == Drag::None) {
        return;
    }
    const LONG dx = p.x - o.dragStart.x;
    const LONG dy = p.y - o.dragStart.y;
    if (!o.moved && std::abs(dx) < 3 && std::abs(dy) < 3) {
        return;  // ignore hand tremor on a click
    }
    o.moved = true;
    const RECT s0 = o.selectionAtStart;
    switch (o.drag) {
        case Drag::Create:
            SetSelection(o, Normalize(o.dragStart.x, o.dragStart.y, p.x, p.y));
            break;
        case Drag::Move: {
            const LONG w = s0.right - s0.left;
            const LONG h = s0.bottom - s0.top;
            const LONG x = std::clamp<LONG>(s0.left + dx, 0, o.shot.Width() - w);
            const LONG y = std::clamp<LONG>(s0.top + dy, 0, o.shot.Height() - h);
            SetSelection(o, RECT{x, y, x + w, y + h});
            break;
        }
        case Drag::Resize: {
            LONG l = s0.left, t = s0.top, r = s0.right, b = s0.bottom;
            const int hdl = o.dragHandle;
            if (hdl == TopLeft || hdl == Left || hdl == BottomLeft) l += dx;
            if (hdl == TopRight || hdl == Right || hdl == BottomRight) r += dx;
            if (hdl == TopLeft || hdl == Top || hdl == TopRight) t += dy;
            if (hdl == BottomLeft || hdl == Bottom || hdl == BottomRight) b += dy;
            SetSelection(o, Normalize(l, t, r, b));
            break;
        }
        case Drag::None:
            break;
    }
}

void OnMouseUp(Overlay& o) {
    if (o.drag == Drag::None) {
        return;
    }
    const bool click = o.drag == Drag::Create && !o.moved;
    o.drag = Drag::None;
    ReleaseCapture();
    if (click) {
        SetSelection(o, ClientRect(o));  // a single click selects the whole screen
    }
}

LPCWSTR CursorFor(int hit) {
    switch (hit) {
        case TopLeft: case BottomRight: return IDC_SIZENWSE;
        case TopRight: case BottomLeft: return IDC_SIZENESW;
        case Top: case Bottom: return IDC_SIZENS;
        case Left: case Right: return IDC_SIZEWE;
        case Inside: return IDC_SIZEALL;
        default: return IDC_CROSS;
    }
}

void FreeOverlay(Overlay* o) {
    if (o->backDc) {
        SelectObject(o->backDc, o->backOld);
        DeleteDC(o->backDc);
    }
    if (o->backBitmap) DeleteObject(o->backBitmap);
    if (o->shotDc) {
        SelectObject(o->shotDc, o->shotOld);
        DeleteDC(o->shotDc);
    }
    if (o->dimDc) {
        SelectObject(o->dimDc, o->dimOld);
        DeleteDC(o->dimDc);
    }
    if (o->font) DeleteObject(o->font);
    FreeSnapshot(o->shot);
    FreeSnapshot(o->dim);
    delete o;
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* o = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        o = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        o->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(o));
    }
    if (!o) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    const POINT p = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};

    switch (msg) {
        case WM_PAINT:
            Paint(*o);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            if (!o->picking) OnMouseDown(*o, p);
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(*o, p);
            return 0;
        case WM_LBUTTONUP:
            OnMouseUp(*o);
            return 0;
        case WM_LBUTTONDBLCLK:
            if (!o->picking && HitTest(*o, p) == Inside) Finish(*o);
            return 0;
        case WM_RBUTTONUP:
            if (o->picking) {
                ChooseMonitor(*o);
            } else if (o->hasSelection && o->drag == Drag::None) {
                ClearSelection(*o);
            }
            return 0;
        case WM_CAPTURECHANGED:
            o->drag = Drag::None;
            return 0;
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wp == VK_ESCAPE || wp == VK_RETURN) {
                OverlayKey(static_cast<unsigned>(wp));  // normally caught by the hook first
            } else if (!o->picking && ctrl && wp == 'S') {
                Finish(*o);
            } else if (!o->picking && ctrl && wp == 'A') {
                SetSelection(*o, ClientRect(*o));
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT c;
                GetCursorPos(&c);
                ScreenToClient(hwnd, &c);
                const int hit = o->drag == Drag::Resize ? o->dragHandle
                                : o->drag == Drag::Move ? Inside
                                : o->drag == Drag::Create ? Outside
                                : HitTest(*o, c);
                SetCursor(LoadCursorW(nullptr, o->picking ? IDC_HAND : CursorFor(hit)));
                return TRUE;
            }
            break;
        case WM_NCDESTROY: {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            g_overlays.erase(std::remove(g_overlays.begin(), g_overlays.end(), o), g_overlays.end());
            FreeOverlay(o);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Prepares drawing resources and creates the full-screen window for one captured monitor.
bool CreateOverlayWindow(HINSTANCE instance, HMONITOR monitor, bool picking) {
    auto* o = new Overlay;
    o->picking = picking;
    if (!CaptureMonitor(monitor, o->shot) || !MakeDimmedCopy(o->shot, o->dim)) {
        FreeOverlay(o);
        return false;
    }
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    o->unit = std::max<int>(1, static_cast<int>((dpiX + 48) / 96));

    HDC screen = GetDC(nullptr);
    o->backDc = CreateCompatibleDC(screen);
    o->backBitmap = CreateCompatibleBitmap(screen, o->shot.Width(), o->shot.Height());
    o->backOld = SelectObject(o->backDc, o->backBitmap);
    o->shotDc = CreateCompatibleDC(screen);
    o->shotOld = SelectObject(o->shotDc, o->shot.bitmap);
    o->dimDc = CreateCompatibleDC(screen);
    o->dimOld = SelectObject(o->dimDc, o->dim.bitmap);
    ReleaseDC(nullptr, screen);

    o->font = CreateFontW(-MulDiv(10, static_cast<int>(dpiX), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");

    g_overlays.push_back(o);
    const RECT& b = o->shot.bounds;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClass, L"OscreenSaver", WS_POPUP,
                                b.left, b.top, o->shot.Width(), o->shot.Height(), nullptr, nullptr, instance, o);
    if (!hwnd) {
        g_overlays.erase(std::remove(g_overlays.begin(), g_overlays.end(), o), g_overlays.end());
        FreeOverlay(o);
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    return true;
}

// Windows only lets the active program take keyboard focus. The capture is started from a background
// hook, so briefly join the input of the program that is active now, then take focus.
void TakeFocus(HWND hwnd) {
    HWND current = GetForegroundWindow();
    const DWORD currentThread = current ? GetWindowThreadProcessId(current, nullptr) : 0;
    const DWORD ourThread = GetCurrentThreadId();
    const bool attached = currentThread && currentThread != ourThread &&
                          AttachThreadInput(currentThread, ourThread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (attached) {
        AttachThreadInput(currentThread, ourThread, FALSE);
    }
}

}  // namespace

bool RegisterOverlayClass(HINSTANCE instance) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.lpszClassName = kOverlayClass;
    return RegisterClassExW(&wc) != 0;
}

void OverlayKey(unsigned vk) {
    if (g_overlays.empty()) {
        return;
    }
    if (vk == VK_ESCAPE) {
        CloseAll();  // close without saving
        return;
    }
    if (vk == VK_RETURN) {
        HWND focus = GetForegroundWindow();
        for (Overlay* o : g_overlays) {
            if (!o->picking && (o->hwnd == focus || g_overlays.size() == 1)) {
                Finish(*o);
                return;
            }
        }
    }
}

bool IsOverlayOpen() {
    return !g_overlays.empty();
}

void OpenOverlay(HINSTANCE instance) {
    if (IsOverlayOpen()) {
        return;
    }
    HMONITOR monitor = MonitorUnderCursor();
    if (monitor) {
        if (CreateOverlayWindow(instance, monitor, false)) {
            TakeFocus(g_overlays.back()->hwnd);
        }
        return;
    }
    // Cursor is not on any monitor: freeze every monitor and let the user right-click one.
    for (HMONITOR m : AllMonitors()) {
        CreateOverlayWindow(instance, m, true);
    }
    if (!g_overlays.empty()) {
        TakeFocus(g_overlays.front()->hwnd);
    }
}
