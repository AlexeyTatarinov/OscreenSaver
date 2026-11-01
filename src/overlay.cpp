#include "overlay.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "annotate.h"
#include "app.h"
#include "capture.h"
#include "i18n.h"
#include "save.h"
#include "toolbar.h"

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
    bool firstSelectionDone = false;

    // Area mode dragging.
    Drag drag = Drag::None;
    int dragHandle = Outside;
    POINT dragStart = {};
    RECT selectionAtStart = {};
    bool moved = false;

    // Drawing.
    Tool tool = Tool::Area;
    Tool lastDrawTool = Tool::Pencil;
    int color = 0;
    int size = 1;
    std::vector<Shape> shapes;
    std::vector<Shape> redo;
    bool drawing = false;
    Shape current;
    bool typing = false;  // text tool: a text is being typed
    Shape text;
    int hover = toolbar::kNone;
    bool dark = true;

    HDC backDc = nullptr;  // off-screen buffer: draws without flicker
    HBITMAP backBitmap = nullptr;
    HGDIOBJ backOld = nullptr;
    HGDIOBJ backFontOld = nullptr;
    HDC shotDc = nullptr;
    HGDIOBJ shotOld = nullptr;
    HDC dimDc = nullptr;
    HGDIOBJ dimOld = nullptr;
    HFONT font = nullptr;
    float scale = 1.f;
    int unit = 1;  // whole-pixel size of one "point" of decoration
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

RECT Union(const RECT& a, const RECT& b) {
    RECT r;
    UnionRect(&r, &a, &b);
    return r;
}

// ---------- Geometry of everything drawn around the selection ----------

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

// A dark box with white text. The same function gives the box for drawing and for repainting,
// so nothing is left behind when the box moves.
RECT TextBox(const Overlay& o, const wchar_t* text, const RECT& anchor, bool centered) {
    SIZE size = {0, 0};
    GetTextExtentPoint32W(o.backDc, text, static_cast<int>(wcslen(text)), &size);
    const int w = size.cx + Scaled(o, 8) * 2;
    const int h = size.cy + Scaled(o, 4) * 2;
    if (centered) {
        const int cx = (anchor.left + anchor.right) / 2;
        const int cy = (anchor.top + anchor.bottom) / 2;
        return RECT{cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h};
    }
    // Above the top-left corner; inside the selection if there is no room above.
    int top = anchor.top - h - Scaled(o, 4);
    if (top < 0) top = anchor.top + Scaled(o, 4);
    return RECT{anchor.left, top, anchor.left + w, top + h};
}

