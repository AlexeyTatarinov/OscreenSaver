#include "overlay.h"

#include <commdlg.h>
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

// Choices that are kept from one screenshot to the next while the program runs.
struct Settings {
    int color = kDefaultColor;  // index in kPalette, or kColorCount = colour from the colour window
    COLORREF customColor = RGB(0, 120, 215);
    bool hasCustom = false;
    COLORREF customColors[16] = {};  // the colour window's own "custom colours" row
    int size = 1;
    TextStyle text;              // px stored at 100% screen scale
    int textSize = 1;            // highlighted text size button, -1 = size from the font window
};
Settings g_settings;
bool g_dialogOpen = false;  // colour or font window is open: Esc/Enter belong to it

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

    // Current tool and options.
    Tool tool = Tool::Area;
    Tool lastDrawTool = Tool::Pencil;
    int colorIndex = kDefaultColor;
    COLORREF color = kPalette[kDefaultColor];
    int size = 1;
    TextStyle textStyle;
    int textSize = 1;

    // Drawn objects and history. Every change saves the previous list, so undo and redo
    // work the same way for drawing, erasing and text edits.
    std::vector<Shape> shapes;
    std::vector<std::vector<Shape>> undo;
    std::vector<std::vector<Shape>> redo;

    bool drawing = false;
    Shape current;

    bool erasing = false;
    bool erasedSomething = false;
    std::vector<Shape> beforeErase;

    bool typing = false;          // a text is being typed
    bool editingExisting = false; // ...and it is an old text opened again
    Shape text;

    POINT mouse = {-10000, -10000};
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

toolbar::State PanelState(const Overlay& o) {
    toolbar::State st;
    st.tool = o.tool;
    st.color = o.colorIndex;
    st.customColor = g_settings.customColor;
    st.hasCustom = g_settings.hasCustom;
    st.size = o.tool == Tool::Text ? o.textSize : o.size;
    const TextStyle& ts = o.typing ? o.text.style : o.textStyle;
    st.bold = ts.bold;
    st.italic = ts.italic;
    st.strike = ts.strike;
    st.canUndo = !o.undo.empty() || (o.typing && !o.text.text.empty());
    st.canRedo = !o.redo.empty();
    st.hover = o.hover;
    st.dark = o.dark;
    return st;
}

bool PanelVisible(const Overlay& o) { return o.hasSelection && !o.picking; }

RECT PanelRect(const Overlay& o) {
    return toolbar::Place(o.selection, ClientRect(o), o.scale, PanelState(o));
}

int PanelHit(const Overlay& o, POINT p) {
    if (!PanelVisible(o)) return toolbar::kNone;
    return toolbar::HitTest(PanelRect(o), p, o.scale, PanelState(o));
}

