#include "overlay.h"

#include <commdlg.h>
#include <dwmapi.h>
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
#include "config.h"
#include "i18n.h"
#include "pin.h"
#include "save.h"
#include "toolbar.h"

namespace {

const wchar_t kOverlayClass[] = L"OscreenSaver.Overlay";

constexpr float kPanelSize = 0.8f;  // panel buttons are 80% of their original size

// Handles around a frame: 0..7 clockwise from top-left. 8 = inside. -1 = outside.
enum Hit { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Inside, Outside = -1 };
enum class Drag { None, Create, Move, Resize };

// Choices that are kept from one screenshot to the next while the program runs.
struct Settings {
    int color = kDefaultColor;  // index in kPalette, or kColorCount = colour from the colour window
    COLORREF customColor = RGB(0, 120, 215);
    bool hasCustom = false;
    COLORREF customColors[16] = {};  // the colour window's own "custom colours" row
    int size = 1;
    TextStyle text;  // px stored at 100% screen scale
    int textSize = 1;
    bool eraseParts = false;
};
Settings g_settings;
bool g_dialogOpen = false;  // colour or font window is open: Esc/Enter belong to it

struct Overlay {
    HWND hwnd = nullptr;
    Snapshot shot;
    Snapshot dim;
    bool picking = false;  // monitor picker mode: right-click chooses this monitor

    bool hasSelection = false;
    RECT selection = {};  // the screenshot area, in window pixels (= snapshot pixels)
    bool firstSelectionDone = false;
    int selectionOrder = 1;  // number of the current selection among side-by-side areas

    // Area mode dragging.
    Drag drag = Drag::None;
    int dragHandle = Outside;
    POINT dragStart = {};
    RECT selectionAtStart = {};
    bool moved = false;
    bool creatingNew = false;  // side-by-side mode: this drag makes a new area

    // Current tool and options.
    Tool tool = Tool::Area;
    Tool lastDrawTool = Tool::Pencil;
    int colorIndex = kDefaultColor;
    COLORREF color = kPalette[kDefaultColor];
    int size = 1;
    TextStyle textStyle;  // look of the next letters typed
    int textSize = 1;
    bool eraseParts = false;

    // Drawn objects and history. Every change saves the previous list, so undo and redo
    // work the same way for drawing, erasing, moving and text edits.
    std::vector<Shape> shapes;
    std::vector<std::vector<Shape>> undo;
    std::vector<std::vector<Shape>> redo;

    bool drawing = false;
    Shape current;

    bool erasing = false;
    bool erasedSomething = false;
    std::vector<Shape> beforeErase;
    std::vector<char> touched;  // partial eraser: objects already cut in this stroke
    Pt lastErase = {};

    int typing = -1;             // index of the text being typed, -1 = none
    int caret = 0;               // text cursor position in that text
    bool typingChanged = false;

    int picked = -1;             // object chosen with the selection tool, -1 = none
    Drag pickDrag = Drag::None;
    int pickHandle = Outside;
    Shape pickOriginal;
    RECT pickBox = {};
    bool pickChanged = false;
    bool pickCheckpoint = false;

    POINT mouse = {-10000, -10000};
    int hover = toolbar::kNone;
    RECT hoverWindow = {};  // window under the cursor, shown with a frame in area mode
    std::wstring flash;     // short message near the cursor (eyedropper)
    RECT flashBox = {};
    bool dark = true;
    COLORREF accent = RGB(0, 120, 215);  // Windows accent colour: frames, handles, numbers

