#include "annotate.h"

#include <algorithm>
#include <cmath>
using std::max;
using std::min;
#include <objidl.h>
#include <gdiplus.h>

namespace G = Gdiplus;

const COLORREF kPalette[kColorCount] = {
    RGB(232, 17, 35),   // red
    RGB(255, 200, 0),   // yellow
    RGB(16, 176, 64),   // green
    RGB(0, 120, 215),   // blue
    RGB(0, 0, 0),       // black
    RGB(255, 255, 255), // white
};

namespace {

ULONG_PTR g_token = 0;

G::Color ToColor(COLORREF c, BYTE alpha = 255) {
    return G::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

float TextSize(int size, float scale) {
    static const float kSizes[kSizeCount] = {16.f, 24.f, 36.f};
    return kSizes[std::clamp(size, 0, kSizeCount - 1)] * scale;
}

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

void DrawText(G::Graphics& g, const Shape& s, POINT off, float scale, bool caret) {
    if (s.points.empty()) return;
    G::FontFamily segoe(L"Segoe UI");
    const G::FontFamily* family = segoe.IsAvailable() ? &segoe : G::FontFamily::GenericSansSerif();
    G::Font font(family, TextSize(s.size, scale), G::FontStyleRegular, G::UnitPixel);
    G::SolidBrush brush(ToColor(s.color));
    const G::PointF origin(static_cast<float>(s.points[0].x - off.x), static_cast<float>(s.points[0].y - off.y));
    const G::StringFormat* format = G::StringFormat::GenericTypographic();
    G::StringFormat fmt(format);
    fmt.SetFormatFlags(fmt.GetFormatFlags() | G::StringFormatFlagsMeasureTrailingSpaces);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
    if (!s.text.empty()) {
        g.DrawString(s.text.c_str(), static_cast<INT>(s.text.size()), &font, origin, &fmt, &brush);
    }
    if (!caret) return;

    // Cursor after the last character of the last line.
    const size_t lineStart = s.text.find_last_of(L'\n');
    const std::wstring last = lineStart == std::wstring::npos ? s.text : s.text.substr(lineStart + 1);
    const int lineIndex = static_cast<int>(std::count(s.text.begin(), s.text.end(), L'\n'));
    G::RectF box;
    if (!last.empty()) {
        g.MeasureString(last.c_str(), static_cast<INT>(last.size()), &font, G::PointF(0, 0), &fmt, &box);
    }
    const float lineHeight = font.GetHeight(&g);
    const float x = origin.X + box.Width + 1;
    const float y = origin.Y + lineHeight * lineIndex;
    G::Pen pen(ToColor(s.color), std::max(1.f, scale));
    g.DrawLine(&pen, x, y, x, y + lineHeight);
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
    static const float kWide[kSizeCount] = {14.f, 28.f, 56.f};  // brush and eraser: large areas
    switch (tool) {
        case Tool::Marker: return kMarker[level] * scale;
        case Tool::Brush:
        case Tool::Eraser: return kWide[level] * scale;
        default: return kLine[level] * scale;
    }
}

RECT ShapeBounds(const Shape& s, float scale) {
    if (s.tool == Tool::Text || s.points.empty()) return RECT{0, 0, 0, 0};
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
            case Tool::Eraser:
                DrawPath(g, s, off, width, G::Color(255, 255, 255, 255));  // leaves a white area
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
                DrawText(g, s, off, scale, caret && i + 1 == shapes.size());
                break;
            default:
                break;
        }
    }
}