void DrawTextBox(HDC dc, const wchar_t* text, const RECT& box) {
    HBRUSH back = CreateSolidBrush(kLabelBack);
    FillRect(dc, &box, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kLabelText);
    RECT r = box;
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

std::wstring SizeText(const Overlay& o) {
    wchar_t buf[48];
    swprintf(buf, 48, L"%ld × %ld", o.selection.right - o.selection.left,
             o.selection.bottom - o.selection.top);
    return buf;
}

RECT HintAnchor(const Overlay& o) { return RECT{0, 0, o.shot.Width(), Scaled(o, 80)}; }

bool PanelVisible(const Overlay& o) { return o.hasSelection && !o.picking; }

RECT PanelRect(const Overlay& o) { return toolbar::Place(o.selection, ClientRect(o), o.scale); }

RECT TipBox(const Overlay& o) {
    if (o.hover == toolbar::kNone || !PanelVisible(o)) return RECT{0, 0, 0, 0};
    const RECT panel = PanelRect(o);
    const RECT button = toolbar::ButtonRect(panel, o.hover, o.scale);
    RECT box = TextBox(o, toolbar::Tip(o.hover), button, true);
    const int h = box.bottom - box.top;
    const int gap = Scaled(o, 4);
    int top = panel.top - gap - h;
    if (top < 0) top = panel.bottom + gap;
    OffsetRect(&box, 0, top - box.top);
    const int w = box.right - box.left;
    if (box.left < 0) OffsetRect(&box, -box.left, 0);
    if (box.right > o.shot.Width()) OffsetRect(&box, o.shot.Width() - w - box.left, 0);
    return box;
}

// Everything outside the picture itself: frame, handles, size label, panel, hint.
RECT ChromeRect(const Overlay& o) {
    if (o.picking) return RECT{0, 0, 0, 0};
    if (!o.hasSelection) return TextBox(o, T(Str::HintSelect), HintAnchor(o), true);
    RECT r = o.selection;
    InflateRect(&r, Scaled(o, 6), Scaled(o, 6));
    if (o.tool == Tool::Area) r = Union(r, TextBox(o, SizeText(o).c_str(), o.selection, false));
    r = Union(r, PanelRect(o));
    r = Union(r, TipBox(o));
    return r;
}

// Remembers what was on screen before a change and repaints old + new places afterwards.
struct ChromeChange {
    explicit ChromeChange(Overlay& overlay) : o(overlay), before(ChromeRect(overlay)) {}
    ~ChromeChange() {
        const RECT dirty = Union(before, ChromeRect(o));
        if (!IsRectEmpty(&dirty)) InvalidateRect(o.hwnd, &dirty, FALSE);
    }
    Overlay& o;
    RECT before;
};

void InvalidatePicture(Overlay& o) {
    if (o.hasSelection) InvalidateRect(o.hwnd, &o.selection, FALSE);
}

void InvalidateShape(Overlay& o, const Shape& s) {
    if (s.tool == Tool::Text) {
        InvalidatePicture(o);
        return;
    }
    RECT r = ShapeBounds(s, o.scale);
    InvalidateRect(o.hwnd, &r, FALSE);
}

// ---------- Selection ----------

void SetSelection(Overlay& o, const RECT& r) {
    ChromeChange change(o);
    o.selection = Clamp(o, r);
    o.hasSelection = true;
}

void ClearSelection(Overlay& o) {
    ChromeChange change(o);
    o.hasSelection = false;
}

// ---------- Drawing ----------

std::vector<const Shape*> VisibleShapes(const Overlay& o) {
    std::vector<const Shape*> list;
    list.reserve(o.shapes.size() + 2);
    for (const Shape& s : o.shapes) list.push_back(&s);
    if (o.drawing) list.push_back(&o.current);
    if (o.typing) list.push_back(&o.text);  // last, so the cursor is drawn for it
    return list;
}

void CommitText(Overlay& o) {
    if (!o.typing) return;
    ChromeChange change(o);
    o.typing = false;
    if (!o.text.text.empty()) {
        o.shapes.push_back(o.text);
        o.redo.clear();
    }
    InvalidatePicture(o);
}

void CancelText(Overlay& o) {
    o.typing = false;
    InvalidatePicture(o);
}

void StartText(Overlay& o, POINT p) {
    CommitText(o);
    o.typing = true;
    o.text = Shape{Tool::Text, kPalette[o.color], o.size, {p}, L""};
    InvalidatePicture(o);
}

void SelectTool(Overlay& o, Tool t) {
    CommitText(o);
    if (t != Tool::Area && !o.hasSelection) return;
    ChromeChange change(o);
    o.tool = t;
    if (t != Tool::Area) o.lastDrawTool = t;
}

void ToggleAreaMode(Overlay& o) {
    SelectTool(o, o.tool == Tool::Area ? o.lastDrawTool : Tool::Area);
}

void SetColor(Overlay& o, int c) {
    ChromeChange change(o);
    o.color = c;
    if (o.typing) {
        o.text.color = kPalette[c];
        InvalidatePicture(o);
    }
}

void SetSize(Overlay& o, int s) {
    ChromeChange change(o);
    o.size = s;
    if (o.typing) {
        o.text.size = s;
        InvalidatePicture(o);
    }
}

void Undo(Overlay& o) {
    CommitText(o);
    if (o.shapes.empty()) return;
    ChromeChange change(o);
    o.redo.push_back(o.shapes.back());
    o.shapes.pop_back();
    InvalidatePicture(o);
}

void Redo(Overlay& o) {
    CommitText(o);
    if (o.redo.empty()) return;
    ChromeChange change(o);
    o.shapes.push_back(o.redo.back());
    o.redo.pop_back();
    InvalidatePicture(o);
}

void StartStroke(Overlay& o, POINT p) {
    o.drawing = true;
    o.current = Shape{o.tool, kPalette[o.color], o.size, {p}, L""};
    if (o.tool == Tool::Arrow || o.tool == Tool::Rect || o.tool == Tool::Ellipse) {
        o.current.points.push_back(p);
    }
    SetCapture(o.hwnd);
    InvalidateShape(o, o.current);
}

void ContinueStroke(Overlay& o, POINT p) {
    std::vector<POINT>& pts = o.current.points;
    if (o.current.points.size() == 2 &&
        (o.tool == Tool::Arrow || o.tool == Tool::Rect || o.tool == Tool::Ellipse)) {
        const RECT before = ShapeBounds(o.current, o.scale);
        pts[1] = p;
        const RECT dirty = Union(before, ShapeBounds(o.current, o.scale));
        InvalidateRect(o.hwnd, &dirty, FALSE);
        return;
    }
    if (pts.back().x == p.x && pts.back().y == p.y) return;
    // Repaint only the newest piece of the line.
    Shape piece{o.current.tool, 0, o.current.size, {pts.back(), p}, L""};
    pts.push_back(p);
    InvalidateShape(o, piece);
}

void EndStroke(Overlay& o) {
    if (!o.drawing) return;
    ChromeChange change(o);  // the undo button becomes active
    o.drawing = false;
    o.shapes.push_back(o.current);
    o.redo.clear();
    if (GetCapture() == o.hwnd) ReleaseCapture();
}

// ---------- Painting ----------

void DrawFrame(const Overlay& o, HDC dc) {
    const RECT& s = o.selection;
    HPEN pen = CreatePen(PS_SOLID, Scaled(o, 1), kAccent);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, s.left, s.top, s.right, s.bottom);
    if (o.tool == Tool::Area) {
        // Handles: small white squares with a blue border.
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        const int half = Scaled(o, 4);
        for (int h = TopLeft; h <= Left; ++h) {
            const POINT c = HandleCenter(s, h);
            Rectangle(dc, c.x - half, c.y - half, c.x + half + 1, c.y + half + 1);
        }
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
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

    if (o.picking) {
        const wchar_t* hint = T(Str::HintPickMonitor);
        DrawTextBox(o.backDc, hint, TextBox(o, hint, ClientRect(o), true));
    } else if (o.hasSelection) {
        const RECT& s = o.selection;
        BitBlt(o.backDc, s.left, s.top, s.right - s.left, s.bottom - s.top, o.shotDc, s.left, s.top, SRCCOPY);
        RECT clip;
        if (IntersectRect(&clip, &s, &p)) {
            DrawShapes(o.backDc, clip, POINT{0, 0}, VisibleShapes(o), o.typing, o.scale);
        }
        DrawFrame(o, o.backDc);
        if (o.tool == Tool::Area) {
            const std::wstring size = SizeText(o);
            DrawTextBox(o.backDc, size.c_str(), TextBox(o, size.c_str(), s, false));
        }
        toolbar::State st;
        st.tool = o.tool;
        st.color = o.color;
        st.size = o.size;
        st.canUndo = !o.shapes.empty() || (o.typing && !o.text.text.empty());
        st.canRedo = !o.redo.empty();
        st.hover = o.hover;
        st.dark = o.dark;
        toolbar::Draw(o.backDc, PanelRect(o), st, o.scale);
        if (o.hover != toolbar::kNone) {
            DrawTextBox(o.backDc, toolbar::Tip(o.hover), TipBox(o));
        }
    } else {
        const wchar_t* hint = T(Str::HintSelect);
        DrawTextBox(o.backDc, hint, TextBox(o, hint, HintAnchor(o), true));
    }
    RestoreDC(o.backDc, saved);

    BitBlt(dc, p.left, p.top, w, h, o.backDc, p.left, p.top, SRCCOPY);
    EndPaint(o.hwnd, &ps);
}

// ---------- Closing and saving ----------

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
    CommitText(o);
    const RECT s = o.selection;
    const int w = s.right - s.left;
    const int h = s.bottom - s.top;
    if (!o.hasSelection || w <= 0 || h <= 0) {
        return;
    }
    // Final picture = the selected part of the screen + everything drawn on it.
    Snapshot result;
    if (!CreateBlankSnapshot(w, h, result)) {
        NotifySaveFailed();
        return;
    }
    for (int y = 0; y < h; ++y) {
        std::memcpy(result.pixels + static_cast<size_t>(y) * w,
                    o.shot.pixels + static_cast<size_t>(s.top + y) * o.shot.Width() + s.left,
                    static_cast<size_t>(w) * 4);
    }
    HDC memory = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(memory, result.bitmap);
    DrawShapes(memory, RECT{0, 0, w, h}, POINT{s.left, s.top}, VisibleShapes(o), false, o.scale);
    GdiFlush();
    SelectObject(memory, old);
    DeleteDC(memory);

    const RECT all = {0, 0, w, h};
    std::wstring path;
    const bool saved = SaveAreaAsJpg(result, all, path);
    CopyAreaToClipboard(result, all, o.hwnd);
    FreeSnapshot(result);
    CloseAll();
    if (saved) {
        NotifySaved(path);
    } else {
        NotifySaveFailed();
    }
}

