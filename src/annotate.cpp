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

void Prepare(G::Graphics& g) {
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
}

// ---------- Text layout: pieces with different looks on shared lines ----------

struct FontPack {
    std::unique_ptr<G::FontFamily> family;
    std::unique_ptr<G::Font> font;
    float ascent = 0;   // from the top of the letters to the base line
    float descent = 0;
    float height = 0;   // line height
};

FontPack MakeFont(const TextStyle& st, G::Graphics& g) {
    FontPack f;
    f.family = std::make_unique<G::FontFamily>(st.font.c_str());
    if (!f.family->IsAvailable()) f.family = std::make_unique<G::FontFamily>(L"Segoe UI");
    if (!f.family->IsAvailable()) f.family.reset(G::FontFamily::GenericSansSerif()->Clone());
    INT style = G::FontStyleRegular;
    if (st.bold) style |= G::FontStyleBold;
    if (st.italic) style |= G::FontStyleItalic;
    // Strikethrough and underline are drawn as lines by DrawText, the same way for every font.
    // Some fonts lack a bold or italic face; fall back to what the font has.
    if (!f.family->IsStyleAvailable(style)) style &= ~(G::FontStyleBold | G::FontStyleItalic);
    const float size = std::max(4.f, st.px);
    f.font = std::make_unique<G::Font>(f.family.get(), size, style, G::UnitPixel);
    const float em = std::max<float>(1, f.family->GetEmHeight(style));
    f.ascent = size * f.family->GetCellAscent(style) / em;
    f.descent = size * f.family->GetCellDescent(style) / em;
    f.height = std::max(f.font->GetHeight(&g), f.ascent + f.descent);
    return f;
}

const G::StringFormat& Format() {
    static G::StringFormat* format = [] {
        G::StringFormat* f = G::StringFormat::GenericTypographic()->Clone();
        f->SetFormatFlags(f->GetFormatFlags() | G::StringFormatFlagsMeasureTrailingSpaces |
                          G::StringFormatFlagsNoWrap);
        return f;
    }();
    return *format;
}

struct Piece {
    std::wstring text;
    size_t font = 0;  // index in Layout::fonts
    float x = 0;
    size_t line = 0;
};

struct Line {
    float top = 0;
    float ascent = 0;
    float descent = 0;
    float height = 0;
    float right = 0;
};

struct Stop {
    float x = 0;      // where a text cursor at this position stands
    size_t line = 0;
};

struct Layout {
    std::vector<FontPack> fonts;  // one per run, then the cursor's
    std::vector<Piece> pieces;
    std::vector<Line> lines;
    std::vector<Stop> stops;      // cursor positions 0..length (only when asked for)
    float caretX = 0;
    float caretTop = 0;
    float caretHeight = 0;
    G::RectF box;
};

void Grow(Line& line, const FontPack& f) {
    line.ascent = std::max(line.ascent, f.ascent);
    line.descent = std::max(line.descent, f.descent);
    line.height = std::max(line.height, f.height);
}

float TextWidth(G::Graphics& g, const std::wstring& text, size_t count, const FontPack& f) {
    if (count == 0) return 0;
    G::RectF m;
    g.MeasureString(text.c_str(), static_cast<INT>(count), f.font.get(), G::PointF(0, 0), &Format(), &m);
    return m.Width;
}