RECT TipBox(const Overlay& o) {
    if (o.hover == toolbar::kNone || !PanelVisible(o)) return RECT{0, 0, 0, 0};
    const RECT panel = PanelRect(o);
    const RECT button = toolbar::ButtonRect(panel, o.hover, o.scale, PanelState(o));
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

// Circle that shows where the eraser works.
bool EraserCircleVisible(const Overlay& o) {
    return o.tool == Tool::Eraser && o.hasSelection && !o.picking && PtInRect(&o.selection, o.mouse) &&
           PanelHit(o, o.mouse) == toolbar::kNone;
}

RECT EraserCircleRect(const Overlay& o) {
    if (!EraserCircleVisible(o)) return RECT{0, 0, 0, 0};
    const int r = static_cast<int>(EraserRadius(o.size, o.scale)) + 2;
    return RECT{o.mouse.x - r, o.mouse.y - r, o.mouse.x + r + 1, o.mouse.y + r + 1};
}

// Everything outside the picture itself: frame, handles, size label, panel, hint, eraser circle.
RECT ChromeRect(const Overlay& o) {
    if (o.picking) return RECT{0, 0, 0, 0};
    if (!o.hasSelection) return TextBox(o, T(Str::HintSelect), HintAnchor(o), true);
    RECT r = o.selection;
    InflateRect(&r, Scaled(o, 6), Scaled(o, 6));
    if (o.tool == Tool::Area) r = Union(r, TextBox(o, SizeText(o).c_str(), o.selection, false));
    r = Union(r, PanelRect(o));
    r = Union(r, TipBox(o));
    r = Union(r, EraserCircleRect(o));
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

void InvalidateShape(Overlay& o, const Shape& s) {
    RECT r = ShapeBounds(s, o.scale);
    InvalidateRect(o.hwnd, &r, FALSE);
}

void InvalidatePicture(Overlay& o) {
    if (o.hasSelection) InvalidateRect(o.hwnd, &o.selection, FALSE);
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

// ---------- History ----------

void Checkpoint(Overlay& o) {
    o.undo.push_back(o.shapes);
    o.redo.clear();
}

// ---------- Text ----------

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
    InvalidateShape(o, o.text);
    o.typing = false;
    if (o.text.text.empty()) {
        return;  // an old text emptied is simply deleted; undo brings it back
    }
    if (!o.editingExisting) Checkpoint(o);  // an old text was saved for undo when it was opened
    o.shapes.push_back(o.text);
}

void CancelText(Overlay& o) {
    if (!o.typing) return;
    ChromeChange change(o);
    InvalidateShape(o, o.text);
    o.typing = false;
    if (o.editingExisting && !o.undo.empty()) {
        o.shapes = o.undo.back();  // put the old text back unchanged
        o.undo.pop_back();
        InvalidatePicture(o);
    }
}

// Shows the style of the text being typed on the panel buttons.
void TakeStyleFrom(Overlay& o, const Shape& s) {
    o.textStyle = s.style;
    o.textSize = -1;
    for (int i = 0; i < kSizeCount; ++i) {
        if (std::fabs(TextPresetPx(i, o.scale) - s.style.px) < 0.5f) o.textSize = i;
    }
    o.color = s.color;
    o.colorIndex = kColorCount;
    for (int i = 0; i < kColorCount; ++i) {
        if (kPalette[i] == s.color) o.colorIndex = i;
    }
    if (o.colorIndex == kColorCount) {
        g_settings.customColor = s.color;
        g_settings.hasCustom = true;
    }
}

void StartText(Overlay& o, POINT p) {
    CommitText(o);
    ChromeChange change(o);
    // Clicking an existing text opens it again for typing and formatting.
    for (size_t i = o.shapes.size(); i-- > 0;) {
        if (TextContains(o.shapes[i], p, o.scale)) {
            Checkpoint(o);
            o.text = o.shapes[i];
            o.shapes.erase(o.shapes.begin() + static_cast<std::ptrdiff_t>(i));
            o.typing = true;
            o.editingExisting = true;
            TakeStyleFrom(o, o.text);
            InvalidateShape(o, o.text);
            return;
        }
    }
    o.typing = true;
    o.editingExisting = false;
    o.text = Shape{Tool::Text, o.color, o.size, {p}, L"", o.textStyle};
    InvalidateShape(o, o.text);
}

// Applies a change of style to the text being typed (and to texts typed later).
template <typename Change>
void ChangeTextStyle(Overlay& o, Change change) {
    ChromeChange chrome(o);
    change(o.textStyle);
    if (o.typing) {
        InvalidateShape(o, o.text);
        change(o.text.style);
        InvalidateShape(o, o.text);
    }
}

void OpenFontWindow(Overlay& o) {
    const TextStyle& st = o.typing ? o.text.style : o.textStyle;
    LOGFONTW lf = {};
    lf.lfHeight = -static_cast<LONG>(std::lround(st.px));
    lf.lfWeight = st.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = st.italic;
    lf.lfStrikeOut = st.strike;
    lf.lfUnderline = st.underline;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, st.font.c_str(), LF_FACESIZE - 1);

    CHOOSEFONTW cf = {sizeof(cf)};
    cf.hwndOwner = o.hwnd;
    cf.lpLogFont = &lf;
    cf.rgbColors = o.typing ? o.text.color : o.color;
    // Only TrueType/OpenType fonts: those can be drawn smoothly at any size.
    cf.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_TTONLY | CF_NOVERTFONTS |
               CF_FORCEFONTEXIST | CF_LIMITSIZE;
    cf.nSizeMin = 6;    // points
    cf.nSizeMax = 200;
    g_dialogOpen = true;
    const BOOL ok = ChooseFontW(&cf);
    g_dialogOpen = false;
    SetFocus(o.hwnd);
    if (!ok) return;

    ChangeTextStyle(o, [&](TextStyle& t) {
        t.font = lf.lfFaceName;
        t.px = std::clamp(static_cast<float>(std::abs(lf.lfHeight)), 6.f, 300.f * o.scale);
        t.bold = lf.lfWeight >= FW_SEMIBOLD;
        t.italic = lf.lfItalic != 0;
        t.strike = lf.lfStrikeOut != 0;
        t.underline = lf.lfUnderline != 0;
    });
    Shape probe;
    probe.style = o.textStyle;
    probe.color = cf.rgbColors;
    ChromeChange chrome(o);
    TakeStyleFrom(o, probe);
    if (o.typing) {
        o.text.color = cf.rgbColors;
        InvalidateShape(o, o.text);
    }
}

// ---------- Tools and options ----------

void SelectTool(Overlay& o, Tool t) {
    if (t != Tool::Text) CommitText(o);
    if (t != Tool::Area && !o.hasSelection) return;
    ChromeChange change(o);
    o.tool = t;
    if (t != Tool::Area) o.lastDrawTool = t;
}

void ToggleAreaMode(Overlay& o) {
    SelectTool(o, o.tool == Tool::Area ? o.lastDrawTool : Tool::Area);
}

void SetColorValue(Overlay& o, int index, COLORREF value) {
    ChromeChange change(o);
    o.colorIndex = index;
    o.color = value;
    if (o.typing) {
        o.text.color = value;
        InvalidateShape(o, o.text);
    }
}

void OpenColorWindow(Overlay& o) {
    CHOOSECOLORW cc = {sizeof(cc)};
    cc.hwndOwner = o.hwnd;
    cc.rgbResult = o.color;
    cc.lpCustColors = g_settings.customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    g_dialogOpen = true;
    const BOOL ok = ChooseColorW(&cc);
    g_dialogOpen = false;
    SetFocus(o.hwnd);
    if (!ok) return;
    {
        ChromeChange change(o);
        g_settings.customColor = cc.rgbResult;
        g_settings.hasCustom = true;
    }
    SetColorValue(o, kColorCount, cc.rgbResult);
}

void SetSize(Overlay& o, int s) {
    if (o.tool == Tool::Text) {
        ChangeTextStyle(o, [&](TextStyle& t) { t.px = TextPresetPx(s, o.scale); });
        o.textSize = s;
        return;
    }
    ChromeChange change(o);
    o.size = s;
}

void Undo(Overlay& o) {
    CommitText(o);
    if (o.undo.empty()) return;
    ChromeChange change(o);
    o.redo.push_back(o.shapes);
    o.shapes = o.undo.back();
    o.undo.pop_back();
    InvalidatePicture(o);
}

void Redo(Overlay& o) {
    CommitText(o);
    if (o.redo.empty()) return;
    ChromeChange change(o);
    o.undo.push_back(o.shapes);
    o.shapes = o.redo.back();
    o.redo.pop_back();
    InvalidatePicture(o);
}

// ---------- Drawing lines and figures ----------

bool IsTwoPointTool(Tool t) { return t == Tool::Arrow || t == Tool::Rect || t == Tool::Ellipse; }

void StartStroke(Overlay& o, POINT p) {
    o.drawing = true;
    o.current = Shape{o.tool, o.color, o.size, {p}, L"", {}};
    if (IsTwoPointTool(o.tool)) o.current.points.push_back(p);
    SetCapture(o.hwnd);
    InvalidateShape(o, o.current);
}

void ContinueStroke(Overlay& o, POINT p) {
    std::vector<POINT>& pts = o.current.points;
    if (IsTwoPointTool(o.current.tool)) {
        const RECT before = ShapeBounds(o.current, o.scale);
        pts[1] = p;
        const RECT dirty = Union(before, ShapeBounds(o.current, o.scale));
        InvalidateRect(o.hwnd, &dirty, FALSE);
        return;
    }
    if (pts.back().x == p.x && pts.back().y == p.y) return;
    // Repaint only the newest piece of the line.
    Shape piece{o.current.tool, 0, o.current.size, {pts.back(), p}, L"", {}};
    pts.push_back(p);
    InvalidateShape(o, piece);
}

void EndStroke(Overlay& o) {
    if (!o.drawing) return;
    ChromeChange change(o);  // the undo button becomes active
    o.drawing = false;
    Checkpoint(o);
    o.shapes.push_back(o.current);
    if (GetCapture() == o.hwnd) ReleaseCapture();
}

// ---------- Eraser: removes whole drawn objects, text included ----------

void EraseAt(Overlay& o, POINT p) {
    const float r = EraserRadius(o.size, o.scale);
    for (size_t i = o.shapes.size(); i-- > 0;) {
        if (ShapeTouched(o.shapes[i], p, r, o.scale)) {
            InvalidateShape(o, o.shapes[i]);
            o.shapes.erase(o.shapes.begin() + static_cast<std::ptrdiff_t>(i));
            o.erasedSomething = true;
        }
    }
}

void StartErase(Overlay& o, POINT p) {
    o.erasing = true;
    o.erasedSomething = false;
    o.beforeErase = o.shapes;
    SetCapture(o.hwnd);
    EraseAt(o, p);
}

void EndErase(Overlay& o) {
    if (!o.erasing) return;
    ChromeChange change(o);
    o.erasing = false;
    if (o.erasedSomething) {
        o.undo.push_back(std::move(o.beforeErase));
        o.redo.clear();
    }
    o.beforeErase.clear();
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

void DrawEraserCircle(const Overlay& o, HDC dc) {
    const int r = static_cast<int>(EraserRadius(o.size, o.scale));
    const POINT c = o.mouse;
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    // Dark and light rings: visible on any background.
    HPEN dark = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN light = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(dc, dark);
    Ellipse(dc, c.x - r, c.y - r, c.x + r + 1, c.y + r + 1);
    SelectObject(dc, light);
    Ellipse(dc, c.x - r + 1, c.y - r + 1, c.x + r, c.y + r);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(dark);
    DeleteObject(light);
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
        if (EraserCircleVisible(o)) DrawEraserCircle(o, o.backDc);
        if (o.tool == Tool::Area) {
            const std::wstring size = SizeText(o);
            DrawTextBox(o.backDc, size.c_str(), TextBox(o, size.c_str(), s, false));
        }
        toolbar::Draw(o.backDc, PanelRect(o), PanelState(o), o.scale);
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
    else if (id < kCustomColor) SetColorValue(o, id - kFirstColor, kPalette[id - kFirstColor]);
    else if (id == kCustomColor) OpenColorWindow(o);
    else if (id < kBold) SetSize(o, id - kFirstSize);
    else if (id == kBold) ChangeTextStyle(o, [](TextStyle& t) { t.bold = !t.bold; });
    else if (id == kItalic) ChangeTextStyle(o, [](TextStyle& t) { t.italic = !t.italic; });
    else if (id == kStrike) ChangeTextStyle(o, [](TextStyle& t) { t.strike = !t.strike; });
    else if (id == kFont) OpenFontWindow(o);
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
    if (PanelHit(o, c) != toolbar::kNone) return IDC_ARROW;
    if (o.tool == Tool::Text) return IDC_IBEAM;
    if (o.tool == Tool::Eraser) return PtInRect(&o.selection, c) ? nullptr : IDC_ARROW;  // the circle is the cursor
    if (o.tool != Tool::Area) return IDC_CROSS;
    const int hit = o.drag == Drag::Resize ? o.dragHandle
                    : o.drag == Drag::Move ? Inside
                    : o.drag == Drag::Create ? Outside
                    : HitTest(o, c);
    return CursorFor(hit);
}

// ---------- Window ----------

void RememberSettings(const Overlay& o) {
    g_settings.color = o.colorIndex;
    g_settings.size = o.size;
    g_settings.text = o.textStyle;
    g_settings.text.px = o.textStyle.px / o.scale;
    g_settings.textSize = o.textSize;
}

void LoadSettings(Overlay& o) {
    o.colorIndex = g_settings.color;
    o.color = g_settings.color < kColorCount ? kPalette[g_settings.color] : g_settings.customColor;
    o.size = g_settings.size;
    o.textStyle = g_settings.text;
    o.textStyle.px = g_settings.text.px * o.scale;
    o.textSize = g_settings.textSize;
}

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
            InvalidateShape(o, o.text);
            o.text.text.push_back(L'\n');
            InvalidateShape(o, o.text);
        } else if (key == VK_RETURN) {
            CommitText(o);
        } else if (key == VK_ESCAPE) {
            CancelText(o);
        } else if (ctrl && key == 'B') {
            ChangeTextStyle(o, [](TextStyle& t) { t.bold = !t.bold; });
        } else if (ctrl && key == 'I') {
            ChangeTextStyle(o, [](TextStyle& t) { t.italic = !t.italic; });
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
    if (ch != L'\b' && (ch < L' ' || ch == 127)) return;  // Enter, Esc, Ctrl+letters
    ChromeChange change(o);  // undo button state
    InvalidateShape(o, o.text);
    if (ch == L'\b') {
        if (!o.text.text.empty()) o.text.text.pop_back();
    } else {
        o.text.text.push_back(ch);
    }
    InvalidateShape(o, o.text);
}

void OnMouseDown(Overlay& o, POINT p) {
    if (o.picking) return;
    const int id = PanelHit(o, p);
    if (id != toolbar::kNone) {
        OnPanelClick(o, id);
        return;
    }
    SetFocus(o.hwnd);
    if (o.tool != Tool::Area && !PtInRect(&o.selection, p)) {
        CommitText(o);  // drawing outside the selection would not be seen or saved
        return;
    }
    switch (o.tool) {
        case Tool::Area:
            CommitText(o);
            AreaMouseDown(o, p);
            break;
        case Tool::Text:
            StartText(o, p);
            break;
        case Tool::Eraser:
            CommitText(o);
            StartErase(o, p);
            break;
        default:
            CommitText(o);
            StartStroke(o, p);
            break;
    }
}

void OnMouseMove(Overlay& o, POINT p) {
    {
        ChromeChange change(o);  // hint under the mouse and the eraser circle follow the mouse
        o.mouse = p;
        o.hover = o.drawing || o.erasing || o.drag != Drag::None ? toolbar::kNone : PanelHit(o, p);
    }
    if (o.drawing) {
        ContinueStroke(o, p);
    } else if (o.erasing) {
        EraseAt(o, p);
    } else if (o.drag != Drag::None) {
        AreaMouseMove(o, p);
    }
}

void OnMouseUp(Overlay& o) {
    if (o.drawing) {
        EndStroke(o);
    } else if (o.erasing) {
        EndErase(o);
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
            if (o->erasing) EndErase(*o);
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
                const LPCWSTR cursor = CursorAt(*o, c);
                SetCursor(cursor ? LoadCursorW(nullptr, cursor) : nullptr);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            RememberSettings(*o);
            return 0;
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
    LoadSettings(*o);

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
    if (g_overlays.empty() || g_dialogOpen) {
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
    if (g_dialogOpen) return true;  // the colour or font window needs Esc and Enter itself
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
