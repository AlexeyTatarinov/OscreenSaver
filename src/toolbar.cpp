#include "toolbar.h"

#include <algorithm>
#include <cmath>
using std::max;
using std::min;
#include <objidl.h>
#include <gdiplus.h>

#include "i18n.h"

namespace G = Gdiplus;

namespace toolbar {
namespace {

constexpr float kButton = 32.f;
constexpr float kGap = 9.f;
constexpr float kPad = 4.f;

bool Visible(int id, const State& st) {
    if (id >= kBold && id <= kFont) return st.tool == Tool::Text;
    if (id == kEraseWhole || id == kErasePart) return st.tool == Tool::Eraser;
    return true;
}

bool GroupEndsAfter(int id) {
    return id == kFirstColor - 1 || id == kCustomColor || id == kEraseWhole - 1 || id == kErasePart ||
           id == kFont || id == kRedo;
}

int Px(float v, float scale) { return static_cast<int>(std::lround(v * scale)); }

// Left offset of a button inside the panel, in unscaled points. Hidden buttons take no room.
float Offset(int id, const State& st) {
    float x = kPad;
    for (int i = 0; i < id; ++i) {
        if (!Visible(i, st)) continue;
        x += kButton;
        if (GroupEndsAfter(i)) x += kGap;
    }
    return x;
}

int PanelWidth(float scale, const State& st) {
    return Px(Offset(kButtonCount, st) - kGap + kPad, scale);
}

int PanelHeight(float scale) { return Px(kButton + kPad * 2, scale); }

G::Color Argb(COLORREF c, BYTE a = 255) { return G::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

// --- Icons: simple vector drawings, so they stay sharp at any screen scale. ---

void IconArea(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 1.5f * s);
    pen.SetDashStyle(G::DashStyleDash);
    g.DrawRectangle(&pen, r.X + 8 * s, r.Y + 9 * s, r.Width - 16 * s, r.Height - 18 * s);
}

void IconPencil(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 2.f * s);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    const float x0 = r.X + 9 * s, y0 = r.GetBottom() - 9 * s, x1 = r.GetRight() - 9 * s, y1 = r.Y + 9 * s;
    G::PointF pts[] = {{x0, y0}, {x0 + 3 * s, y0 - 7 * s}, {x1 - 2 * s, y1 - 1 * s}, {x1 + 1 * s, y1 + 2 * s},
                       {x0 + 7 * s, y0 - 3 * s}, {x0, y0}};
    g.DrawPolygon(&pen, pts, 6);
}

void IconStroke(G::Graphics& g, const G::RectF& r, const G::Color& color, float width, float s) {
    G::Pen pen(color, width);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawLine(&pen, r.X + 10 * s, r.GetBottom() - 11 * s, r.GetRight() - 10 * s, r.Y + 11 * s);
}

void IconArrow(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 2.f * s);
    G::AdjustableArrowCap cap(3.5f, 3.5f, TRUE);
    pen.SetCustomEndCap(&cap);
    g.DrawLine(&pen, r.X + 9 * s, r.GetBottom() - 9 * s, r.GetRight() - 9 * s, r.Y + 9 * s);
}

void IconText(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 2.f * s);
    const float cx = r.X + r.Width / 2;
    g.DrawLine(&pen, r.X + 10 * s, r.Y + 9 * s, r.GetRight() - 10 * s, r.Y + 9 * s);
    g.DrawLine(&pen, cx, r.Y + 9 * s, cx, r.GetBottom() - 8 * s);
}

void IconEraser(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 1.5f * s);
    pen.SetLineJoin(G::LineJoinRound);
    const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
    G::PointF pts[] = {{cx - 9 * s, cy + 3 * s}, {cx + 1 * s, cy - 7 * s}, {cx + 9 * s, cy + 1 * s},
                       {cx + 1 * s, cy + 9 * s}};
    g.DrawPolygon(&pen, pts, 4);
    G::SolidBrush brush(fg);
    G::PointF half[] = {{cx - 9 * s, cy + 3 * s}, {cx - 4 * s, cy - 2 * s}, {cx + 4 * s, cy + 6 * s},
                        {cx + 1 * s, cy + 9 * s}};
    g.FillPolygon(&brush, half, 4);
}

void IconUndo(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s, bool redo) {
    // Half circle over the top with an arrow at the end: undo points left, redo points right.
    G::Pen pen(fg, 2.f * s);
    G::AdjustableArrowCap cap(2.6f, 2.6f, TRUE);
    pen.SetCustomEndCap(&cap);
    pen.SetStartCap(G::LineCapRound);
    const float d = 14 * s;
    const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2 + 3 * s;
    G::GraphicsPath path;
    if (redo) path.AddArc(cx - d / 2, cy - d / 2, d, d, 180.f, 180.f);
    else path.AddArc(cx - d / 2, cy - d / 2, d, d, 0.f, -180.f);
    g.DrawPath(&pen, &path);
}