// caretIndex: position of the text cursor (-1 = at the end). withStops: also find every cursor position.
Layout MakeLayout(G::Graphics& g, const Shape& s, const TextStyle* caretStyle, int caretIndex = -1,
                  bool withStops = false) {
    Layout L;
    const Pt origin = s.points.empty() ? Pt{} : s.points[0];
    for (const TextRun& run : s.runs) L.fonts.push_back(MakeFont(run.style, g));
    const TextStyle fallback;
    L.fonts.push_back(MakeFont(caretStyle ? *caretStyle : (s.runs.empty() ? fallback : s.runs.back().style), g));
    const size_t caretFont = L.fonts.size() - 1;
    if (caretStyle) withStops = true;

    size_t total = 0;
    for (const TextRun& run : s.runs) total += run.text.size();
    if (withStops) {
        L.stops.assign(total + 1, Stop{origin.x, 0});
    }

    L.lines.emplace_back();
    Grow(L.lines.back(), L.fonts[s.runs.empty() ? caretFont : 0]);
    float x = origin.x;
    size_t global = 0;  // index of the first letter of the current run in the whole text
    for (size_t r = 0; r < s.runs.size(); ++r) {
        const std::wstring& text = s.runs[r].text;
        size_t start = 0;
        while (true) {
            const size_t nl = text.find(L'\n', start);
            const std::wstring part = text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
            if (!part.empty()) {
                const size_t line = L.lines.size() - 1;
                L.pieces.push_back(Piece{part, r, x, line});
                if (withStops) {
                    for (size_t k = 1; k <= part.size(); ++k) {
                        L.stops[global + start + k] = Stop{x + TextWidth(g, part, k, L.fonts[r]), line};
                    }
                }
                x += TextWidth(g, part, part.size(), L.fonts[r]);
                Grow(L.lines.back(), L.fonts[r]);
            }
            L.lines.back().right = x;
            if (nl == std::wstring::npos) break;
            L.lines.emplace_back();  // a new line starts with the look of the text that made it
            Grow(L.lines.back(), L.fonts[r]);
            x = origin.x;
            if (withStops) L.stops[global + nl + 1] = Stop{origin.x, L.lines.size() - 1};
            start = nl + 1;
        }
        global += text.size();
    }
    L.lines.back().right = std::max(L.lines.back().right, x);

    size_t caretLine = L.lines.size() - 1;
    L.caretX = x;
    if (withStops) {
        const size_t idx = caretIndex < 0 ? total : std::min<size_t>(static_cast<size_t>(caretIndex), total);
        L.caretX = L.stops[idx].x;
        caretLine = L.stops[idx].line;
    }
    if (caretStyle) Grow(L.lines[caretLine], L.fonts[caretFont]);

    float top = origin.y;
    float right = origin.x;
    for (Line& line : L.lines) {
        line.top = top;
        top += line.height;
        right = std::max(right, line.right);
    }
    const Line& cl = L.lines[caretLine];
    const FontPack& cf = L.fonts[caretFont];
    L.caretTop = cl.top + cl.ascent - cf.ascent;
    L.caretHeight = cf.ascent + cf.descent;
    L.box = G::RectF(origin.x, origin.y, right - origin.x, top - origin.y);
    return L;
}

G::RectF TextBox(const Shape& s) {
    HDC dc = CreateCompatibleDC(nullptr);
    G::RectF box;
    {
        G::Graphics g(dc);
        Prepare(g);
        box = MakeLayout(g, s, nullptr).box;
    }
    DeleteDC(dc);
    return box;
}

void DrawText(G::Graphics& g, const Shape& s, const TextStyle* caretStyle, COLORREF caretColor, int caretIndex,
              float scale) {
    if (s.points.empty()) return;
    const Layout L = MakeLayout(g, s, caretStyle, caretIndex);
    for (const Piece& p : L.pieces) {
        const Line& line = L.lines[p.line];
        const FontPack& f = L.fonts[p.font];
        G::SolidBrush brush(ToColor(s.runs[p.font].color));
        const float y = line.top + line.ascent - f.ascent;  // shared base line for different sizes
        g.DrawString(p.text.c_str(), static_cast<INT>(p.text.size()), f.font.get(), G::PointF(p.x, y), &Format(),
                     &brush);
        const TextStyle& st = s.runs[p.font].style;
        if (st.strike || st.underline) {
            G::RectF m;
            g.MeasureString(p.text.c_str(), static_cast<INT>(p.text.size()), f.font.get(), G::PointF(0, 0),
                            &Format(), &m);
            const float base = line.top + line.ascent;
            const float thick = std::max(1.f, st.px / 14.f);
            G::Pen pen(ToColor(s.runs[p.font].color), thick);
            if (st.strike) {
                const float yy = base - f.ascent * 0.3f;
                g.DrawLine(&pen, p.x, yy, p.x + m.Width, yy);
            }
            if (st.underline) {
                const float yy = base + std::max(thick, f.descent * 0.35f);
                g.DrawLine(&pen, p.x, yy, p.x + m.Width, yy);
            }
        }
    }
    if (caretStyle) {
        G::Pen pen(ToColor(caretColor), std::max(1.f, scale));
        const float x = L.caretX + 1;
        g.DrawLine(&pen, x, L.caretTop, x, L.caretTop + L.caretHeight);
    }
}