    HDC backDc = nullptr;  // off-screen buffer: draws without flicker
    HBITMAP backBitmap = nullptr;
    HGDIOBJ backOld = nullptr;
    HGDIOBJ backFontOld = nullptr;
    HDC shotDc = nullptr;
    HGDIOBJ shotOld = nullptr;
    HDC dimDc = nullptr;
    HGDIOBJ dimOld = nullptr;
    HFONT font = nullptr;
    Snapshot blurred[kSizeCount];  // made the first time a blur of that strength is used
    float scale = 1.f;
    float panelScale = 1.f;
    int unit = 1;  // whole-pixel size of one "point" of decoration
};

std::vector<Overlay*> g_overlays;

// Areas kept for a side-by-side picture ("add area" button). The current selection is the next one.
struct Piece {
    Overlay* owner;
    RECT area;
    int order;  // number shown in the corner and position in the joined picture
};
std::vector<Piece> g_pieces;

// Windows on the screen at the moment of the snapshot (top first), for "click a window to select it".
std::vector<RECT> g_windows;
constexpr int kMaxAreas = 4;

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

// ---------- Windows under the cursor ----------

BOOL CALLBACK CollectWindow(HWND w, LPARAM) {
    if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;
    BOOL cloaked = FALSE;  // windows hidden on other virtual desktops and similar
    if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;  // tooltips, small helper windows
    RECT r;
    // The visible frame, without the invisible resize border of Windows 10/11.
    if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(w, &r);
    if (r.right - r.left < 40 || r.bottom - r.top < 30) return TRUE;
    g_windows.push_back(r);
    return TRUE;
}

void CollectWindows() {
    g_windows.clear();
    EnumWindows(CollectWindow, 0);  // goes from the top window down
}

// Rectangle of the top window under the point (in this monitor's pixels), or an empty one.
RECT WindowAt(const Overlay& o, POINT p) {
    const POINT screen = {p.x + o.shot.bounds.left, p.y + o.shot.bounds.top};
    for (const RECT& r : g_windows) {
        if (PtInRect(&r, screen)) {
            RECT local = {r.left - o.shot.bounds.left, r.top - o.shot.bounds.top, r.right - o.shot.bounds.left,
                          r.bottom - o.shot.bounds.top};
            const RECT all = {0, 0, o.shot.Width(), o.shot.Height()};
            RECT clipped;
            IntersectRect(&clipped, &local, &all);
            return clipped;
        }
    }
    return RECT{0, 0, 0, 0};
}

// ---------- Areas for the side-by-side picture ----------

bool CompareMode() { return !g_pieces.empty(); }

int AreaCount() {
    int n = static_cast<int>(g_pieces.size());
    for (const Overlay* o : g_overlays) n += o->hasSelection ? 1 : 0;
    return n;
}

std::vector<RECT> PiecesOf(const Overlay& o) {
    std::vector<RECT> list;
    for (const Piece& p : g_pieces) {
        if (p.owner == &o) list.push_back(p.area);
    }
    return list;
}

// Every area of this monitor that is shown clear and can be drawn on.
std::vector<RECT> AreasOf(const Overlay& o) {
    std::vector<RECT> list = PiecesOf(o);
    if (o.hasSelection) list.push_back(o.selection);
    return list;
}

bool InAnyArea(const Overlay& o, POINT p) {
    for (const RECT& r : AreasOf(o)) {
        if (PtInRect(&r, p)) return true;
    }
    return false;
}

// Number shown in the corner of an area (1..4) in side-by-side mode.
int PieceNumber(const Overlay& o, const RECT& r) {
    for (const Piece& p : g_pieces) {
        if (p.owner == &o && EqualRect(&p.area, &r)) return p.order;
    }
    return o.selectionOrder;
}

// Number for a new area: one more than the largest number in use.
int NextOrder() {
    int n = 0;
    for (const Piece& p : g_pieces) n = std::max(n, p.order);
    for (const Overlay* o : g_overlays) {
        if (o->hasSelection) n = std::max(n, o->selectionOrder);
    }
    return n + 1;
}

// The active area (the one with handles) becomes a kept area again.
void KeepSelection(Overlay& o);

// Side-by-side mode: clicking a kept area makes it the active one, so it can be moved and resized.
// The area that was active before is kept with its number.
int KeptAreaAt(const Overlay& o, POINT p) {
    for (size_t i = 0; i < g_pieces.size(); ++i) {
        if (g_pieces[i].owner == &o && PtInRect(&g_pieces[i].area, p)) return static_cast<int>(i);
    }
    return -1;
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

int FrameHit(const Overlay& o, const RECT& frame, POINT p) {
    const int grab = Scaled(o, 6);
    for (int h = TopLeft; h <= Left; ++h) {
        const POINT c = HandleCenter(frame, h);
        if (std::abs(p.x - c.x) <= grab && std::abs(p.y - c.y) <= grab) {
            return h;
        }
    }
    return PtInRect(&frame, p) ? Inside : Outside;
}

int HitTest(const Overlay& o, POINT p) {
    return o.hasSelection ? FrameHit(o, o.selection, p) : Outside;
}

// Moves the edges grabbed by a handle.
RECT ResizeByHandle(const RECT& r0, int handle, LONG dx, LONG dy) {
    LONG l = r0.left, t = r0.top, r = r0.right, b = r0.bottom;
    if (handle == TopLeft || handle == Left || handle == BottomLeft) l += dx;
    if (handle == TopRight || handle == Right || handle == BottomRight) r += dx;
    if (handle == TopLeft || handle == Top || handle == TopRight) t += dy;
    if (handle == BottomLeft || handle == Bottom || handle == BottomRight) b += dy;
    return Normalize(l, t, r, b);
}

// A dark box with white text. The same function gives the box for drawing and for repainting,
// so nothing is left behind when the box moves.
RECT TextBox(const Overlay& o, const wchar_t* text, const RECT& anchor, bool centered) {
    SIZE size = {0, 0};
    GetTextExtentPoint32W(o.backDc, text, static_cast<int>(wcslen(text)), &size);
    const int w = size.cx + static_cast<int>(10 * o.scale) * 2;
    const int h = size.cy + static_cast<int>(5 * o.scale) * 2;
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

void DrawTextBox(const Overlay& o, HDC dc, const wchar_t* text, const RECT& box) {
    toolbar::DrawLabel(dc, box, text, o.dark, o.scale);
}

std::wstring SizeText(const Overlay& o) {
    wchar_t buf[48];
    swprintf(buf, 48, L"%ld × %ld", o.selection.right - o.selection.left,
             o.selection.bottom - o.selection.top);
    return buf;
}

RECT HintAnchor(const Overlay& o) { return RECT{0, 0, o.shot.Width(), Scaled(o, 80)}; }

std::wstring HintText() {
    if (!CompareMode()) return T(Str::HintSelect);
    wchar_t buf[160];
    swprintf(buf, 160, T(Str::HintNextArea), AreaCount() + 1);
    return buf;
}

toolbar::State PanelState(const Overlay& o) {
    toolbar::State st;
    st.tool = o.tool;
    st.color = o.colorIndex;
    st.customColor = g_settings.customColor;
    st.hasCustom = g_settings.hasCustom;
    st.size = o.tool == Tool::Text ? o.textSize : o.size;
    st.bold = o.textStyle.bold;
    st.italic = o.textStyle.italic;
    st.strike = o.textStyle.strike;
    st.eraseParts = o.eraseParts;
    st.canUndo = !o.undo.empty();
    st.canRedo = !o.redo.empty();
    st.canAddArea = o.hasSelection && AreaCount() < kMaxAreas;
    st.compare = CompareMode();
    st.frames = Cfg().frames;
    st.hover = o.hover;
    st.dark = o.dark;
    return st;
}

bool PanelVisible(const Overlay& o) { return !o.picking && (o.hasSelection || !PiecesOf(o).empty()); }

// The panel sits next to the current selection, or next to the last kept area of this monitor.
RECT PanelAnchor(const Overlay& o) {
    if (o.hasSelection) return o.selection;
    const std::vector<RECT> pieces = PiecesOf(o);
    return pieces.empty() ? RECT{0, 0, 0, 0} : pieces.back();
}

RECT PanelRect(const Overlay& o) {
    return toolbar::Place(PanelAnchor(o), ClientRect(o), o.panelScale, PanelState(o));
}

int PanelHit(const Overlay& o, POINT p) {
    if (!PanelVisible(o)) return toolbar::kNone;
    return toolbar::HitTest(PanelRect(o), p, o.panelScale, PanelState(o));
}

RECT TipBox(const Overlay& o) {
    if (o.hover == toolbar::kNone || !PanelVisible(o)) return RECT{0, 0, 0, 0};
    const RECT panel = PanelRect(o);
    const RECT button = toolbar::ButtonRect(panel, o.hover, o.panelScale, PanelState(o));
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
    return o.tool == Tool::Eraser && !o.picking && InAnyArea(o, o.mouse) && PanelHit(o, o.mouse) == toolbar::kNone;
}

RECT EraserCircleRect(const Overlay& o) {
    if (!EraserCircleVisible(o)) return RECT{0, 0, 0, 0};
    const int r = static_cast<int>(EraserRadius(o.size, o.scale)) + 2;
    return RECT{o.mouse.x - r, o.mouse.y - r, o.mouse.x + r + 1, o.mouse.y + r + 1};
}

// ---------- Magnifier (optional, in the settings) ----------
// While choosing an area, a box next to the cursor shows the pixels under it enlarged, the position
// and the colour, so an area can be selected exactly to the pixel.

constexpr int kMagPixels = 15;  // pixels across (odd: the middle one is under the cursor)

int MagCell(const Overlay& o) { return std::max(6, static_cast<int>(8 * o.scale + 0.5f)); }

bool MagnifierVisible(const Overlay& o) {
    if (o.picking) return false;
    if (o.tool != Tool::Picker && (!Cfg().magnifier || o.tool != Tool::Area)) return false;
    const RECT all = ClientRect(o);
    return PtInRect(&all, o.mouse) && PanelHit(o, o.mouse) == toolbar::kNone;
}

// The zoomed square and, under it, the label with position and colour.
void MagnifierLayout(const Overlay& o, RECT& zoom, RECT& label) {
    const int size = kMagPixels * MagCell(o);
    const int gap = static_cast<int>(20 * o.scale);
    // The label fits the longest possible text, so its size does not jump while the mouse moves.
    SIZE text = {0, 0};
    GetTextExtentPoint32W(o.backDc, L"X 0000  Y 0000   #000000", 24, &text);
    const int labelW = std::max(size, static_cast<int>(text.cx + 20 * o.scale));
    const int labelH = static_cast<int>(text.cy + 10 * o.scale);
    const int width = labelW;
    int x = o.mouse.x + gap, y = o.mouse.y + gap;
    if (x + width > o.shot.Width()) x = o.mouse.x - gap - width;  // flip to the other side at the edges
    if (y + size + labelH > o.shot.Height()) y = o.mouse.y - gap - size - labelH;
    zoom = RECT{x, y, x + size, y + size};
    label = RECT{x, y + size + static_cast<int>(2 * o.scale), x + labelW, y + size + static_cast<int>(2 * o.scale) + labelH};
}

RECT MagnifierRect(const Overlay& o) {
    if (!MagnifierVisible(o)) return RECT{0, 0, 0, 0};
    RECT zoom, label;
    MagnifierLayout(o, zoom, label);
    RECT r = Union(zoom, label);
    InflateRect(&r, 2, 2);
    return r;
}

// Frame around the object chosen with the selection tool.
RECT PickFrame(const Overlay& o) {
    if (o.picked < 0 || o.picked >= static_cast<int>(o.shapes.size())) return RECT{0, 0, 0, 0};
    RECT r = ShapeBox(o.shapes[o.picked], o.scale);
    const int pad = Scaled(o, 5);
    InflateRect(&r, pad, pad);
    return r;
}

RECT PickFrameArea(const Overlay& o) {
    RECT r = PickFrame(o);
    if (IsRectEmpty(&r)) return r;
    InflateRect(&r, Scaled(o, 6), Scaled(o, 6));
    return r;
}

RECT HoverWindowArea(const Overlay& o) {
    if (IsRectEmpty(&o.hoverWindow)) return o.hoverWindow;
    RECT r = o.hoverWindow;
    InflateRect(&r, Scaled(o, 3), Scaled(o, 3));
    return r;
}

// Everything outside the picture itself: frames, handles, size label, panel, hints, eraser circle.
RECT ChromeRect(const Overlay& o) {
    if (o.picking) return RECT{0, 0, 0, 0};
    RECT r;
    if (!o.hasSelection) {
        r = Union(TextBox(o, HintText().c_str(), HintAnchor(o), true), MagnifierRect(o));
        r = Union(r, HoverWindowArea(o));
    } else {
        r = o.selection;
        InflateRect(&r, Scaled(o, 6), Scaled(o, 6));
        if (o.tool == Tool::Area) r = Union(r, TextBox(o, SizeText(o).c_str(), o.selection, false));
    }
    if (PanelVisible(o)) {
        r = Union(r, PanelRect(o));
        r = Union(r, TipBox(o));
    }
    r = Union(r, EraserCircleRect(o));
    r = Union(r, PickFrameArea(o));
    r = Union(r, MagnifierRect(o));
    r = Union(r, HoverWindowArea(o));
    r = Union(r, o.flashBox);
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
    for (const RECT& r : AreasOf(o)) InvalidateRect(o.hwnd, &r, FALSE);
}

// ---------- Selection of the screenshot area ----------

void SetSelection(Overlay& o, const RECT& r) {
    ChromeChange change(o);
    if (!o.hasSelection) o.selectionOrder = NextOrder();
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

void Unpick(Overlay& o) {
    if (o.picked < 0) return;
    ChromeChange change(o);
    o.picked = -1;
    o.pickDrag = Drag::None;
}

// ---------- Text ----------

std::vector<const Shape*> VisibleShapes(const Overlay& o) {
    std::vector<const Shape*> list;
    list.reserve(o.shapes.size() + 1);
    for (const Shape& s : o.shapes) list.push_back(&s);
    if (o.drawing) list.push_back(&o.current);
    return list;
}

Caret CurrentCaret(const Overlay& o) {
    Caret c;
    if (o.typing >= 0) {
        c.shape = &o.shapes[o.typing];
        c.style = o.textStyle;
        c.color = o.color;
        c.index = o.caret;
    }
    return c;
}

void InvalidateTyping(Overlay& o) {
    if (o.typing < 0) return;
    Shape probe = o.shapes[o.typing];
    probe.runs.push_back(TextRun{L"W", o.textStyle, o.color});  // room for the cursor in the next look
    InvalidateShape(o, probe);
}

// Ends typing. Empty texts disappear; nothing typed means nothing to undo.
void CommitText(Overlay& o) {
    if (o.typing < 0) return;
    ChromeChange change(o);
    InvalidateTyping(o);
    Shape& s = o.shapes[o.typing];
    const bool empty = std::all_of(s.runs.begin(), s.runs.end(), [](const TextRun& r) { return r.text.empty(); });
    if (empty) {
        if (o.picked == o.typing) o.picked = -1;
        else if (o.picked > o.typing) --o.picked;
        o.shapes.erase(o.shapes.begin() + o.typing);
    }
    if (!o.typingChanged && !o.undo.empty()) {
        o.undo.pop_back();  // the text was only opened, not changed
    }
    o.typing = -1;
}

// Esc while typing: the text returns to how it was before.
void CancelText(Overlay& o) {
    if (o.typing < 0) return;
    ChromeChange change(o);
    InvalidateTyping(o);
    o.typing = -1;
    o.picked = -1;
    if (!o.undo.empty()) {
        o.shapes = o.undo.back();
        o.undo.pop_back();
    }
    InvalidatePicture(o);
}

// Shows the look of the end of a text on the panel, so typing continues in the same look.
void TakeLookFrom(Overlay& o, const TextStyle& style, COLORREF color) {
    o.textStyle = style;
    o.textSize = -1;
    for (int i = 0; i < kSizeCount; ++i) {
        if (std::fabs(TextPresetPx(i, o.scale) - style.px) < 0.5f) o.textSize = i;
    }
    o.color = color;
    o.colorIndex = kColorCount;
    for (int i = 0; i < kColorCount; ++i) {
        if (kPalette[i] == color) o.colorIndex = i;
    }
    if (o.colorIndex == kColorCount) {
        g_settings.customColor = color;
        g_settings.hasCustom = true;
    }
}

// Puts the text cursor at a position; typing continues in the look of the letter before it.
void SetCaret(Overlay& o, int position) {
    if (o.typing < 0) return;
    ChromeChange change(o);
    InvalidateTyping(o);
    const Shape& s = o.shapes[o.typing];
    o.caret = std::clamp(position, 0, TextLength(s));
    TextStyle style;
    COLORREF color;
    if (TextLookAt(s, o.caret, style, color)) TakeLookFrom(o, style, color);
    InvalidateTyping(o);
}

// caret: cursor position, -1 = at the end of the text.
void BeginTyping(Overlay& o, int index, int caret = -1) {
    ChromeChange change(o);
    o.typing = index;
    o.typingChanged = false;
    SetCaret(o, caret < 0 ? TextLength(o.shapes[index]) : caret);
}

int TextAt(const Overlay& o, POINT p) {
    for (int i = static_cast<int>(o.shapes.size()); i-- > 0;) {
        if (o.shapes[i].tool == Tool::Text && ShapeHit(o.shapes[i], ToPt(p), o.scale)) return i;
    }
    return -1;
}

// Text tool click: move the cursor inside the text being typed, open the text under the mouse with
// the cursor where it was clicked, or start a new text.
void StartText(Overlay& o, POINT p) {
    if (o.typing >= 0 && TextAt(o, p) == o.typing) {
        SetCaret(o, TextPositionAt(o.shapes[o.typing], ToPt(p)));
        return;
    }
    CommitText(o);
    const int existing = TextAt(o, p);
    Checkpoint(o);
    if (existing >= 0) {
        BeginTyping(o, existing, TextPositionAt(o.shapes[existing], ToPt(p)));
        return;
    }
    Shape s;
    s.tool = Tool::Text;
    s.color = o.color;
    s.points = {ToPt(p)};
    o.shapes.push_back(s);
    BeginTyping(o, static_cast<int>(o.shapes.size()) - 1);
}

void TypeChar(Overlay& o, wchar_t ch) {
    if (o.typing < 0) return;
    ChromeChange change(o);
    InvalidateTyping(o);
    Shape& s = o.shapes[o.typing];
    if (ch == L'\b') {  // Backspace: the letter before the cursor
        if (o.caret > 0) {
            TextErase(s, o.caret - 1);
            --o.caret;
        }
    } else if (ch == 127) {  // Delete: the letter after the cursor
        TextErase(s, o.caret);
    } else {
        // New letters take the look chosen now; letters typed before keep theirs.
        TextInsert(s, o.caret, ch, o.textStyle, o.color);
        ++o.caret;
    }
    o.typingChanged = true;
    InvalidateTyping(o);
}

// Changes the look of the next letters only.
template <typename Change>
void ChangeTextStyle(Overlay& o, Change change) {
    ChromeChange chrome(o);
    InvalidateTyping(o);
    change(o.textStyle);
    InvalidateTyping(o);
}

void OpenFontWindow(Overlay& o) {
    const TextStyle& st = o.textStyle;
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
    cf.rgbColors = o.color;
    // Only TrueType/OpenType fonts: those can be drawn smoothly at any size.
    cf.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_TTONLY | CF_NOVERTFONTS |
               CF_FORCEFONTEXIST | CF_LIMITSIZE;
    cf.nSizeMin = 6;  // points
    cf.nSizeMax = 200;
    g_dialogOpen = true;
    const BOOL ok = ChooseFontW(&cf);
    g_dialogOpen = false;
    SetFocus(o.hwnd);
    if (!ok) return;

    ChromeChange chrome(o);
    InvalidateTyping(o);
    TextStyle t;
    t.font = lf.lfFaceName;
    t.px = std::clamp(static_cast<float>(std::abs(lf.lfHeight)), 6.f, 300.f * o.scale);
    t.bold = lf.lfWeight >= FW_SEMIBOLD;
    t.italic = lf.lfItalic != 0;
    t.strike = lf.lfStrikeOut != 0;
    t.underline = lf.lfUnderline != 0;
    TakeLookFrom(o, t, cf.rgbColors);
    InvalidateTyping(o);
}

// ---------- Tools and options ----------

void SelectTool(Overlay& o, Tool t) {
    if (t != Tool::Text && t != Tool::Select) CommitText(o);
    if (t != Tool::Area && AreasOf(o).empty()) return;
    ChromeChange change(o);
    if (t != Tool::Select) o.picked = -1;
    o.tool = t;
    if (t != Tool::Area) o.lastDrawTool = t;
}

void ToggleAreaMode(Overlay& o) {
    SelectTool(o, o.tool == Tool::Area ? o.lastDrawTool : Tool::Area);
}

void SetColorValue(Overlay& o, int index, COLORREF value) {
    ChromeChange change(o);
    InvalidateTyping(o);
    o.colorIndex = index;
    o.color = value;
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

void SetEraseParts(Overlay& o, bool parts) {
    ChromeChange change(o);
    o.eraseParts = parts;
}

void Undo(Overlay& o) {
    CommitText(o);
    if (o.undo.empty()) return;
    ChromeChange change(o);
    o.picked = -1;
    o.redo.push_back(o.shapes);
    o.shapes = o.undo.back();
    o.undo.pop_back();
    InvalidatePicture(o);
}

void Redo(Overlay& o) {
    CommitText(o);
    if (o.redo.empty()) return;
    ChromeChange change(o);
    o.picked = -1;
    o.undo.push_back(o.shapes);
    o.shapes = o.redo.back();
    o.redo.pop_back();
    InvalidatePicture(o);
}

void DeletePicked(Overlay& o) {
    if (o.picked < 0 || o.typing >= 0) return;
    ChromeChange change(o);
    Checkpoint(o);
    InvalidateShape(o, o.shapes[o.picked]);
    o.shapes.erase(o.shapes.begin() + o.picked);
    o.picked = -1;
}

// ---------- Drawing lines and figures ----------

void StartStroke(Overlay& o, POINT p) {
    o.drawing = true;
    o.current = Shape{};
    o.current.tool = o.tool;
    o.current.color = o.color;
    o.current.size = o.size;
    o.current.points = {ToPt(p)};
    if (IsTwoPointTool(o.tool)) o.current.points.push_back(ToPt(p));
    SetCapture(o.hwnd);
    InvalidateShape(o, o.current);
}

void ContinueStroke(Overlay& o, POINT p) {
    std::vector<Pt>& pts = o.current.points;
    if (IsTwoPointTool(o.current.tool)) {
        const RECT before = ShapeBounds(o.current, o.scale);
        pts[1] = ToPt(p);
        const RECT dirty = Union(before, ShapeBounds(o.current, o.scale));
        InvalidateRect(o.hwnd, &dirty, FALSE);
        return;
    }
    const Pt np = ToPt(p);
    if (pts.back().x == np.x && pts.back().y == np.y) return;
    // Repaint only the newest piece of the line.
    Shape piece;
    piece.tool = o.current.tool;
    piece.size = o.current.size;
    piece.points = {pts.back(), np};
    pts.push_back(np);
    InvalidateShape(o, piece);
}

void EndStroke(Overlay& o) {
    if (!o.drawing) return;
    ChromeChange change(o);  // the undo button becomes active
    o.drawing = false;
    if (GetCapture() == o.hwnd) ReleaseCapture();
    // A click without dragging makes no arrow, rectangle, ellipse or blur: such an invisible object
    // could later be picked by mistake.
    if (IsTwoPointTool(o.current.tool)) {
        const Pt a = o.current.points[0], b = o.current.points[1];
        if (std::fabs(a.x - b.x) < 2 && std::fabs(a.y - b.y) < 2) {
            InvalidateShape(o, o.current);
            return;
        }
    }
    Checkpoint(o);
    o.shapes.push_back(o.current);
}

// ---------- Eraser ----------
// Whole mode removes every object the eraser touches. Part mode rubs out only what is under the
// eraser, like in Paint; the rubbed-out part is kept with the object, so it moves and scales with it.

void InvalidateAround(Overlay& o, Pt a, Pt b, float r) {
    RECT dirty = {static_cast<LONG>(std::min(a.x, b.x) - r - 2), static_cast<LONG>(std::min(a.y, b.y) - r - 2),
                  static_cast<LONG>(std::max(a.x, b.x) + r + 3), static_cast<LONG>(std::max(a.y, b.y) + r + 3)};
    InvalidateRect(o.hwnd, &dirty, FALSE);
}

void EraseAt(Overlay& o, POINT pos) {
    const Pt p = ToPt(pos);
    const float r = EraserRadius(o.size, o.scale);
    if (!o.eraseParts) {
        for (size_t i = o.shapes.size(); i-- > 0;) {
            if (ShapeTouched(o.shapes[i], p, r, o.scale)) {
                InvalidateShape(o, o.shapes[i]);
                o.shapes.erase(o.shapes.begin() + static_cast<std::ptrdiff_t>(i));
                o.erasedSomething = true;
            }
        }
        return;
    }
    const Pt from = o.lastErase;
    const RECT reach = {static_cast<LONG>(std::min(from.x, p.x) - r), static_cast<LONG>(std::min(from.y, p.y) - r),
                        static_cast<LONG>(std::max(from.x, p.x) + r + 1),
                        static_cast<LONG>(std::max(from.y, p.y) + r + 1)};
    for (size_t i = 0; i < o.shapes.size(); ++i) {
        const RECT bounds = ShapeBounds(o.shapes[i], o.scale);
        RECT common;
        if (!IntersectRect(&common, &bounds, &reach)) continue;
        Shape& s = o.shapes[i];
        if (!o.touched[i]) {
            o.touched[i] = 1;
            s.erasures.push_back(Erasure{{from}, 2 * r});
        }
        s.erasures.back().points.push_back(p);
        o.erasedSomething = true;
    }
    InvalidateAround(o, from, p, r);
    o.lastErase = p;
}

void StartErase(Overlay& o, POINT p) {
    o.erasing = true;
    o.erasedSomething = false;
    o.beforeErase = o.shapes;
    o.touched.assign(o.shapes.size(), 0);
    o.lastErase = ToPt(p);
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

// ---------- Selection tool: pick, move, resize, edit text ----------

int ShapeAt(const Overlay& o, POINT p) {
    for (int i = static_cast<int>(o.shapes.size()); i-- > 0;) {
        if (ShapeHit(o.shapes[i], ToPt(p), o.scale)) return i;
    }
    return -1;
}

void PickMouseDown(Overlay& o, POINT p) {
    // A handle or the inside of the current frame: start resizing or moving.
    int frameHit = o.picked >= 0 ? FrameHit(o, PickFrame(o), p) : Outside;
    int index = frameHit == Outside ? ShapeAt(o, p) : o.picked;
    if (o.typing >= 0 && index != o.typing) {
        CommitText(o);  // may remove an empty text and shift the list, so look again
        frameHit = o.picked >= 0 ? FrameHit(o, PickFrame(o), p) : Outside;
        index = frameHit == Outside ? ShapeAt(o, p) : o.picked;
    }
    if (index < 0) {
        Unpick(o);
        return;
    }
    {
        ChromeChange change(o);
        o.picked = index;
    }
    o.pickDrag = (frameHit >= TopLeft && frameHit <= Left) ? Drag::Resize : Drag::Move;
    o.pickHandle = frameHit;
    o.pickOriginal = o.shapes[index];
    o.pickBox = ShapeBox(o.shapes[index], o.scale);
    o.dragStart = p;
    o.pickChanged = false;
    o.pickCheckpoint = false;
    SetCapture(o.hwnd);
    // Clicking a text opens it for typing right away.
    if (o.shapes[index].tool == Tool::Text && o.typing != index) {
        Checkpoint(o);
        o.pickCheckpoint = true;
        BeginTyping(o, index);
    }
}

void PickMouseMove(Overlay& o, POINT p) {
    if (o.pickDrag == Drag::None || o.picked < 0) return;
    const LONG dx = p.x - o.dragStart.x;
    const LONG dy = p.y - o.dragStart.y;
    if (!o.pickChanged && std::abs(dx) < 3 && std::abs(dy) < 3) return;
    if (!o.pickChanged) {
        o.pickChanged = true;
        if (o.typing == o.picked) {
            o.typingChanged = true;  // part of the same text edit for undo
        } else if (!o.pickCheckpoint) {
            Checkpoint(o);
            o.pickCheckpoint = true;
        }
    }
    ChromeChange change(o);
    Shape& s = o.shapes[o.picked];
    InvalidateShape(o, s);
    if (o.pickDrag == Drag::Move) {
        s = o.pickOriginal;
        MoveShape(s, static_cast<float>(dx), static_cast<float>(dy));
    } else {
        const RECT b0 = o.pickBox;
        const RECT nb = ResizeByHandle(b0, o.pickHandle, dx, dy);
        const int h = o.pickHandle;
        const float w0 = static_cast<float>(b0.right - b0.left);
        const float h0 = static_cast<float>(b0.bottom - b0.top);
        float sx = w0 >= 1 ? std::max(4.f, static_cast<float>(nb.right - nb.left)) / w0 : 1.f;
        float sy = h0 >= 1 ? std::max(4.f, static_cast<float>(nb.bottom - nb.top)) / h0 : 1.f;
        if (h == Left || h == Right) sy = s.tool == Tool::Text ? sx : 1.f;
        if (h == Top || h == Bottom) sx = 1.f;
        const bool leftSide = h == TopLeft || h == Left || h == BottomLeft;
        const bool topSide = h == TopLeft || h == Top || h == TopRight;
        const Pt anchor{static_cast<float>(leftSide ? b0.right : b0.left),
                        static_cast<float>(topSide ? b0.bottom : b0.top)};
        ScaleShape(s, o.pickOriginal, anchor, sx, sy);
    }
    InvalidateShape(o, s);
}

void PickMouseUp(Overlay& o) {
    // A click on a text (without dragging) puts the text cursor there.
    if (!o.pickChanged && o.picked >= 0 && o.picked == o.typing) {
        SetCaret(o, TextPositionAt(o.shapes[o.typing], ToPt(o.dragStart)));
    }
    o.pickDrag = Drag::None;
    if (GetCapture() == o.hwnd) ReleaseCapture();
    ChromeChange change(o);  // undo button
}

// ---------- Blur ----------

// Blurred copies are made only for the strengths actually used, the first time they are needed.
BlurSource BlurOf(Overlay& o) {
    auto ensure = [&](const Shape& s) {
        if (s.tool != Tool::Blur) return;
        const int level = std::clamp(s.size, 0, kSizeCount - 1);
        if (!o.blurred[level].bitmap) MakeBlurredCopy(o.shot, BlurRadius(level, o.scale), o.blurred[level]);
    };
    for (const Shape& s : o.shapes) ensure(s);
    if (o.drawing) ensure(o.current);
    BlurSource b;
    b.width = o.shot.Width();
    b.height = o.shot.Height();
    for (int i = 0; i < kSizeCount; ++i) b.pixels[i] = o.blurred[i].pixels;
    return b;
}

// ---------- Painting ----------

void DrawFrame(const Overlay& o, HDC dc) {
    const RECT& s = o.selection;
    HPEN pen = CreatePen(PS_SOLID, Scaled(o, 1), o.accent);
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

// Number in the top-left corner of an area in side-by-side mode.
void DrawBadge(const Overlay& o, HDC dc, const RECT& area, int number) {
    const int d = Scaled(o, 18);
    const RECT box = {area.left + Scaled(o, 4), area.top + Scaled(o, 4), area.left + Scaled(o, 4) + d,
                      area.top + Scaled(o, 4) + d};
    HBRUSH brush = CreateSolidBrush(o.accent);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, box.left, box.top, box.right + 1, box.bottom + 1);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
    wchar_t text[4];
    swprintf(text, 4, L"%d", number);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r = box;
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// A kept area: solid frame and its number.
void DrawPieceFrame(const Overlay& o, HDC dc, const RECT& a) {
    HPEN pen = CreatePen(PS_SOLID, Scaled(o, 2), o.accent);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, a.left, a.top, a.right, a.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    DrawBadge(o, dc, a, PieceNumber(o, a));
}

void DrawPickFrame(const Overlay& o, HDC dc) {
    const RECT f = PickFrame(o);
    if (IsRectEmpty(&f)) return;
    HPEN dash = CreatePen(PS_DOT, 1, o.accent);
    HGDIOBJ oldPen = SelectObject(dc, dash);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(255, 255, 255));
    Rectangle(dc, f.left, f.top, f.right, f.bottom);
    HPEN solid = CreatePen(PS_SOLID, Scaled(o, 1), o.accent);
    SelectObject(dc, solid);
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    const int half = Scaled(o, 3);
    for (int h = TopLeft; h <= Left; ++h) {
        const POINT c = HandleCenter(f, h);
        Rectangle(dc, c.x - half, c.y - half, c.x + half + 1, c.y + half + 1);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(dash);
    DeleteObject(solid);
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

void DrawMagnifier(const Overlay& o, HDC dc) {
    RECT zoom, label;
    MagnifierLayout(o, zoom, label);
    const int cell = MagCell(o);
    const int half = kMagPixels / 2;
    // Enlarged pixels of the clear screenshot (edges outside the screen stay black).
    HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    FillRect(dc, &zoom, black);
    const int sx = o.mouse.x - half, sy = o.mouse.y - half;
    const int l = std::max(0, sx), t = std::max(0, sy);
    const int r = std::min(o.shot.Width(), sx + kMagPixels), b = std::min(o.shot.Height(), sy + kMagPixels);
    if (r > l && b > t) {
        SetStretchBltMode(dc, COLORONCOLOR);  // sharp pixels, no smoothing
        StretchBlt(dc, zoom.left + (l - sx) * cell, zoom.top + (t - sy) * cell, (r - l) * cell, (b - t) * cell,
                   o.shotDc, l, t, r - l, b - t, SRCCOPY);
    }
    // Thin pixel grid and the pixel under the cursor.
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
    HGDIOBJ oldPen = SelectObject(dc, grid);
    const int oldRop = SetROP2(dc, R2_MASKPEN);
    for (int i = 1; i < kMagPixels; ++i) {
        MoveToEx(dc, zoom.left + i * cell, zoom.top, nullptr);
        LineTo(dc, zoom.left + i * cell, zoom.bottom);
        MoveToEx(dc, zoom.left, zoom.top + i * cell, nullptr);
        LineTo(dc, zoom.right, zoom.top + i * cell);
    }
    SetROP2(dc, oldRop);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const RECT mid = {zoom.left + half * cell, zoom.top + half * cell, zoom.left + (half + 1) * cell,
                      zoom.top + (half + 1) * cell};
    SelectObject(dc, GetStockObject(WHITE_PEN));
    Rectangle(dc, mid.left - 1, mid.top - 1, mid.right + 1, mid.bottom + 1);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    Rectangle(dc, mid.left, mid.top, mid.right, mid.bottom);
    HPEN frame = CreatePen(PS_SOLID, std::max(1, o.unit), o.accent);
    SelectObject(dc, frame);
    Rectangle(dc, zoom.left, zoom.top, zoom.right, zoom.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(grid);
    DeleteObject(frame);

    // Position (on this monitor) and colour of the pixel under the cursor.
    COLORREF c = RGB(0, 0, 0);
    if (o.mouse.x >= 0 && o.mouse.y >= 0 && o.mouse.x < o.shot.Width() && o.mouse.y < o.shot.Height()) {
        const uint32_t px = o.shot.pixels[static_cast<size_t>(o.mouse.y) * o.shot.Width() + o.mouse.x];
        c = RGB((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF);
    }
    wchar_t text[64];
    swprintf(text, 64, L"X %ld  Y %ld   #%02X%02X%02X", o.mouse.x, o.mouse.y, GetRValue(c), GetGValue(c),
             GetBValue(c));
    toolbar::DrawLabel(dc, label, text, o.dark, o.scale);
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
        DrawTextBox(o, o.backDc, hint, TextBox(o, hint, ClientRect(o), true));
    } else {
        // Clear picture and drawings in every area: the current selection and the kept ones.
        const BlurSource blur = BlurOf(o);
        const std::vector<const Shape*> shapes = VisibleShapes(o);
        for (const RECT& a : AreasOf(o)) {
            BitBlt(o.backDc, a.left, a.top, a.right - a.left, a.bottom - a.top, o.shotDc, a.left, a.top, SRCCOPY);
            RECT clip;
            if (IntersectRect(&clip, &a, &p)) {
                DrawShapes(o.backDc, clip, POINT{0, 0}, shapes, CurrentCaret(o), blur, o.scale);
            }
        }
        for (const RECT& a : PiecesOf(o)) DrawPieceFrame(o, o.backDc, a);
        if (o.hasSelection) {
            DrawFrame(o, o.backDc);
            if (CompareMode()) DrawBadge(o, o.backDc, o.selection, PieceNumber(o, o.selection));
        }
        DrawPickFrame(o, o.backDc);
        if (EraserCircleVisible(o)) DrawEraserCircle(o, o.backDc);
        if (o.hasSelection && o.tool == Tool::Area) {
            const std::wstring size = SizeText(o);
            DrawTextBox(o, o.backDc, size.c_str(), TextBox(o, size.c_str(), o.selection, false));
        }
        if (!o.hasSelection) {
            const std::wstring hint = HintText();
            DrawTextBox(o, o.backDc, hint.c_str(), TextBox(o, hint.c_str(), HintAnchor(o), true));
        }
        if (PanelVisible(o)) {
            toolbar::Draw(o.backDc, PanelRect(o), PanelState(o), o.panelScale);
            if (o.hover != toolbar::kNone) {
                DrawTextBox(o, o.backDc, toolbar::Tip(o.hover), TipBox(o));
            }
        }
    }
    if (!IsRectEmpty(&o.hoverWindow) && !o.picking) {
        // The window that a click would select.
        HPEN pen = CreatePen(PS_SOLID, Scaled(o, 2), o.accent);
        HGDIOBJ oldPen = SelectObject(o.backDc, pen);
        HGDIOBJ oldBrush = SelectObject(o.backDc, GetStockObject(NULL_BRUSH));
        const RECT& r = o.hoverWindow;
        Rectangle(o.backDc, r.left + 1, r.top + 1, r.right, r.bottom);
        SelectObject(o.backDc, oldBrush);
        SelectObject(o.backDc, oldPen);
        DeleteObject(pen);
    }
    if (!o.flash.empty()) DrawTextBox(o, o.backDc, o.flash.c_str(), o.flashBox);
    if (MagnifierVisible(o)) DrawMagnifier(o, o.backDc);
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
    g_pieces.clear();
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

// The selected part of one monitor's screenshot with everything drawn on it.
bool ComposeArea(Overlay& o, const RECT& s, Snapshot& result) {
    const int w = s.right - s.left;
    const int h = s.bottom - s.top;
    if (w <= 0 || h <= 0 || !CreateBlankSnapshot(w, h, result)) return false;
    for (int y = 0; y < h; ++y) {
        std::memcpy(result.pixels + static_cast<size_t>(y) * w,
                    o.shot.pixels + static_cast<size_t>(s.top + y) * o.shot.Width() + s.left,
                    static_cast<size_t>(w) * 4);
    }
    const BlurSource blur = BlurOf(o);
    HDC memory = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(memory, result.bitmap);
    DrawShapes(memory, RECT{0, 0, w, h}, POINT{s.left, s.top}, VisibleShapes(o), Caret{}, blur, o.scale);
    GdiFlush();
    SelectObject(memory, old);
    DeleteDC(memory);
    return true;
}

// Joins 2-4 pictures without overlapping: two side by side; a third centred below them;
// a fourth next to the third (2 x 2). Each row is centred; free space is black.
bool JoinPictures(std::vector<Snapshot>& parts, Snapshot& out) {
    std::vector<std::vector<size_t>> rows;
    if (parts.size() <= 2) {
        rows.push_back({0});
        if (parts.size() == 2) rows[0].push_back(1);
    } else {
        rows.push_back({0, 1});
        rows.push_back({2});
        if (parts.size() == 4) rows[1].push_back(3);
    }
    std::vector<int> rowW(rows.size(), 0), rowH(rows.size(), 0);
    int width = 0, height = 0;
    for (size_t r = 0; r < rows.size(); ++r) {
        for (size_t i : rows[r]) {
            rowW[r] += parts[i].Width();
            rowH[r] = std::max(rowH[r], parts[i].Height());
        }
        width = std::max(width, rowW[r]);
        height += rowH[r];
    }
    if (!CreateBlankSnapshot(width, height, out)) return false;
    // Background colour from the settings (black by default), in the picture's BGR pixel order.
    const COLORREF bg = Cfg().background;
    const uint32_t fill = (static_cast<uint32_t>(GetRValue(bg)) << 16) | (static_cast<uint32_t>(GetGValue(bg)) << 8) |
                          GetBValue(bg);
    std::fill(out.pixels, out.pixels + static_cast<size_t>(width) * height, fill);
    int top = 0;
    for (size_t r = 0; r < rows.size(); ++r) {
        int x = (width - rowW[r]) / 2;
        for (size_t i : rows[r]) {
            const Snapshot& p = parts[i];
            const int y0 = top + (rowH[r] - p.Height()) / 2;
            for (int y = 0; y < p.Height(); ++y) {
                std::memcpy(out.pixels + static_cast<size_t>(y0 + y) * width + x,
                            p.pixels + static_cast<size_t>(y) * p.Width(), static_cast<size_t>(p.Width()) * 4);
            }
            x += p.Width();
        }
        top += rowH[r];
    }
    return true;
}

// Thin grey frame around a part of a joined picture (inside its own edge, so parts stay touching).
void AddFrame(Snapshot& part, int width) {
    const int w = part.Width(), h = part.Height();
    const uint32_t grey = 0x00A0A0A0u;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x < width || y < width || x >= w - width || y >= h - width) {
                part.pixels[static_cast<size_t>(y) * w + x] = grey;
            }
        }
    }
}

// Saves and copies the result of the whole session: one area, or up to four joined together.
// What happens with the finished picture.
enum class Finishing { SaveAndCopy, CopyOnly, Pin, Text };

void FinishSession(Finishing how = Finishing::SaveAndCopy) {
    if (how == Finishing::SaveAndCopy && Cfg().clipboardOnly) how = Finishing::CopyOnly;
    for (Overlay* o : g_overlays) CommitText(*o);
    std::vector<Piece> areas = g_pieces;
    for (Overlay* o : g_overlays) {
        if (o->hasSelection && !o->picking) areas.push_back(Piece{o, o->selection, o->selectionOrder});
    }
    if (areas.empty()) return;
    std::sort(areas.begin(), areas.end(), [](const Piece& a, const Piece& b) { return a.order < b.order; });

    std::vector<Snapshot> parts;
    for (const Piece& a : areas) {
        Snapshot part;
        if (ComposeArea(*a.owner, a.area, part)) {
            if (areas.size() > 1 && Cfg().frames) AddFrame(part, std::max(1, a.owner->unit));
            parts.push_back(part);
        }
    }
    Snapshot result;
    bool ok = false;
    if (parts.size() == 1) {
        result = parts[0];
        parts.clear();
        ok = true;
    } else if (!parts.empty()) {
        ok = JoinPictures(parts, result);
    }
    for (Snapshot& p : parts) FreeSnapshot(p);
    if (!ok) {
        NotifySaveFailed();
        return;
    }
    // Where the first area was on the screen: a pinned picture appears right there.
    const Piece& first = areas.front();
    const POINT screenAt = {first.owner->shot.bounds.left + first.area.left,
                            first.owner->shot.bounds.top + first.area.top};
    const RECT all = {0, 0, result.Width(), result.Height()};
    HWND owner = g_overlays.empty() ? nullptr : g_overlays.front()->hwnd;
    switch (how) {
        case Finishing::Pin:
            CloseAll();
            PinPicture(result, screenAt);  // the pinned window keeps the picture
            return;
        case Finishing::Text:
            CloseAll();
            StartTextRecognition(result);  // the text goes to the clipboard when it is read
            return;
        case Finishing::CopyOnly:
            CopyAreaToClipboard(result, all, owner);
            FreeSnapshot(result);
            CloseAll();
            NotifyCopied();
            return;
        case Finishing::SaveAndCopy: {
            std::wstring path;
            const bool saved = SaveArea(result, all, path);
            CopyAreaToClipboard(result, all, owner);
            FreeSnapshot(result);
            CloseAll();
            if (saved) NotifySaved(path);
            else NotifySaveFailed();
            return;
        }
    }
}

void Finish(Overlay&) { FinishSession(); }

bool CreateOverlayWindow(HINSTANCE instance, HMONITOR monitor, bool picking);

// Other monitors are frozen too, so the next area can be taken there.
void FreezeOtherMonitors() {
    for (HMONITOR m : AllMonitors()) {
        const bool open = std::any_of(g_overlays.begin(), g_overlays.end(), [&](const Overlay* x) {
            return MonitorFromRect(&x->shot.bounds, MONITOR_DEFAULTTONULL) == m;
        });
        if (!open) CreateOverlayWindow(GetModuleHandleW(nullptr), m, false);
    }
    for (Overlay* x : g_overlays) InvalidateRect(x->hwnd, nullptr, FALSE);  // new hint "area N of 4"
}

// Keeps the current selection as one part of a side-by-side picture and lets the user select the next
// one, on this or another monitor.
void AddArea(Overlay& o) {
    if (!o.hasSelection || AreaCount() >= kMaxAreas) return;
    CommitText(o);
    g_pieces.push_back(Piece{&o, o.selection, o.selectionOrder});
    o.hasSelection = false;
    o.picked = -1;
    o.tool = Tool::Area;
    InvalidateRect(o.hwnd, nullptr, FALSE);
    FreezeOtherMonitors();
}

void KeepSelection(Overlay& o);

// Ctrl + drag outside the areas: the next area of a side-by-side picture, whatever tool is chosen.
// The current tool stays, so drawing can continue right after.
bool StartAreaWithCtrl(Overlay& o, POINT p) {
    if (AreaCount() == 0 || InAnyArea(o, p)) return false;
    for (Overlay* x : g_overlays) {
        CommitText(*x);
        x->picked = -1;
    }
    if (AreaCount() >= kMaxAreas) return true;  // already 4 areas: ignore the click
    for (Overlay* x : g_overlays) KeepSelection(*x);
    FreezeOtherMonitors();
    SetCapture(o.hwnd);
    o.dragStart = p;
    o.selectionAtStart = o.selection;
    o.moved = false;
    o.creatingNew = true;
    o.drag = Drag::Create;
    return true;
}

void OnPanelClick(Overlay& o, int id) {
    using namespace toolbar;
    if (id < kFirstColor) SelectTool(o, static_cast<Tool>(id));
    else if (id < kCustomColor) SetColorValue(o, id - kFirstColor, kPalette[id - kFirstColor]);
    else if (id == kCustomColor) OpenColorWindow(o);
    else if (id < kEraseWhole) SetSize(o, id - kFirstSize);
    else if (id == kEraseWhole) SetEraseParts(o, false);
    else if (id == kErasePart) SetEraseParts(o, true);
    else if (id == kBold) ChangeTextStyle(o, [](TextStyle& t) { t.bold = !t.bold; });
    else if (id == kItalic) ChangeTextStyle(o, [](TextStyle& t) { t.italic = !t.italic; });
    else if (id == kStrike) ChangeTextStyle(o, [](TextStyle& t) { t.strike = !t.strike; });
    else if (id == kFont) OpenFontWindow(o);
    else if (id == kUndo) Undo(o);
    else if (id == kRedo) Redo(o);
    else if (id == kAddArea) AddArea(o);
    else if (id == kFrames) {
        ChromeChange change(o);
        Cfg().frames = !Cfg().frames;
        SaveConfig();
    }
    else if (id == kOcr) FinishSession(Finishing::Text);
    else if (id == kPin) FinishSession(Finishing::Pin);
    else if (id == kCopy) FinishSession(Finishing::CopyOnly);
    else if (id == kSave) Finish(o);
    else if (id == kClose) CloseAll();
}

// ---------- Mouse in area mode ----------

// Side-by-side mode: before a new area, the active one is kept with its number. False if there is no
// room for one more area.
bool MakeRoomForNewArea(Overlay& o) {
    const bool someActive =
        std::any_of(g_overlays.begin(), g_overlays.end(), [](const Overlay* x) { return x->hasSelection; });
    if (someActive && AreaCount() >= kMaxAreas) return false;
    for (Overlay* x : g_overlays) KeepSelection(*x);
    if (AreaCount() >= kMaxAreas) return false;
    o.creatingNew = true;
    return true;
}

void AreaMouseDown(Overlay& o, POINT p) {
    SetCapture(o.hwnd);
    o.dragStart = p;
    o.selectionAtStart = o.selection;
    o.moved = false;
    o.creatingNew = false;
    const int hit = HitTest(o, p);
    if (hit >= TopLeft && hit <= Left) {
        o.drag = Drag::Resize;
        o.dragHandle = hit;
    } else if (hit == Inside) {
        o.drag = Drag::Move;
    } else if (CompareMode() && KeptAreaAt(o, p) >= 0) {
        // Side-by-side mode: a click on a kept area only makes it the active one.
        const Piece chosen = g_pieces[KeptAreaAt(o, p)];
        for (Overlay* x : g_overlays) {
            if (x->hasSelection) KeepSelection(*x);
        }
        g_pieces.erase(std::remove_if(g_pieces.begin(), g_pieces.end(),
                                      [&](const Piece& q) { return q.owner == &o && EqualRect(&q.area, &chosen.area); }),
                       g_pieces.end());
        {
            ChromeChange change(o);
            o.selection = chosen.area;
            o.selectionOrder = chosen.order;
            o.hasSelection = true;
        }
        InvalidateRect(o.hwnd, nullptr, FALSE);
        ReleaseCapture();
    } else {
        o.drag = Drag::Create;  // in side-by-side mode the new area starts only when the mouse moves
    }
}

void KeepSelection(Overlay& o) {
    if (!o.hasSelection) return;
    CommitText(o);
    g_pieces.push_back(Piece{&o, o.selection, o.selectionOrder});
    o.hasSelection = false;
    o.picked = -1;
    InvalidateRect(o.hwnd, nullptr, FALSE);
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
            if (CompareMode() && !o.creatingNew && !MakeRoomForNewArea(o)) {
                o.drag = Drag::None;
                ReleaseCapture();
                return;
            }
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
        case Drag::Resize:
            SetSelection(o, ResizeByHandle(s0, o.dragHandle, dx, dy));
            break;
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
        // A click selects the window under the cursor; where there is no window, the whole screen.
        const RECT window = WindowAt(o, o.dragStart);
        if (!CompareMode()) {
            SetSelection(o, IsRectEmpty(&window) ? ClientRect(o) : window);
        } else if (!IsRectEmpty(&window) && (o.creatingNew || MakeRoomForNewArea(o))) {
            SetSelection(o, window);
        }
        ChromeChange change(o);
        o.hoverWindow = RECT{};
    }
    if (drag == Drag::Create && o.hasSelection && !o.firstSelectionDone && !CompareMode()) {
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
    switch (o.tool) {
        case Tool::Text: return IDC_IBEAM;
        case Tool::Eraser: return InAnyArea(o, c) ? nullptr : IDC_ARROW;  // the circle is the cursor
        case Tool::Select: {
            if (o.pickDrag == Drag::Resize) return CursorFor(o.pickHandle);
            if (o.pickDrag == Drag::Move) return IDC_SIZEALL;
            const int hit = o.picked >= 0 ? FrameHit(o, PickFrame(o), c) : Outside;
            if (hit != Outside) return CursorFor(hit);
            return ShapeAt(o, c) >= 0 ? IDC_SIZEALL : IDC_ARROW;
        }
        case Tool::Area: {
            const int hit = o.drag == Drag::Resize ? o.dragHandle
                            : o.drag == Drag::Move ? Inside
                            : o.drag == Drag::Create ? Outside
                            : HitTest(o, c);
            return CursorFor(hit);
        }
        default: return IDC_CROSS;
    }
}

// ---------- Window ----------

void RememberSettings(const Overlay& o) {
    g_settings.color = o.colorIndex;
    g_settings.size = o.size;
    g_settings.text = o.textStyle;
    g_settings.text.px = o.textStyle.px / o.scale;
    g_settings.textSize = o.textSize;
    g_settings.eraseParts = o.eraseParts;
}

void LoadSettings(Overlay& o) {
    o.colorIndex = g_settings.color;
    o.color = g_settings.color < kColorCount ? kPalette[g_settings.color] : g_settings.customColor;
    o.size = g_settings.size;
    o.textStyle = g_settings.text;
    o.textStyle.px = g_settings.text.px * o.scale;
    o.textSize = g_settings.textSize;
    o.eraseParts = g_settings.eraseParts;
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
    for (Snapshot& b : o->blurred) FreeSnapshot(b);
    FreeSnapshot(o->shot);
    FreeSnapshot(o->dim);
    delete o;
}

void OnKeyDown(Overlay& o, WPARAM key) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (o.typing >= 0) {
        if (key == VK_RETURN && shift) {
            TypeChar(o, L'\n');
        } else if (key == VK_RETURN) {
            CommitText(o);
        } else if (key == VK_ESCAPE) {
            CancelText(o);
        } else if (ctrl && key == 'B') {
            ChangeTextStyle(o, [](TextStyle& t) { t.bold = !t.bold; });
        } else if (ctrl && key == 'I') {
            ChangeTextStyle(o, [](TextStyle& t) { t.italic = !t.italic; });
        } else if (ctrl && key == 'Z') {
            Undo(o);
        } else if (key == VK_DELETE) {
            TypeChar(o, 127);
        } else if (key == VK_LEFT) {
            SetCaret(o, o.caret - 1);
        } else if (key == VK_RIGHT) {
            SetCaret(o, o.caret + 1);
        } else if (key == VK_HOME) {
            SetCaret(o, TextMoveOnLine(o.shapes[o.typing], o.caret, -2));
        } else if (key == VK_END) {
            SetCaret(o, TextMoveOnLine(o.shapes[o.typing], o.caret, 2));
        } else if (key == VK_UP) {
            SetCaret(o, TextMoveOnLine(o.shapes[o.typing], o.caret, -1));
        } else if (key == VK_DOWN) {
            SetCaret(o, TextMoveOnLine(o.shapes[o.typing], o.caret, 1));
        }
        return;  // letters arrive as WM_CHAR
    }
    if (key == VK_ESCAPE || key == VK_RETURN) {
        OverlayKey(static_cast<unsigned>(key));  // normally caught by the hook first
    } else if (o.picking) {
        return;
    } else if (key == VK_TAB) {
        ToggleAreaMode(o);
    } else if (key == VK_DELETE) {
        DeletePicked(o);
    } else if (ctrl && key == 'Z') {
        shift ? Redo(o) : Undo(o);
    } else if (ctrl && key == 'Y') {
        Redo(o);
    } else if (ctrl && key == 'S') {
        Finish(o);
    } else if (ctrl && key == 'C') {
        FinishSession(Finishing::CopyOnly);
    } else if (ctrl && key == 'A') {
        SetSelection(o, ClientRect(o));
    }
}

void OnChar(Overlay& o, wchar_t ch) {
    if (o.typing < 0) return;
    if (ch != L'\b' && (ch < L' ' || ch == 127)) return;  // Enter, Esc, Ctrl+letters
    TypeChar(o, ch);
}

void CopyText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        std::memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    }
    CloseClipboard();
}

constexpr UINT_PTR kFlashTimer = 77;

// Eyedropper: the colour under the cursor goes to the clipboard as #RRGGBB and becomes the drawing colour.
void PickColor(Overlay& o, POINT p) {
    if (p.x < 0 || p.y < 0 || p.x >= o.shot.Width() || p.y >= o.shot.Height()) return;
    const uint32_t px = o.shot.pixels[static_cast<size_t>(p.y) * o.shot.Width() + p.x];
    const COLORREF c = RGB((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF);
    wchar_t hex[8];
    swprintf(hex, 8, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    CopyText(o.hwnd, hex);
    {
        ChromeChange change(o);
        g_settings.customColor = c;
        g_settings.hasCustom = true;
        wchar_t text[64];
        swprintf(text, 64, T(Str::ColorCopied), hex);
        o.flash = text;
        const int gap = static_cast<int>(18 * o.scale);
        o.flashBox = TextBox(o, text, RECT{p.x + gap, p.y - gap, p.x + gap, p.y - gap}, false);
    }
    SetColorValue(o, kColorCount, c);
    SetTimer(o.hwnd, kFlashTimer, 1500, nullptr);
}

// Numbered marker: the next number after the largest one already placed.
void PlaceStep(Overlay& o, POINT p) {
    int number = 0;
    for (const Shape& s : o.shapes) {
        if (s.tool == Tool::Step) number = std::max(number, s.number);
    }
    Shape s;
    s.tool = Tool::Step;
    s.color = o.color;
    s.size = o.size;
    s.points = {ToPt(p)};
    s.number = number + 1;
    ChromeChange change(o);  // undo button
    Checkpoint(o);
    o.shapes.push_back(s);
    InvalidateShape(o, s);
}

void OnMouseDown(Overlay& o, POINT p, bool ctrl) {
    if (o.picking) return;
    const int id = PanelHit(o, p);
    if (id != toolbar::kNone) {
        OnPanelClick(o, id);
        return;
    }
    SetFocus(o.hwnd);
    if (ctrl && StartAreaWithCtrl(o, p)) return;
    if (o.tool == Tool::Picker) {
        PickColor(o, p);  // works anywhere on the screen
        return;
    }
    if (o.tool != Tool::Area && o.tool != Tool::Select && !InAnyArea(o, p)) {
        CommitText(o);  // drawing outside the selection would not be seen or saved
        return;
    }
    switch (o.tool) {
        case Tool::Area:
            CommitText(o);
            AreaMouseDown(o, p);
            break;
        case Tool::Select:
            PickMouseDown(o, p);
            break;
        case Tool::Text:
            StartText(o, p);
            break;
        case Tool::Eraser:
            CommitText(o);
            StartErase(o, p);
            break;
        case Tool::Step:
            CommitText(o);
            PlaceStep(o, p);
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
        const bool busy = o.drawing || o.erasing || o.drag != Drag::None || o.pickDrag != Drag::None;
        o.hover = busy ? toolbar::kNone : PanelHit(o, p);
        // In area mode the window under the cursor is framed: a click selects it.
        RECT window = {};
        if (o.tool == Tool::Area && !busy && !o.picking && o.hover == toolbar::kNone && HitTest(o, p) == Outside &&
            KeptAreaAt(o, p) < 0) {
            window = WindowAt(o, p);
        }
        o.hoverWindow = window;
    }
    if (o.drawing) {
        ContinueStroke(o, p);
    } else if (o.erasing) {
        EraseAt(o, p);
    } else if (o.pickDrag != Drag::None) {
        PickMouseMove(o, p);
    } else if (o.drag != Drag::None) {
        AreaMouseMove(o, p);
    }
}

void OnMouseUp(Overlay& o) {
    if (o.drawing) {
        EndStroke(o);
    } else if (o.erasing) {
        EndErase(o);
    } else if (o.pickDrag != Drag::None) {
        PickMouseUp(o);
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
            OnMouseDown(*o, p, (wp & MK_CONTROL) != 0);
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
                OnMouseDown(*o, p, (wp & MK_CONTROL) != 0);  // in drawing tools a fast second click is just a click
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
            o->pickDrag = Drag::None;
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
        case WM_TIMER:
            if (wp == kFlashTimer) {
                KillTimer(hwnd, kFlashTimer);
                ChromeChange change(*o);
                o->flash.clear();
                o->flashBox = RECT{};
            }
            return 0;
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
    o->accent = toolbar::AccentColor();
    if (!CaptureMonitor(monitor, o->shot) || !MakeDimmedCopy(o->shot, o->dim)) {
        FreeOverlay(o);
        return false;
    }
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    o->scale = static_cast<float>(dpiX) / 96.f;
    o->panelScale = o->scale * kPanelSize;
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
    POINT cursor;
    if (GetCursorPos(&cursor)) {
        o->mouse = POINT{cursor.x - b.left, cursor.y - b.top};  // the magnifier starts at the cursor
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
        FinishSession();
    }
}

bool IsOverlayOpen() {
    return !g_overlays.empty();
}

bool IsOverlayTyping() {
    if (g_dialogOpen) return true;  // the colour or font window needs Esc and Enter itself
    for (const Overlay* o : g_overlays) {
        if (o->typing >= 0) return true;
    }
    return false;
}

void OpenOverlay(HINSTANCE instance) {
    if (IsOverlayOpen()) {
        return;
    }
    g_pieces.clear();
    CollectWindows();  // before our own windows appear
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