void IconSave(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 2.f * s);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    pen.SetLineJoin(G::LineJoinRound);
    const float cx = r.X + r.Width / 2;
    G::PointF tick[] = {{r.X + 9 * s, r.Y + r.Height / 2}, {cx - 2 * s, r.GetBottom() - 10 * s},
                        {r.GetRight() - 9 * s, r.Y + 10 * s}};
    g.DrawLines(&pen, tick, 3);
}

void IconClose(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 2.f * s);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawLine(&pen, r.X + 10 * s, r.Y + 10 * s, r.GetRight() - 10 * s, r.GetBottom() - 10 * s);
    g.DrawLine(&pen, r.GetRight() - 10 * s, r.Y + 10 * s, r.X + 10 * s, r.GetBottom() - 10 * s);
}

// Mouse pointer: pick, move and resize drawn objects.
void IconSelect(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    const float x = r.X + 11 * s, y = r.Y + 7 * s;
    G::PointF pts[] = {{x, y}, {x, y + 17 * s}, {x + 4.5f * s, y + 13 * s}, {x + 7.5f * s, y + 19 * s},
                       {x + 10 * s, y + 18 * s}, {x + 7 * s, y + 12 * s}, {x + 12 * s, y + 12 * s}};
    G::SolidBrush brush(fg);
    g.FillPolygon(&brush, pts, 7);
}

// Whole objects: a figure with a cross over it.
void IconEraseWhole(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 1.5f * s);
    g.DrawEllipse(&pen, r.X + 8 * s, r.Y + 8 * s, r.Width - 16 * s, r.Height - 16 * s);
    G::Pen cross(fg, 2.f * s);
    cross.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    g.DrawLine(&cross, r.X + 7 * s, r.Y + 7 * s, r.GetRight() - 7 * s, r.GetBottom() - 7 * s);
}

// Part of a line rubbed out: a broken line with the eraser circle in the gap.
void IconErasePart(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s) {
    G::Pen pen(fg, 3.f * s);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    const float cy = r.Y + r.Height / 2, cx = r.X + r.Width / 2;
    g.DrawLine(&pen, r.X + 6 * s, cy, cx - 6 * s, cy);
    g.DrawLine(&pen, cx + 6 * s, cy, r.GetRight() - 6 * s, cy);
    G::Pen ring(fg, 1.2f * s);
    g.DrawEllipse(&ring, cx - 5 * s, cy - 5 * s, 10 * s, 10 * s);
}

// B, I, S and Aa for the text buttons, drawn with a real font.
void IconLetters(G::Graphics& g, const G::RectF& r, const G::Color& fg, float s, int id) {
    G::FontFamily segoe(L"Segoe UI");
    const G::FontFamily* family = segoe.IsAvailable() ? &segoe : G::FontFamily::GenericSansSerif();
    INT style = G::FontStyleRegular;
    const wchar_t* text = L"Aa";
    if (id == kBold) { style = G::FontStyleBold; text = L"B"; }
    if (id == kItalic) { style = G::FontStyleItalic | G::FontStyleBold; text = L"I"; }
    if (id == kStrike) { style = G::FontStyleStrikeout; text = L"S"; }
    if (!family->IsStyleAvailable(style)) style = G::FontStyleRegular;
    G::Font font(family, 15.f * s, style, G::UnitPixel);
    G::StringFormat fmt;
    fmt.SetAlignment(G::StringAlignmentCenter);
    fmt.SetLineAlignment(G::StringAlignmentCenter);
    G::SolidBrush brush(fg);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
    g.DrawString(text, -1, &font, r, &fmt, &brush);
}

}  // namespace