// ---------- Lines and figures ----------

void DrawPath(G::Graphics& g, const std::vector<Pt>& points, float width, const G::Color& color) {
    if (points.empty()) return;
    if (points.size() == 1) {
        G::SolidBrush brush(color);
        g.FillEllipse(&brush, points[0].x - width / 2, points[0].y - width / 2, width, width);
        return;
    }
    std::vector<G::PointF> pts;
    pts.reserve(points.size());
    for (const Pt& p : points) pts.emplace_back(p.x, p.y);
    G::Pen pen(color, width);
    pen.SetLineCap(G::LineCapRound, G::LineCapRound, G::DashCapRound);
    pen.SetLineJoin(G::LineJoinRound);
    g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
}

void DrawBlur(G::Graphics& g, const Shape& s, const BlurSource& blur) {
    if (s.points.size() < 2) return;
    const int level = std::clamp(s.size, 0, kSizeCount - 1);
    const uint32_t* pixels = blur.pixels[level];
    if (!pixels) return;
    const int l = std::clamp(static_cast<int>(std::min(s.points[0].x, s.points[1].x)), 0, blur.width);
    const int t = std::clamp(static_cast<int>(std::min(s.points[0].y, s.points[1].y)), 0, blur.height);
    const int r = std::clamp(static_cast<int>(std::max(s.points[0].x, s.points[1].x)), 0, blur.width);
    const int b = std::clamp(static_cast<int>(std::max(s.points[0].y, s.points[1].y)), 0, blur.height);
    if (r <= l || b <= t) return;
    // Wraps the blurred picture without copying it and shows the part under the object.
    G::Bitmap source(blur.width, blur.height, blur.width * 4, PixelFormat32bppRGB,
                     reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pixels)));
    const G::PixelOffsetMode mode = g.GetPixelOffsetMode();
    g.SetPixelOffsetMode(G::PixelOffsetModeNone);
    g.DrawImage(&source, G::Rect(l, t, r - l, b - t), l, t, r - l, b - t, G::UnitPixel);
    g.SetPixelOffsetMode(mode);
}

// Numbered marker: a filled circle with the number in a contrasting colour.
void DrawStep(G::Graphics& g, const Shape& s, float scale) {
    if (s.points.empty()) return;
    const float r = StepRadius(s.size, scale);
    const Pt c = s.points[0];
    G::SolidBrush fill(ToColor(s.color));
    g.FillEllipse(&fill, c.x - r, c.y - r, 2 * r, 2 * r);
    const int brightness = (GetRValue(s.color) * 299 + GetGValue(s.color) * 587 + GetBValue(s.color) * 114) / 1000;
    G::Pen edge(brightness > 160 ? G::Color(160, 0, 0, 0) : G::Color(200, 255, 255, 255), std::max(1.f, scale));
    g.DrawEllipse(&edge, c.x - r, c.y - r, 2 * r, 2 * r);
    G::FontFamily segoe(L"Segoe UI");
    const G::FontFamily* family = segoe.IsAvailable() ? &segoe : G::FontFamily::GenericSansSerif();
    const INT style = family->IsStyleAvailable(G::FontStyleBold) ? G::FontStyleBold : G::FontStyleRegular;
    const std::wstring text = std::to_wstring(s.number);
    G::Font font(family, r * (text.size() > 1 ? 1.0f : 1.2f), style, G::UnitPixel);
    G::StringFormat fmt;
    fmt.SetAlignment(G::StringAlignmentCenter);
    fmt.SetLineAlignment(G::StringAlignmentCenter);
    G::SolidBrush ink(brightness > 160 ? G::Color(255, 0, 0, 0) : G::Color(255, 255, 255, 255));
    g.DrawString(text.c_str(), -1, &font, G::RectF(c.x - r, c.y - r + r * 0.06f, 2 * r, 2 * r), &fmt, &ink);
}

