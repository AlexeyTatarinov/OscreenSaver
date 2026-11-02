#include "toolbar.h"

#include <algorithm>
#include <cmath>
#include <memory>
using std::max;
using std::min;
#include <objidl.h>
#include <gdiplus.h>

#include "i18n.h"

namespace G = Gdiplus;

namespace toolbar {
namespace {

// Sizes in points (multiplied by the panel scale).
constexpr float kButton = 32.f;
constexpr float kGap = 9.f;      // between groups, with a thin divider in the middle
constexpr float kPad = 4.f;      // inside the panel around the buttons
constexpr float kMargin = 4.f;   // room around the panel for its soft shadow
constexpr float kRadius = 8.f;   // panel corners (Windows 11 style)
constexpr float kIcon = 16.f;    // icon size

bool Visible(int id, const State& st) {
    if (id >= kBold && id <= kFont) return st.tool == Tool::Text;
    if (id == kEraseWhole || id == kErasePart) return st.tool == Tool::Eraser;
    if (id == kFrames) return st.compare;
    return true;
}

bool GroupEndsAfter(int id) {
    return id == kFirstColor - 1 || id == kCustomColor || id == kEraseWhole - 1 || id == kErasePart ||
           id == kFont || id == kRedo;
}

int Px(float v, float scale) { return static_cast<int>(std::lround(v * scale)); }

// Left offset of a button inside the panel, in points. Hidden buttons take no room.
float Offset(int id, const State& st) {
    float x = kMargin + kPad;
    for (int i = 0; i < id; ++i) {
        if (!Visible(i, st)) continue;
        x += kButton;
        if (GroupEndsAfter(i)) x += kGap;
    }
    return x;
}

int PanelWidth(float scale, const State& st) {
    return Px(Offset(kButtonCount, st) - kGap + kPad + kMargin, scale);
}

int PanelHeight(float scale) { return Px(kButton + (kPad + kMargin) * 2, scale); }

G::Color Argb(COLORREF c, BYTE a = 255) { return G::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

void RoundedPath(G::GraphicsPath& path, const G::RectF& b, float rad) {
    const float d = std::min({rad * 2, b.Width, b.Height});
    path.AddArc(b.X, b.Y, d, d, 180, 90);
    path.AddArc(b.GetRight() - d, b.Y, d, d, 270, 90);
    path.AddArc(b.GetRight() - d, b.GetBottom() - d, d, d, 0, 90);
    path.AddArc(b.X, b.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRounded(G::Graphics& g, const G::RectF& b, float rad, const G::Color& color) {
    G::GraphicsPath path;
    RoundedPath(path, b, rad);
    G::SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

// ---------- Windows icon font ----------
// Windows 11 has "Segoe Fluent Icons", Windows 10 "Segoe MDL2 Assets": the icons Windows itself uses.
// They are sharp at any screen scale. If neither font is present, the icons are drawn as lines.

const wchar_t* IconFontName() {
    static const wchar_t* name = [] () -> const wchar_t* {
        for (const wchar_t* candidate : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
            G::FontFamily family(candidate);
            if (family.IsAvailable()) return candidate;
        }
        return nullptr;
    }();
    return name;
}

wchar_t GlyphFor(int id) {
    switch (id) {
        case static_cast<int>(Tool::Area): return 0xE7A8;    // Crop
        case static_cast<int>(Tool::Pencil): return 0xE70F;  // Edit
        case static_cast<int>(Tool::Marker): return 0xE7E6;  // Highlight
        case static_cast<int>(Tool::Brush): return 0xE771;   // Personalize (brush)
        case static_cast<int>(Tool::Rect): return 0xE739;    // Checkbox (empty square)
        case static_cast<int>(Tool::Text): return 0xE8D2;    // Font
        case static_cast<int>(Tool::Eraser): return 0xE75C;  // EraseTool
        case kUndo: return 0xE7A7;
        case kRedo: return 0xE7A6;
        case kSave: return 0xE8FB;   // Accept
        case kClose: return 0xE711;  // Cancel
        case kBold: return 0xE8DD;
        case kItalic: return 0xE8DB;
        default: return 0;
    }
}

bool DrawGlyph(G::Graphics& g, const G::RectF& r, wchar_t glyph, const G::Color& color, float s) {
    const wchar_t* fontName = IconFontName();
    if (!fontName || !glyph) return false;
    G::FontFamily family(fontName);
    G::Font font(&family, kIcon * s, G::FontStyleRegular, G::UnitPixel);
    G::StringFormat fmt;
    fmt.SetAlignment(G::StringAlignmentCenter);
    fmt.SetLineAlignment(G::StringAlignmentCenter);
    G::SolidBrush brush(color);
    const wchar_t text[2] = {glyph, 0};
    g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
    g.DrawString(text, 1, &font, r, &fmt, &brush);
    return true;
}

// ---------- Line icons: for buttons without a Windows icon, and as a fallback ----------
// All drawn in a 16-point square in the middle of the button, with thin round lines.

struct Box {
    float l, t, size;
    float X(float v) const { return l + v * size / 16.f; }
    float Y(float v) const { return t + v * size / 16.f; }
};

Box IconBox(const G::RectF& r, float s) {
    const float size = kIcon * s;
    return Box{r.X + (r.Width - size) / 2, r.Y + (r.Height - size) / 2, size};
}

std::unique_ptr<G::Pen> LinePen(const G::Color& c, float s, float width = 1.25f) {
    auto pen = std::make_unique<G::Pen>(c, width * s);
    pen->SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    pen->SetLineJoin(G::LineJoinRound);
    return pen;
}

void IconSelect(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    G::PointF pts[] = {{b.X(4), b.Y(1.5f)}, {b.X(4), b.Y(13.5f)}, {b.X(7), b.Y(10.6f)}, {b.X(9.2f), b.Y(15)},
                       {b.X(11), b.Y(14.2f)}, {b.X(8.9f), b.Y(9.9f)}, {b.X(12.8f), b.Y(9.9f)}};
    auto pen = LinePen(fg, s);
    g.DrawPolygon(pen.get(), pts, 7);
}

void IconArrow(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto pen = LinePen(fg, s);
    g.DrawLine(pen.get(), b.X(2.5f), b.Y(13.5f), b.X(13.5f), b.Y(2.5f));
    g.DrawLine(pen.get(), b.X(13.5f), b.Y(2.5f), b.X(7.5f), b.Y(2.5f));
    g.DrawLine(pen.get(), b.X(13.5f), b.Y(2.5f), b.X(13.5f), b.Y(8.5f));
}

void IconEllipse(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto pen = LinePen(fg, s);
    g.DrawEllipse(pen.get(), b.X(1.5f), b.Y(3.5f), b.size * 13 / 16, b.size * 9 / 16);
}

void IconBlur(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto pen = LinePen(fg, s);
    G::GraphicsPath path;
    RoundedPath(path, G::RectF(b.X(1.5f), b.Y(2.5f), b.size * 13 / 16, b.size * 11 / 16), 2 * s);
    g.DrawPath(pen.get(), &path);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            const BYTE alpha = static_cast<BYTE>(((i + j) % 2) ? 230 : 110);
            G::SolidBrush dot(G::Color(alpha, fg.GetR(), fg.GetG(), fg.GetB()));
            const float d = 2.6f * s;
            g.FillEllipse(&dot, b.X(4.5f + i * 3.5f) - d / 2, b.Y(6.f + j * 4.f) - d / 2, d, d);
        }
    }
}

void IconAddArea(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto pen = LinePen(fg, s);
    g.DrawRectangle(pen.get(), b.X(0.5f), b.Y(5), b.size * 6 / 16, b.size * 9 / 16);
    g.DrawRectangle(pen.get(), b.X(7.5f), b.Y(5), b.size * 6 / 16, b.size * 9 / 16);
    g.DrawLine(pen.get(), b.X(11.5f), b.Y(0.5f), b.X(15.5f), b.Y(0.5f));
    g.DrawLine(pen.get(), b.X(13.5f), b.Y(-1.5f), b.X(13.5f), b.Y(2.5f));
}

void IconFrames(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto pen = LinePen(fg, s);
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            g.DrawRectangle(pen.get(), b.X(1.5f + i * 7), b.Y(1.5f + j * 7), b.size * 6 / 16, b.size * 6 / 16);
        }
    }
}

void IconEraseWhole(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto pen = LinePen(fg, s);
    g.DrawEllipse(pen.get(), b.X(3), b.Y(3), b.size * 10 / 16, b.size * 10 / 16);
    g.DrawLine(pen.get(), b.X(1.5f), b.Y(1.5f), b.X(14.5f), b.Y(14.5f));
}

void IconErasePart(G::Graphics& g, const Box& b, const G::Color& fg, float s) {
    auto thick = LinePen(fg, s, 2.f);
    g.DrawLine(thick.get(), b.X(1), b.Y(8), b.X(4.5f), b.Y(8));
    g.DrawLine(thick.get(), b.X(11.5f), b.Y(8), b.X(15), b.Y(8));
    auto pen = LinePen(fg, s);
    g.DrawEllipse(pen.get(), b.X(5), b.Y(5), b.size * 6 / 16, b.size * 6 / 16);
}

// Fallback drawings for buttons that normally use the Windows icon font.
void IconFallback(G::Graphics& g, const Box& b, const G::Color& fg, float s, int id) {
    auto pen = LinePen(fg, s);
    switch (id) {
        case static_cast<int>(Tool::Area): {
            auto dash = LinePen(fg, s);
            dash->SetDashStyle(G::DashStyleDash);
            g.DrawRectangle(dash.get(), b.X(1.5f), b.Y(2.5f), b.size * 13 / 16, b.size * 11 / 16);
            break;
        }
        case static_cast<int>(Tool::Pencil): {
            G::PointF pts[] = {{b.X(2), b.Y(14)}, {b.X(3), b.Y(10.5f)}, {b.X(11.5f), b.Y(2)}, {b.X(14), b.Y(4.5f)},
                               {b.X(5.5f), b.Y(13)}, {b.X(2), b.Y(14)}};
            g.DrawLines(pen.get(), pts, 6);
            break;
        }
        case static_cast<int>(Tool::Marker):
        case static_cast<int>(Tool::Brush): {
            auto wide = LinePen(fg, s, id == static_cast<int>(Tool::Marker) ? 4.f : 3.f);
            g.DrawLine(wide.get(), b.X(3), b.Y(13), b.X(13), b.Y(3));
            break;
        }
        case static_cast<int>(Tool::Rect):
            g.DrawRectangle(pen.get(), b.X(1.5f), b.Y(3.5f), b.size * 13 / 16, b.size * 9 / 16);
            break;
        case static_cast<int>(Tool::Text):
            g.DrawLine(pen.get(), b.X(3), b.Y(2.5f), b.X(13), b.Y(2.5f));
            g.DrawLine(pen.get(), b.X(8), b.Y(2.5f), b.X(8), b.Y(14));
            break;
        case static_cast<int>(Tool::Eraser): {
            G::PointF pts[] = {{b.X(1.5f), b.Y(10)}, {b.X(8), b.Y(3.5f)}, {b.X(14.5f), b.Y(10)}, {b.X(10.5f), b.Y(14)},
                               {b.X(5.5f), b.Y(14)}};
            g.DrawPolygon(pen.get(), pts, 5);
            break;
        }
        case kUndo:
        case kRedo: {
            const bool redo = id == kRedo;
            G::GraphicsPath path;
            path.AddArc(b.X(2.5f), b.Y(4), b.size * 11 / 16, b.size * 9 / 16, redo ? 180.f : 0.f, redo ? 180.f : -180.f);
            G::AdjustableArrowCap cap(3.f, 3.f, FALSE);
            pen->SetCustomEndCap(&cap);
            g.DrawPath(pen.get(), &path);
            break;
        }
        case kSave: {
            G::PointF pts[] = {{b.X(2), b.Y(8.5f)}, {b.X(6), b.Y(12.5f)}, {b.X(14), b.Y(3.5f)}};
            g.DrawLines(pen.get(), pts, 3);
            break;
        }
        case kClose:
            g.DrawLine(pen.get(), b.X(3), b.Y(3), b.X(13), b.Y(13));
            g.DrawLine(pen.get(), b.X(13), b.Y(3), b.X(3), b.Y(13));
            break;
    }
}

// B, I, S and Aa, written with the Windows interface font.
void IconLetters(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s, int id) {
    G::FontFamily segoe(L"Segoe UI");
    const G::FontFamily* family = segoe.IsAvailable() ? &segoe : G::FontFamily::GenericSansSerif();
    INT style = G::FontStyleRegular;
    const wchar_t* text = L"Aa";
    if (id == kBold) { style = G::FontStyleBold; text = L"B"; }
    if (id == kItalic) { style = G::FontStyleItalic; text = L"I"; }
    if (id == kStrike) { text = L"S"; }
    if (!family->IsStyleAvailable(style)) style = G::FontStyleRegular;
    G::Font font(family, 14.f * s, style, G::UnitPixel);
    G::StringFormat fmt;
    fmt.SetAlignment(G::StringAlignmentCenter);
    fmt.SetLineAlignment(G::StringAlignmentCenter);
    G::SolidBrush brush(fg);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
    g.DrawString(text, -1, &font, r, &fmt, &brush);
    if (id == kStrike) {
        auto pen = LinePen(fg, s, 1.f);
        const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2 + 0.5f * s;
        g.DrawLine(pen.get(), cx - 6 * s, cy, cx + 6 * s, cy);
    }
}

}  // namespace

RECT Place(const RECT& sel, const RECT& screen, float scale, const State& st) {
    const int w = PanelWidth(scale, st);
    const int h = PanelHeight(scale);
    const int gap = Px(2, scale);  // the shadow margin adds the rest of the distance
    int left = std::max<int>(sel.right - w, screen.left);  // aligned to the right edge of the selection
    left = std::min<int>(left, screen.right - w);
    left = std::max<int>(left, screen.left);
    int top;
    if (sel.bottom + gap + h <= screen.bottom) {
        top = sel.bottom + gap;  // below
    } else if (sel.top - gap - h >= screen.top) {
        top = sel.top - gap - h;  // above
    } else {
        top = sel.bottom - gap - h;  // inside, at the bottom
    }
    return RECT{left, top, left + w, top + h};
}

RECT ButtonRect(const RECT& panel, int id, float scale, const State& st) {
    const int left = panel.left + Px(Offset(id, st), scale);
    const int top = panel.top + Px(kMargin + kPad, scale);
    return RECT{left, top, left + Px(kButton, scale), top + Px(kButton, scale)};
}

int HitTest(const RECT& panel, POINT p, float scale, const State& st) {
    if (!PtInRect(&panel, p)) return kNone;
    for (int id = 0; id < kButtonCount; ++id) {
        if (!Visible(id, st)) continue;
        const RECT r = ButtonRect(panel, id, scale, st);
        if (PtInRect(&r, p)) return id;
    }
    return kNone;
}

void Draw(HDC dc, const RECT& panel, const State& st, float s) {
    G::Graphics g(dc);
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(G::PixelOffsetModeHalf);

    const COLORREF backRgb = st.dark ? RGB(44, 44, 44) : RGB(249, 249, 249);
    const COLORREF borderRgb = st.dark ? RGB(70, 70, 70) : RGB(224, 224, 224);
    const COLORREF fgRgb = st.dark ? RGB(255, 255, 255) : RGB(28, 28, 28);
    const COLORREF accent = AccentColor();
    const G::Color fg = Argb(fgRgb);
    const G::Color fgDisabled = Argb(fgRgb, 80);

    // Panel: soft shadow, rounded body, thin border.
    const float m = kMargin * s;
    const G::RectF body(panel.left + m, panel.top + m, panel.right - panel.left - 2 * m,
                        panel.bottom - panel.top - 2 * m);
    for (int i = 3; i >= 1; --i) {
        G::RectF sh = body;
        sh.Inflate(i * s, i * s);
        sh.Offset(0, 1.5f * s);
        FillRounded(g, sh, kRadius * s + i * s, G::Color(static_cast<BYTE>(st.dark ? 34 : 14), 0, 0, 0));
    }
    FillRounded(g, body, kRadius * s, Argb(backRgb));
    {
        G::GraphicsPath path;
        G::RectF edge = body;
        edge.Inflate(-0.5f, -0.5f);
        RoundedPath(path, edge, kRadius * s);
        G::Pen border(Argb(borderRgb), 1.f);
        g.DrawPath(&border, &path);
    }

    for (int id = 0; id < kButtonCount; ++id) {
        if (!Visible(id, st)) continue;
        const RECT br = ButtonRect(panel, id, s, st);
        const G::RectF r(static_cast<float>(br.left), static_cast<float>(br.top),
                         static_cast<float>(br.right - br.left), static_cast<float>(br.bottom - br.top));

        // Thin divider after each group.
        if (GroupEndsAfter(id)) {
            int next = id + 1;
            while (next < kButtonCount && !Visible(next, st)) ++next;
            if (next < kButtonCount) {
                const float x = r.GetRight() + kGap * s / 2;
                G::Pen divider(Argb(fgRgb, st.dark ? 40 : 30), 1.f);
                g.DrawLine(&divider, x, r.Y + 7 * s, x, r.GetBottom() - 7 * s);
            }
        }

        const bool isTool = id < kFirstColor;
        const bool active = (isTool && id == static_cast<int>(st.tool)) ||
                            (id >= kFirstSize && id < kEraseWhole && id - kFirstSize == st.size) ||
                            (id == kBold && st.bold) || (id == kItalic && st.italic) ||
                            (id == kStrike && st.strike) || (id == kEraseWhole && !st.eraseParts) ||
                            (id == kErasePart && st.eraseParts) || (id == kFrames && st.frames);
        const bool disabled = (id == kUndo && !st.canUndo) || (id == kRedo && !st.canRedo) ||
                              (id == kAddArea && !st.canAddArea);
        G::RectF cell = r;
        cell.Inflate(-2 * s, -2 * s);
        if (active) {
            FillRounded(g, cell, 4 * s, st.dark ? G::Color(26, 255, 255, 255) : G::Color(20, 0, 0, 0));
            if (isTool) {
                // Windows-style accent pill under the chosen tool.
                const float pw = 10 * s, ph = 3 * s;
                FillRounded(g, G::RectF(cell.X + (cell.Width - pw) / 2, cell.GetBottom() - ph, pw, ph), ph / 2,
                            Argb(accent));
            }
        } else if (id == st.hover && !disabled) {
            FillRounded(g, cell, 4 * s, st.dark ? G::Color(15, 255, 255, 255) : G::Color(10, 0, 0, 0));
        }
        const G::Color color = disabled ? fgDisabled : fg;

        if (id >= kFirstColor && id <= kCustomColor) {
            const int c = id - kFirstColor;
            const float d = 14 * s;
            const float x = r.X + (r.Width - d) / 2, y = r.Y + (r.Height - d) / 2;
            if (id == kCustomColor) {
                // Colour wheel: opens the window with all colours. Shows the colour picked there.
                static const COLORREF kWheel[6] = {RGB(232, 17, 35), RGB(255, 200, 0), RGB(16, 176, 64),
                                                   RGB(0, 190, 220), RGB(0, 90, 215), RGB(160, 40, 200)};
                for (int k = 0; k < 6; ++k) {
                    G::SolidBrush part(Argb(kWheel[k]));
                    g.FillPie(&part, x, y, d, d, -90.f + k * 60.f, 60.f);
                }
                if (st.hasCustom) {
                    G::SolidBrush inner(Argb(st.customColor));
                    g.FillEllipse(&inner, x + d / 4, y + d / 4, d / 2, d / 2);
                }
            } else {
                G::SolidBrush swatch(Argb(kPalette[c]));
                g.FillEllipse(&swatch, x, y, d, d);
                G::Pen edge(Argb(fgRgb, 60), 1.f);
                g.DrawEllipse(&edge, x, y, d, d);
            }
            if (c == st.color) {
                G::Pen ring(Argb(accent), 2.f * s);
                g.DrawEllipse(&ring, x - 3.5f * s, y - 3.5f * s, d + 7 * s, d + 7 * s);
            }
            continue;
        }
        if (id >= kFirstSize && id < kEraseWhole) {
            static const float kDots[kSizeCount] = {3.f, 6.f, 10.f};
            const float d = kDots[id - kFirstSize] * s;
            G::SolidBrush dot(fg);
            g.FillEllipse(&dot, r.X + (r.Width - d) / 2, r.Y + (r.Height - d) / 2, d, d);
            continue;
        }
        if (id == kStrike || id == kFont) {
            IconLetters(g, r, color, s, id);
            continue;
        }
        const G::Color iconColor = id == kSave ? Argb(RGB(16, 160, 64)) : color;
        if (DrawGlyph(g, r, GlyphFor(id), iconColor, s)) continue;
        if (id == kBold || id == kItalic) {
            IconLetters(g, r, color, s, id);
            continue;
        }
        const Box b = IconBox(r, s);
        switch (id) {
            case static_cast<int>(Tool::Select): IconSelect(g, b, color, s); break;
            case static_cast<int>(Tool::Arrow): IconArrow(g, b, color, s); break;
            case static_cast<int>(Tool::Ellipse): IconEllipse(g, b, color, s); break;
            case static_cast<int>(Tool::Blur): IconBlur(g, b, color, s); break;
            case kEraseWhole: IconEraseWhole(g, b, color, s); break;
            case kErasePart: IconErasePart(g, b, color, s); break;
            case kAddArea: IconAddArea(g, b, color, s); break;
            case kFrames: IconFrames(g, b, color, s); break;
            default: IconFallback(g, b, iconColor, s, id); break;
        }
    }
}

const wchar_t* Tip(int id) {
    if (id >= kFirstColor && id < kCustomColor) return T(Str::TipColor);
    if (id == kCustomColor) return T(Str::TipCustomColor);
    if (id >= kFirstSize && id < kEraseWhole) return T(Str::TipSize);
    if (id == kEraseWhole) return T(Str::TipEraseWhole);
    if (id == kErasePart) return T(Str::TipErasePart);
    if (id == kBold) return T(Str::TipBold);
    if (id == kItalic) return T(Str::TipItalic);
    if (id == kStrike) return T(Str::TipStrike);
    if (id == kFont) return T(Str::TipFont);
    switch (id) {
        case static_cast<int>(Tool::Area): return T(Str::TipArea);
        case static_cast<int>(Tool::Select): return T(Str::TipSelect);
        case static_cast<int>(Tool::Pencil): return T(Str::TipPencil);
        case static_cast<int>(Tool::Marker): return T(Str::TipMarker);
        case static_cast<int>(Tool::Brush): return T(Str::TipBrush);
        case static_cast<int>(Tool::Arrow): return T(Str::TipArrow);
        case static_cast<int>(Tool::Rect): return T(Str::TipRect);
        case static_cast<int>(Tool::Ellipse): return T(Str::TipEllipse);
        case static_cast<int>(Tool::Blur): return T(Str::TipBlur);
        case static_cast<int>(Tool::Text): return T(Str::TipText);
        case static_cast<int>(Tool::Eraser): return T(Str::TipEraser);
        case kUndo: return T(Str::TipUndo);
        case kRedo: return T(Str::TipRedo);
        case kAddArea: return T(Str::TipAddArea);
        case kFrames: return T(Str::TipFrames);
        case kSave: return T(Str::TipSave);
        case kClose: return T(Str::TipClose);
    }
    return L"";
}

bool SystemUsesDarkTheme() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

COLORREF AccentColor() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    // Stored as 0xAABBGGRR, the same byte order as COLORREF.
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                     nullptr, &value, &size) == ERROR_SUCCESS) {
        return value & 0x00FFFFFF;
    }
    return RGB(0, 120, 215);
}

void DrawLabel(HDC dc, const RECT& box, const wchar_t* text, bool dark, float s) {
    {
        G::Graphics g(dc);
        g.SetSmoothingMode(G::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
        const G::RectF r(static_cast<float>(box.left), static_cast<float>(box.top),
                         static_cast<float>(box.right - box.left), static_cast<float>(box.bottom - box.top));
        FillRounded(g, r, 4 * s, dark ? G::Color(240, 44, 44, 44) : G::Color(245, 249, 249, 249));
        G::GraphicsPath path;
        G::RectF edge = r;
        edge.Inflate(-0.5f, -0.5f);
        RoundedPath(path, edge, 4 * s);
        G::Pen border(dark ? G::Color(255, 70, 70, 70) : G::Color(255, 224, 224, 224), 1.f);
        g.DrawPath(&border, &path);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark ? RGB(255, 255, 255) : RGB(28, 28, 28));
    RECT r = box;
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

}  // namespace toolbar
