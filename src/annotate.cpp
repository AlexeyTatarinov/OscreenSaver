#include "annotate.h"

#include <algorithm>
#include <cmath>
#include <memory>
using std::max;
using std::min;
#include <objidl.h>
#include <gdiplus.h>

namespace G = Gdiplus;

const COLORREF kPalette[kColorCount] = {
    RGB(16, 176, 64),    // green
    RGB(232, 17, 35),    // red
    RGB(255, 255, 255),  // white
    RGB(0, 0, 0),        // black
};

namespace {

ULONG_PTR g_token = 0;

G::Color ToColor(COLORREF c, BYTE alpha = 255) {
    return G::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// ---------- Text ----------

struct TextTools {
    std::unique_ptr<G::FontFamily> family;
    std::unique_ptr<G::Font> font;
    std::unique_ptr<G::StringFormat> format;
};

TextTools MakeTextTools(const TextStyle& st) {
    TextTools t;
    t.family = std::make_unique<G::FontFamily>(st.font.c_str());
    if (!t.family->IsAvailable()) t.family = std::make_unique<G::FontFamily>(L"Segoe UI");
    if (!t.family->IsAvailable()) t.family.reset(G::FontFamily::GenericSansSerif()->Clone());
    INT style = G::FontStyleRegular;
    if (st.bold) style |= G::FontStyleBold;
    if (st.italic) style |= G::FontStyleItalic;
    if (st.strike) style |= G::FontStyleStrikeout;
    if (st.underline) style |= G::FontStyleUnderline;
    // Some fonts lack a bold or italic face; fall back to what the font has.
    if (!t.family->IsStyleAvailable(style)) style &= ~(G::FontStyleBold | G::FontStyleItalic);
    t.font = std::make_unique<G::Font>(t.family.get(), std::max(4.f, st.px), style, G::UnitPixel);
    t.format.reset(G::StringFormat::GenericTypographic()->Clone());
    t.format->SetFormatFlags(t.format->GetFormatFlags() | G::StringFormatFlagsMeasureTrailingSpaces);
    return t;
}

// Size of a text block in pixels (empty text = one empty line).
G::RectF MeasureText(G::Graphics& g, const Shape& s, const TextTools& t) {
    G::RectF box(0, 0, 0, t.font->GetHeight(&g));
    if (!s.text.empty()) {
        g.MeasureString(s.text.c_str(), static_cast<INT>(s.text.size()), t.font.get(), G::PointF(0, 0),
                        t.format.get(), &box);
        // A trailing new line is not counted by MeasureString; add its height.
        if (s.text.back() == L'\n') box.Height += t.font->GetHeight(&g);
    }
    box.X = static_cast<float>(s.points[0].x);
    box.Y = static_cast<float>(s.points[0].y);
    return box;
}

RECT TextRect(const Shape& s) {
    if (s.points.empty()) return RECT{0, 0, 0, 0};
    HDC dc = CreateCompatibleDC(nullptr);
    G::RectF box;
    {
        G::Graphics g(dc);
        g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
        const TextTools t = MakeTextTools(s.style);
        box = MeasureText(g, s, t);
    }
    DeleteDC(dc);
    return RECT{static_cast<LONG>(std::floor(box.X)), static_cast<LONG>(std::floor(box.Y)),
                static_cast<LONG>(std::ceil(box.GetRight())) + 2, static_cast<LONG>(std::ceil(box.GetBottom()))};
}

void DrawTextShape(G::Graphics& g, const Shape& s, POINT off, float scale, bool caret) {
    if (s.points.empty()) return;
    const TextTools t = MakeTextTools(s.style);
    G::SolidBrush brush(ToColor(s.color));
    const G::PointF origin(static_cast<float>(s.points[0].x - off.x), static_cast<float>(s.points[0].y - off.y));
    g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
    if (!s.text.empty()) {
        g.DrawString(s.text.c_str(), static_cast<INT>(s.text.size()), t.font.get(), origin, t.format.get(),
                     &brush);
    }
    if (!caret) return;

    // Cursor after the last character of the last line.
    const size_t lineStart = s.text.find_last_of(L'\n');
    const std::wstring last = lineStart == std::wstring::npos ? s.text : s.text.substr(lineStart + 1);
    const int lineIndex = static_cast<int>(std::count(s.text.begin(), s.text.end(), L'\n'));
    G::RectF box;
    if (!last.empty()) {
        g.MeasureString(last.c_str(), static_cast<INT>(last.size()), t.font.get(), G::PointF(0, 0),
                        t.format.get(), &box);
    }
    const float lineHeight = t.font->GetHeight(&g);
    const float x = origin.X + box.Width + 1;
    const float y = origin.Y + lineHeight * lineIndex;
    G::Pen pen(ToColor(s.color), std::max(1.f, scale));
    g.DrawLine(&pen, x, y, x, y + lineHeight);
}

// ---------- Lines ----------

void DrawPath(G::Graphics& g, const Shape& s, POINT off, float width, const G::Color& color) {
    if (s.points.empty()) return;
    if (s.points.size() == 1) {
        G::SolidBrush brush(color);
        const float x = static_cast<float>(s.points[0].x - off.x);
        const float y = static_cast<float>(s.points[0].y - off.y);
        g.FillEllipse(&brush, x - width / 2, y - width / 2, width, width);
        return;
    }
    std::vector<G::PointF> pts;
    pts.reserve(s.points.size());
    for (const POINT& p : s.points) {
        pts.emplace_back(static_cast<float>(p.x - off.x), static_cast<float>(p.y - off.y));
    }
    G::Pen pen(color, width);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    pen.SetLineJoin(G::LineJoinRound);
    g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
}

double SegmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Outline of a shape as a list of points (closed shapes repeat the first point).
std::vector<G::PointF> Outline(const Shape& s) {
    std::vector<G::PointF> pts;
    if (s.tool == Tool::Rect || s.tool == Tool::Ellipse) {
        if (s.points.size() < 2) return pts;
        const float l = static_cast<float>(std::min(s.points[0].x, s.points[1].x));
        const float r = static_cast<float>(std::max(s.points[0].x, s.points[1].x));
        const float t = static_cast<float>(std::min(s.points[0].y, s.points[1].y));
        const float b = static_cast<float>(std::max(s.points[0].y, s.points[1].y));
        if (s.tool == Tool::Rect) {
            pts = {{l, t}, {r, t}, {r, b}, {l, b}, {l, t}};
        } else {
            const float cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
            for (int i = 0; i <= 48; ++i) {
                const double a = i * 2 * 3.14159265358979 / 48;
                pts.emplace_back(cx + rx * static_cast<float>(std::cos(a)), cy + ry * static_cast<float>(std::sin(a)));
            }
        }
        return pts;
    }
    for (const POINT& p : s.points) pts.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    return pts;
}

}  // namespace

void StartGraphics() {
    G::GdiplusStartupInput input;
    G::GdiplusStartup(&g_token, &input, nullptr);
}

void StopGraphics() {
    if (g_token) G::GdiplusShutdown(g_token);
    g_token = 0;
}

float StrokeWidth(Tool tool, int size, float scale) {
    const int level = std::clamp(size, 0, kSizeCount - 1);
    static const float kLine[kSizeCount] = {2.f, 4.f, 8.f};     // pencil, arrow, rectangle, ellipse
    static const float kMarker[kSizeCount] = {10.f, 18.f, 30.f};
    static const float kWide[kSizeCount] = {14.f, 28.f, 56.f};  // brush: large areas
    switch (tool) {
        case Tool::Marker: return kMarker[level] * scale;
        case Tool::Brush: return kWide[level] * scale;
        default: return kLine[level] * scale;
    }
}

float EraserRadius(int size, float scale) {
    static const float kRadius[kSizeCount] = {8.f, 16.f, 30.f};
    return kRadius[std::clamp(size, 0, kSizeCount - 1)] * scale;
}

float TextPresetPx(int size, float scale) {
    static const float kSizes[kSizeCount] = {16.f, 24.f, 36.f};
    return kSizes[std::clamp(size, 0, kSizeCount - 1)] * scale;
}

RECT ShapeBounds(const Shape& s, float scale) {
    if (s.points.empty()) return RECT{0, 0, 0, 0};
    if (s.tool == Tool::Text) {
        RECT r = TextRect(s);
        const int grow = static_cast<int>(s.style.px / 4 + 4 * scale);  // italic overhang, caret
        InflateRect(&r, grow, grow);
        return r;
    }
    RECT r = {s.points[0].x, s.points[0].y, s.points[0].x, s.points[0].y};
    for (const POINT& p : s.points) {
        r.left = std::min(r.left, p.x);
        r.top = std::min(r.top, p.y);
        r.right = std::max(r.right, p.x);
        r.bottom = std::max(r.bottom, p.y);
    }
    const float width = StrokeWidth(s.tool, s.size, scale);
    const int grow = static_cast<int>(width * (s.tool == Tool::Arrow ? 4.f : 1.f) + 20 * scale) + 2;
    InflateRect(&r, grow, grow);
    return r;
}

bool TextContains(const Shape& s, POINT p, float scale) {
    if (s.tool != Tool::Text) return false;
    RECT r = TextRect(s);
    const int grow = static_cast<int>(4 * scale);
    InflateRect(&r, grow, grow);
    return PtInRect(&r, p) != FALSE;
}

bool ShapeTouched(const Shape& s, POINT p, float r, float scale) {
    if (s.points.empty()) return false;
    if (s.tool == Tool::Text) {
        RECT box = TextRect(s);
        InflateRect(&box, static_cast<int>(r), static_cast<int>(r));
        return PtInRect(&box, p) != FALSE;
    }
    const double reach = r + StrokeWidth(s.tool, s.size, scale) / 2;
    const std::vector<G::PointF> pts = Outline(s);
    if (pts.size() == 1) return std::hypot(p.x - pts[0].X, p.y - pts[0].Y) <= reach;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (SegmentDistance(p.x, p.y, pts[i - 1].X, pts[i - 1].Y, pts[i].X, pts[i].Y) <= reach) return true;
    }
    return false;
}