void DrawOne(G::Graphics& g, const Shape& s, const TextStyle* caretStyle, COLORREF caretColor, int caretIndex,
             const BlurSource& blur, float scale) {
    const float width = StrokeWidth(s.tool, s.size, scale);
    switch (s.tool) {
        case Tool::Step:
            DrawStep(g, s, scale);
            break;
        case Tool::Blur:
            DrawBlur(g, s, blur);
            break;
        case Tool::Pencil:
        case Tool::Brush:
            DrawPath(g, s.points, width, ToColor(s.color));
            break;
        case Tool::Marker:
            DrawPath(g, s.points, width, ToColor(s.color, 110));  // see-through highlighter
            break;
        case Tool::Arrow:
        case Tool::Rect:
        case Tool::Ellipse: {
            if (s.points.size() < 2) break;
            const Pt a = s.points[0], b = s.points[1];
            G::Pen pen(ToColor(s.color), width);
            if (s.tool == Tool::Arrow) {
                if (std::hypot(b.x - a.x, b.y - a.y) < 2) break;
                const float head = std::max(3.f, 14.f * scale / width);  // in line widths
                G::AdjustableArrowCap cap(head, head * 0.9f, TRUE);
                pen.SetCustomEndCap(&cap);
                pen.SetStartCap(G::LineCapRound);
                g.DrawLine(&pen, a.x, a.y, b.x, b.y);
            } else {
                const float l = std::min(a.x, b.x), t = std::min(a.y, b.y);
                const float w = std::fabs(b.x - a.x), h = std::fabs(b.y - a.y);
                pen.SetLineJoin(G::LineJoinRound);
                if (s.tool == Tool::Rect) g.DrawRectangle(&pen, l, t, w, h);
                else g.DrawEllipse(&pen, l, t, w, h);
            }
            break;
        }
        case Tool::Text:
            DrawText(g, s, caretStyle, caretColor, caretIndex, scale);
            break;
        default:
            break;
    }
}

double SegmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Outline of a shape as a list of points (closed shapes repeat the first point).
std::vector<Pt> Outline(const Shape& s) {
    if (s.tool == Tool::Rect || s.tool == Tool::Ellipse) {
        std::vector<Pt> pts;
        if (s.points.size() < 2) return pts;
        const float l = std::min(s.points[0].x, s.points[1].x), r = std::max(s.points[0].x, s.points[1].x);
        const float t = std::min(s.points[0].y, s.points[1].y), b = std::max(s.points[0].y, s.points[1].y);
        if (s.tool == Tool::Rect) return {{l, t}, {r, t}, {r, b}, {l, b}, {l, t}};
        const float cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
        for (int i = 0; i <= 48; ++i) {
            const double a = i * 2 * 3.14159265358979 / 48;
            pts.push_back(Pt{cx + rx * static_cast<float>(std::cos(a)), cy + ry * static_cast<float>(std::sin(a))});
        }
        return pts;
    }
    return s.points;
}

bool NearOutline(const Shape& s, Pt p, double reach) {
    const std::vector<Pt> pts = Outline(s);
    if (pts.empty()) return false;
    if (pts.size() == 1) return std::hypot(p.x - pts[0].x, p.y - pts[0].y) <= reach;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (SegmentDistance(p.x, p.y, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y) <= reach) return true;
    }
    return false;
}