RECT Place(const RECT& sel, const RECT& screen, float scale, const State& st) {
    const int w = PanelWidth(scale, st);
    const int h = PanelHeight(scale);
    const int gap = Px(6, scale);
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
    const int top = panel.top + Px(kPad, scale);
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

    const COLORREF backRgb = st.dark ? RGB(43, 43, 43) : RGB(249, 249, 249);
    const COLORREF fgRgb = st.dark ? RGB(255, 255, 255) : RGB(26, 26, 26);
    const COLORREF accent = RGB(0, 120, 215);
    const G::Color fg = Argb(fgRgb);
    const G::Color fgDisabled = Argb(fgRgb, 70);

    // Background with rounded corners and a thin border.
    {
        const float rad = 6 * s;
        const G::RectF b(static_cast<float>(panel.left), static_cast<float>(panel.top),
                         static_cast<float>(panel.right - panel.left - 1),
                         static_cast<float>(panel.bottom - panel.top - 1));
        G::GraphicsPath path;
        path.AddArc(b.X, b.Y, rad * 2, rad * 2, 180, 90);
        path.AddArc(b.GetRight() - rad * 2, b.Y, rad * 2, rad * 2, 270, 90);
        path.AddArc(b.GetRight() - rad * 2, b.GetBottom() - rad * 2, rad * 2, rad * 2, 0, 90);
        path.AddArc(b.X, b.GetBottom() - rad * 2, rad * 2, rad * 2, 90, 90);
        path.CloseFigure();
        G::SolidBrush back(Argb(backRgb));
        g.FillPath(&back, &path);
        G::Pen border(st.dark ? G::Color(255, 70, 70, 70) : G::Color(255, 210, 210, 210), 1.f);
        g.DrawPath(&border, &path);
    }

    for (int id = 0; id < kButtonCount; ++id) {
        if (!Visible(id, st)) continue;
        const RECT br = ButtonRect(panel, id, s, st);
        const G::RectF r(static_cast<float>(br.left), static_cast<float>(br.top),
                         static_cast<float>(br.right - br.left), static_cast<float>(br.bottom - br.top));
        const bool active = (id < kFirstColor && id == static_cast<int>(st.tool)) ||
                            (id >= kFirstSize && id < kBold && id - kFirstSize == st.size) ||
                            (id == kBold && st.bold) || (id == kItalic && st.italic) ||
                            (id == kStrike && st.strike) || (id == kEraseWhole && !st.eraseParts) ||
                            (id == kErasePart && st.eraseParts);
        if (active || id == st.hover) {
            G::SolidBrush hl(active ? Argb(accent, st.dark ? 150 : 60) : Argb(fgRgb, 30));
            g.FillRectangle(&hl, r.X + 2 * s, r.Y + 2 * s, r.Width - 4 * s, r.Height - 4 * s);
        }
        const G::Color& color = ((id == kUndo && !st.canUndo) || (id == kRedo && !st.canRedo)) ? fgDisabled : fg;

        if (id >= kFirstColor && id <= kCustomColor) {
            const int c = id - kFirstColor;
            const float d = 16 * s;
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
            }
            G::Pen edge(Argb(fgRgb, 90), 1.f);
            g.DrawEllipse(&edge, x, y, d, d);
            if (c == st.color) {
                G::Pen ring(fg, 2.f * s);
                g.DrawEllipse(&ring, x - 4 * s, y - 4 * s, d + 8 * s, d + 8 * s);
            }
            continue;
        }
        if (id >= kFirstSize && id < kEraseWhole) {
            static const float kDots[kSizeCount] = {4.f, 8.f, 13.f};
            const float d = kDots[id - kFirstSize] * s;
            G::SolidBrush dot(fg);
            g.FillEllipse(&dot, r.X + (r.Width - d) / 2, r.Y + (r.Height - d) / 2, d, d);
            continue;
        }
        if (id >= kBold && id <= kFont) {
            IconLetters(g, r, color, s, id);
            continue;
        }
        switch (id) {
            case static_cast<int>(Tool::Area): IconArea(g, r, color, s); break;
            case static_cast<int>(Tool::Select): IconSelect(g, r, color, s); break;
            case kEraseWhole: IconEraseWhole(g, r, color, s); break;
            case kErasePart: IconErasePart(g, r, color, s); break;
            case static_cast<int>(Tool::Pencil): IconPencil(g, r, color, s); break;
            case static_cast<int>(Tool::Marker): IconStroke(g, r, Argb(RGB(255, 200, 0), 200), 7 * s, s); break;
            case static_cast<int>(Tool::Brush): IconStroke(g, r, color, 6 * s, s); break;
            case static_cast<int>(Tool::Arrow): IconArrow(g, r, color, s); break;
            case static_cast<int>(Tool::Rect): {
                G::Pen pen(color, 2.f * s);
                g.DrawRectangle(&pen, r.X + 8 * s, r.Y + 10 * s, r.Width - 16 * s, r.Height - 20 * s);
                break;
            }
            case static_cast<int>(Tool::Ellipse): {
                G::Pen pen(color, 2.f * s);
                g.DrawEllipse(&pen, r.X + 7 * s, r.Y + 10 * s, r.Width - 14 * s, r.Height - 20 * s);
                break;
            }
            case static_cast<int>(Tool::Text): IconText(g, r, color, s); break;
            case static_cast<int>(Tool::Eraser): IconEraser(g, r, color, s); break;
            case kUndo: IconUndo(g, r, color, s, false); break;
            case kRedo: IconUndo(g, r, color, s, true); break;
            case kSave: IconSave(g, r, Argb(RGB(16, 176, 64)), s); break;
            case kClose: IconClose(g, r, color, s); break;
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
        case static_cast<int>(Tool::Text): return T(Str::TipText);
        case static_cast<int>(Tool::Eraser): return T(Str::TipEraser);
        case kUndo: return T(Str::TipUndo);
        case kRedo: return T(Str::TipRedo);
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

}  // namespace toolbar