void DrawShapes(HDC dc, const RECT& clip, POINT off, const std::vector<const Shape*>& shapes, bool caret,
                float scale) {
    if (shapes.empty()) return;
    G::Graphics g(dc);
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
    g.SetClip(G::Rect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top));

    for (size_t i = 0; i < shapes.size(); ++i) {
        const Shape& s = *shapes[i];
        const float width = StrokeWidth(s.tool, s.size, scale);
        switch (s.tool) {
            case Tool::Pencil:
            case Tool::Brush:
                DrawPath(g, s, off, width, ToColor(s.color));
                break;
            case Tool::Marker:
                DrawPath(g, s, off, width, ToColor(s.color, 110));  // see-through highlighter
                break;
            case Tool::Arrow:
            case Tool::Rect:
            case Tool::Ellipse: {
                if (s.points.size() < 2) break;
                const float x1 = static_cast<float>(s.points[0].x - off.x);
                const float y1 = static_cast<float>(s.points[0].y - off.y);
                const float x2 = static_cast<float>(s.points[1].x - off.x);
                const float y2 = static_cast<float>(s.points[1].y - off.y);
                G::Pen pen(ToColor(s.color), width);
                if (s.tool == Tool::Arrow) {
                    if (std::hypot(x2 - x1, y2 - y1) < 2) break;
                    const float head = std::max(3.f, 14.f * scale / width);  // in line widths
                    G::AdjustableArrowCap cap(head, head * 0.9f, TRUE);
                    pen.SetCustomEndCap(&cap);
                    pen.SetStartCap(G::LineCapRound);
                    g.DrawLine(&pen, x1, y1, x2, y2);
                } else {
                    const float l = std::min(x1, x2), t = std::min(y1, y2);
                    const float w = std::fabs(x2 - x1), h = std::fabs(y2 - y1);
                    pen.SetLineJoin(G::LineJoinRound);
                    if (s.tool == Tool::Rect) g.DrawRectangle(&pen, l, t, w, h);
                    else g.DrawEllipse(&pen, l, t, w, h);
                }
                break;
            }
            case Tool::Text:
                DrawTextShape(g, s, off, scale, caret && i + 1 == shapes.size());
                break;
            default:
                break;
        }
    }
}