RECT ToRect(float l, float t, float r, float b) {
    return RECT{static_cast<LONG>(std::floor(l)), static_cast<LONG>(std::floor(t)), static_cast<LONG>(std::ceil(r)),
                static_cast<LONG>(std::ceil(b))};
}

}  // namespace

// ---------- Editing text at the cursor ----------

namespace {

Layout LayoutWithStops(const Shape& s) {
    HDC dc = CreateCompatibleDC(nullptr);
    Layout L;
    {
        G::Graphics g(dc);
        Prepare(g);
        L = MakeLayout(g, s, nullptr, -1, true);
    }
    DeleteDC(dc);
    return L;
}

// Splits the pieces so that a piece boundary falls exactly at position; returns the piece index there.
size_t SplitAt(Shape& s, int position) {
    int start = 0;
    for (size_t i = 0; i < s.runs.size(); ++i) {
        const int len = static_cast<int>(s.runs[i].text.size());
        if (position == start) return i;
        if (position < start + len) {
            TextRun tail = s.runs[i];
            tail.text = s.runs[i].text.substr(static_cast<size_t>(position - start));
            s.runs[i].text.resize(static_cast<size_t>(position - start));
            s.runs.insert(s.runs.begin() + static_cast<std::ptrdiff_t>(i) + 1, tail);
            return i + 1;
        }
        start += len;
    }
    return s.runs.size();
}

// Joins neighbouring pieces with the same look and removes empty ones.
void Tidy(Shape& s) {
    std::vector<TextRun> out;
    for (TextRun& r : s.runs) {
        if (r.text.empty()) continue;
        if (!out.empty() && out.back().style == r.style && out.back().color == r.color) {
            out.back().text += r.text;
        } else {
            out.push_back(r);
        }
    }
    s.runs.swap(out);
}

}  // namespace

int TextLength(const Shape& s) {
    int n = 0;
    for (const TextRun& r : s.runs) n += static_cast<int>(r.text.size());
    return n;
}