void OnPanelClick(Overlay& o, int id) {
    using namespace toolbar;
    if (id < kFirstColor) SelectTool(o, static_cast<Tool>(id));
    else if (id < kFirstSize) SetColor(o, id - kFirstColor);
    else if (id < kUndo) SetSize(o, id - kFirstSize);
    else if (id == kUndo) Undo(o);
    else if (id == kRedo) Redo(o);
    else if (id == kSave) Finish(o);
    else if (id == kClose) CloseAll();
}

// ---------- Mouse in area mode ----------

void AreaMouseDown(Overlay& o, POINT p) {
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

void AreaMouseMove(Overlay& o, POINT p) {
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

void AreaMouseUp(Overlay& o) {
    const Drag drag = o.drag;
    const bool click = drag == Drag::Create && !o.moved;
    o.drag = Drag::None;
    if (GetCapture() == o.hwnd) ReleaseCapture();
    if (click) {
        SetSelection(o, ClientRect(o));  // a single click selects the whole screen
    }
    if (drag == Drag::Create && o.hasSelection && !o.firstSelectionDone) {
        // Order of work: first choose the area, then draw. The pencil is ready right away.
        o.firstSelectionDone = true;
        SelectTool(o, o.lastDrawTool);
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

LPCWSTR CursorAt(const Overlay& o, POINT c) {
    if (o.picking) return IDC_HAND;
    if (PanelVisible(o) && toolbar::HitTest(PanelRect(o), c, o.scale) != toolbar::kNone) return IDC_ARROW;
    if (o.tool == Tool::Text) return IDC_IBEAM;
    if (o.tool != Tool::Area) return IDC_CROSS;
    const int hit = o.drag == Drag::Resize ? o.dragHandle
                    : o.drag == Drag::Move ? Inside
                    : o.drag == Drag::Create ? Outside
                    : HitTest(o, c);
    return CursorFor(hit);
}

// ---------- Window ----------

void FreeOverlay(Overlay* o) {
    if (o->backDc) {
        SelectObject(o->backDc, o->backFontOld);
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

void OnKeyDown(Overlay& o, WPARAM key) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (o.typing) {
        if (key == VK_RETURN && shift) {
            o.text.text.push_back(L'\n');
            InvalidatePicture(o);
        } else if (key == VK_RETURN) {
            CommitText(o);
        } else if (key == VK_ESCAPE) {
            CancelText(o);
        }
        return;  // letters arrive as WM_CHAR
    }
    if (key == VK_ESCAPE || key == VK_RETURN) {
        OverlayKey(static_cast<unsigned>(key));  // normally caught by the hook first
    } else if (o.picking) {
        return;
    } else if (key == VK_TAB) {
        ToggleAreaMode(o);
    } else if (ctrl && key == 'Z') {
        shift ? Redo(o) : Undo(o);
    } else if (ctrl && key == 'Y') {
        Redo(o);
    } else if (ctrl && key == 'S') {
        Finish(o);
    } else if (ctrl && key == 'A') {
        SetSelection(o, ClientRect(o));
    }
}

void OnChar(Overlay& o, wchar_t ch) {
    if (!o.typing) return;
    if (ch == L'\b') {
        if (!o.text.text.empty()) o.text.text.pop_back();
    } else if (ch >= L' ' && ch != 127) {
        o.text.text.push_back(ch);
    } else {
        return;
    }
    ChromeChange change(o);  // undo button state
    InvalidatePicture(o);
}

void OnMouseDown(Overlay& o, POINT p) {
    if (o.picking) return;
    if (PanelVisible(o)) {
        const int id = toolbar::HitTest(PanelRect(o), p, o.scale);
        if (id != toolbar::kNone) {
            OnPanelClick(o, id);
            return;
        }
    }
    SetFocus(o.hwnd);
    if (o.tool == Tool::Area) {
        CommitText(o);
        AreaMouseDown(o, p);
    } else if (o.tool == Tool::Text) {
        StartText(o, p);
    } else {
        CommitText(o);
        StartStroke(o, p);
    }
}

void OnMouseMove(Overlay& o, POINT p) {
    if (PanelVisible(o)) {
        const int hover = o.drawing || o.drag != Drag::None ? toolbar::kNone
                                                            : toolbar::HitTest(PanelRect(o), p, o.scale);
        if (hover != o.hover) {
            ChromeChange change(o);
            o.hover = hover;
        }
    }
    if (o.drawing) {
        ContinueStroke(o, p);
    } else if (o.drag != Drag::None) {
        AreaMouseMove(o, p);
    }
}

void OnMouseUp(Overlay& o) {
    if (o.drawing) {
        EndStroke(o);
    } else if (o.drag != Drag::None) {
        AreaMouseUp(o);
    }
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
            OnMouseDown(*o, p);
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(*o, p);
            return 0;
        case WM_LBUTTONUP:
            OnMouseUp(*o);
            return 0;
        case WM_LBUTTONDBLCLK:
            if (!o->picking && o->tool == Tool::Area && HitTest(*o, p) == Inside) {
                Finish(*o);
            } else {
                OnMouseDown(*o, p);  // in drawing tools a fast second click is just another click
            }
            return 0;
        case WM_RBUTTONUP:
            if (o->picking) {
                ChooseMonitor(*o);
            } else if (o->tool == Tool::Area && o->hasSelection && o->drag == Drag::None) {
                ClearSelection(*o);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (o->drawing) EndStroke(*o);
            o->drag = Drag::None;
            return 0;
        case WM_KEYDOWN:
            OnKeyDown(*o, wp);
            return 0;
        case WM_CHAR:
            OnChar(*o, static_cast<wchar_t>(wp));
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT c;
                GetCursorPos(&c);
                ScreenToClient(hwnd, &c);
                SetCursor(LoadCursorW(nullptr, CursorAt(*o, c)));
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
    o->dark = toolbar::SystemUsesDarkTheme();
    if (!CaptureMonitor(monitor, o->shot) || !MakeDimmedCopy(o->shot, o->dim)) {
        FreeOverlay(o);
        return false;
    }
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    o->scale = static_cast<float>(dpiX) / 96.f;
    o->unit = std::max<int>(1, static_cast<int>((dpiX + 48) / 96));

    o->font = CreateFontW(-MulDiv(10, static_cast<int>(dpiX), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
    HDC screen = GetDC(nullptr);
    o->backDc = CreateCompatibleDC(screen);
    o->backBitmap = CreateCompatibleBitmap(screen, o->shot.Width(), o->shot.Height());
    o->backOld = SelectObject(o->backDc, o->backBitmap);
    o->backFontOld = SelectObject(o->backDc, o->font);
    o->shotDc = CreateCompatibleDC(screen);
    o->shotOld = SelectObject(o->shotDc, o->shot.bitmap);
    o->dimDc = CreateCompatibleDC(screen);
    o->dimOld = SelectObject(o->dimDc, o->dim.bitmap);
    ReleaseDC(nullptr, screen);

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

bool IsOverlayTyping() {
    for (const Overlay* o : g_overlays) {
        if (o->typing) return true;
    }
    return false;
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