int TextPositionAt(const Shape& s, Pt p) {
    const Layout L = LayoutWithStops(s);
    if (L.stops.empty()) return 0;
    size_t line = L.lines.size() - 1;
    for (size_t i = 0; i < L.lines.size(); ++i) {
        if (p.y < L.lines[i].top + L.lines[i].height) {
            line = i;
            break;
        }
    }
    int best = -1;
    float bestDistance = 1e30f;
    for (size_t i = 0; i < L.stops.size(); ++i) {
        if (L.stops[i].line != line) continue;
        const float d = std::fabs(L.stops[i].x - p.x);
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best < 0 ? static_cast<int>(L.stops.size()) - 1 : best;
}

int TextMoveOnLine(const Shape& s, int position, int how) {
    const Layout L = LayoutWithStops(s);
    const int last = static_cast<int>(L.stops.size()) - 1;
    position = std::clamp(position, 0, last);
    const size_t line = L.stops[position].line;
    if (how == -2 || how == 2) {
        int result = position;
        for (int i = 0; i <= last; ++i) {
            if (L.stops[i].line != line) continue;
            if (how == -2) return i;  // first position on the line
            result = i;               // last position on the line
        }
        return result;
    }
    const long target = static_cast<long>(line) + how;
    if (target < 0 || target >= static_cast<long>(L.lines.size())) return position;
    int best = position;
    float bestDistance = 1e30f;
    for (int i = 0; i <= last; ++i) {
        if (static_cast<long>(L.stops[i].line) != target) continue;
        const float d = std::fabs(L.stops[i].x - L.stops[position].x);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void TextInsert(Shape& s, int position, wchar_t ch, const TextStyle& style, COLORREF color) {
    position = std::clamp(position, 0, TextLength(s));
    const size_t at = SplitAt(s, position);
    s.runs.insert(s.runs.begin() + static_cast<std::ptrdiff_t>(at), TextRun{std::wstring(1, ch), style, color});
    Tidy(s);
}

void TextErase(Shape& s, int position) {
    int start = 0;
    for (TextRun& r : s.runs) {
        const int len = static_cast<int>(r.text.size());
        if (position >= start && position < start + len) {
            r.text.erase(static_cast<size_t>(position - start), 1);
            break;
        }
        start += len;
    }
    Tidy(s);
}

bool TextLookAt(const Shape& s, int position, TextStyle& style, COLORREF& color) {
    if (s.runs.empty()) return false;
    const int target = std::max(0, position - 1);  // the letter before the cursor
    int start = 0;
    for (const TextRun& r : s.runs) {
        const int len = static_cast<int>(r.text.size());
        if (target < start + len) {
            style = r.style;
            color = r.color;
            return true;
        }
        start += len;
    }
    style = s.runs.back().style;
    color = s.runs.back().color;
    return true;
}

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
    static const float kRadius[kSizeCount] = {6.f, 12.f, 24.f};
    return kRadius[std::clamp(size, 0, kSizeCount - 1)] * scale;
}

float TextPresetPx(int size, float scale) {
    static const float kSizes[kSizeCount] = {16.f, 24.f, 36.f};
    return kSizes[std::clamp(size, 0, kSizeCount - 1)] * scale;
}

bool IsTwoPointTool(Tool t) {
    return t == Tool::Arrow || t == Tool::Rect || t == Tool::Ellipse || t == Tool::Blur;
}

float StepRadius(int size, float scale) {
    static const float kRadius[kSizeCount] = {10.f, 14.f, 19.f};
    return kRadius[std::clamp(size, 0, kSizeCount - 1)] * scale;
}

RECT ShapeBox(const Shape& s, float scale) {
    if (s.points.empty()) return RECT{0, 0, 0, 0};
    if (s.tool == Tool::Step) {
        const float r = StepRadius(s.size, scale);
        return ToRect(s.points[0].x - r, s.points[0].y - r, s.points[0].x + r, s.points[0].y + r);
    }
    if (s.tool == Tool::Text) {
        const G::RectF b = TextBox(s);
        return ToRect(b.X, b.Y, b.GetRight(), b.GetBottom());
    }
    float l = s.points[0].x, t = s.points[0].y, r = l, b = t;
    for (const Pt& p : s.points) {
        l = std::min(l, p.x);
        t = std::min(t, p.y);
        r = std::max(r, p.x);
        b = std::max(b, p.y);
    }
    return ToRect(l, t, r, b);
}

RECT ShapeBounds(const Shape& s, float scale) {
    RECT r = ShapeBox(s, scale);
    if (IsRectEmpty(&r) && s.points.empty()) return r;
    int grow;
    if (s.tool == Tool::Text) {
        float px = 24 * scale;
        for (const TextRun& run : s.runs) px = std::max(px, run.style.px);
        grow = static_cast<int>(px / 2 + 4 * scale);  // italic overhang, cursor
    } else {
        const float width = StrokeWidth(s.tool, s.size, scale);
        grow = static_cast<int>(width * (s.tool == Tool::Arrow ? 4.f : 1.f) + 20 * scale) + 2;
    }
    InflateRect(&r, grow, grow);
    return r;
}

bool ShapeTouched(const Shape& s, Pt p, float r, float scale) {
    if (s.points.empty()) return false;
    if (s.tool == Tool::Step) {
        return std::hypot(p.x - s.points[0].x, p.y - s.points[0].y) <= r + StepRadius(s.size, scale);
    }
    if (s.tool == Tool::Text || s.tool == Tool::Blur) {
        RECT box = ShapeBox(s, scale);
        InflateRect(&box, static_cast<int>(r), static_cast<int>(r));
        return PtInRect(&box, POINT{static_cast<LONG>(p.x), static_cast<LONG>(p.y)}) != FALSE;
    }
    return NearOutline(s, p, r + StrokeWidth(s.tool, s.size, scale) / 2);
}

bool ShapeHit(const Shape& s, Pt p, float scale) {
    if (s.points.empty()) return false;
    if (s.tool == Tool::Step) {
        return std::hypot(p.x - s.points[0].x, p.y - s.points[0].y) <= StepRadius(s.size, scale) + 3 * scale;
    }
    if (s.tool == Tool::Text || s.tool == Tool::Rect || s.tool == Tool::Ellipse || s.tool == Tool::Blur) {
        RECT box = ShapeBox(s, scale);
        const int grow = static_cast<int>(StrokeWidth(s.tool, s.size, scale) / 2 + 5 * scale);
        InflateRect(&box, grow, grow);
        return PtInRect(&box, POINT{static_cast<LONG>(p.x), static_cast<LONG>(p.y)}) != FALSE;
    }
    return NearOutline(s, p, StrokeWidth(s.tool, s.size, scale) / 2 + 5 * scale);
}

void MoveShape(Shape& s, float dx, float dy) {
    for (Pt& p : s.points) {
        p.x += dx;
        p.y += dy;
    }
    for (Erasure& e : s.erasures) {
        for (Pt& p : e.points) {
            p.x += dx;
            p.y += dy;
        }
    }
}

void ScaleShape(Shape& s, const Shape& original, Pt a, float sx, float sy) {
    s = original;
    const bool text = s.tool == Tool::Text;
    if (text) sx = sy;  // letters keep their proportions
    auto map = [&](Pt& p) {
        p.x = a.x + (p.x - a.x) * sx;
        p.y = a.y + (p.y - a.y) * sy;
    };
    for (Pt& p : s.points) map(p);
    for (Erasure& e : s.erasures) {
        for (Pt& p : e.points) map(p);
        if (text) e.width *= sy;
    }
    if (text) {
        for (TextRun& run : s.runs) run.style.px = std::clamp(run.style.px * sy, 4.f, 1000.f);
    }
}

int BlurRadius(int size, float scale) {
    static const float kRadius[kSizeCount] = {4.f, 9.f, 16.f};
    return std::max(1, static_cast<int>(kRadius[std::clamp(size, 0, kSizeCount - 1)] * scale));
}

void DrawShapes(HDC dc, const RECT& clip, POINT off, const std::vector<const Shape*>& shapes, const Caret& caret,
                const BlurSource& blur, float scale) {
    const int w = clip.right - clip.left;
    const int h = clip.bottom - clip.top;
    if (shapes.empty() || w <= 0 || h <= 0) return;

    // Everything is drawn on a transparent layer first. Rubbed-out parts become transparent there,
    // so the screenshot shows through, and only then the layer is put over the screenshot.
    G::Bitmap layer(w, h, PixelFormat32bppPARGB);
    {
        G::Graphics lg(&layer);
        Prepare(lg);
        const float tx = -static_cast<float>(clip.left + off.x);
        const float ty = -static_cast<float>(clip.top + off.y);
        lg.TranslateTransform(tx, ty);
        for (size_t i = 0; i < shapes.size(); ++i) {
            const Shape& s = *shapes[i];
            const TextStyle* cs = &s == caret.shape ? &caret.style : nullptr;
            if (s.erasures.empty()) {
                DrawOne(lg, s, cs, caret.color, caret.index, blur, scale);
                continue;
            }
            // A partly erased object is drawn on its own layer, then the erased parts are cleared.
            G::Bitmap own(w, h, PixelFormat32bppPARGB);
            {
                G::Graphics og(&own);
                Prepare(og);
                og.TranslateTransform(tx, ty);
                DrawOne(og, s, cs, caret.color, caret.index, blur, scale);
                og.SetCompositingMode(G::CompositingModeSourceCopy);
                const G::Color clear(0, 0, 0, 0);
                for (const Erasure& e : s.erasures) DrawPath(og, e.points, e.width, clear);
            }
            lg.ResetTransform();
            lg.DrawImage(&own, G::Rect(0, 0, w, h), 0, 0, w, h, G::UnitPixel);
            lg.TranslateTransform(tx, ty);
        }
    }
    G::Graphics g(dc);
    g.DrawImage(&layer, G::Rect(clip.left, clip.top, w, h), 0, 0, w, h, G::UnitPixel);
}
