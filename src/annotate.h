#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// Drawing tools. The order matches the buttons on the panel.
// Step = numbered marker 1, 2, 3...; Picker = eyedropper (copies the colour under the cursor).
enum class Tool { Area, Select, Pencil, Marker, Brush, Arrow, Line, Rect, Ellipse, Blur, Step, Text, Eraser, Picker, Count };

// Fixed colours on the panel: green, red, white, black. Any other colour is picked in the colour window.
constexpr int kColorCount = 4;
constexpr int kDefaultColor = 1;  // red
extern const COLORREF kPalette[kColorCount];
constexpr int kSizeCount = 3;

struct Pt {
    float x = 0;
    float y = 0;
};

inline Pt ToPt(POINT p) { return Pt{static_cast<float>(p.x), static_cast<float>(p.y)}; }

struct TextStyle {
    std::wstring font = L"Segoe UI";
    float px = 24.f;  // letter height in screen pixels
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool underline = false;

    bool operator==(const TextStyle& o) const {
        return font == o.font && px == o.px && bold == o.bold && italic == o.italic && strike == o.strike &&
               underline == o.underline;
    }
    bool operator!=(const TextStyle& o) const { return !(*this == o); }
};

// A piece of text with one look. One text object can hold many pieces: bold, then normal, and so on.
struct TextRun {
    std::wstring text;
    TextStyle style;
    COLORREF color = 0;
};

// Part of a drawn object rubbed out with the partial eraser (like in Paint).
struct Erasure {
    std::vector<Pt> points;
    float width = 10;
};

// One drawn object. Coordinates are in monitor pixels.
struct Shape {
    Tool tool = Tool::Pencil;
    COLORREF color = 0;
    int size = 1;                  // 0 = thin, 1 = medium, 2 = thick
    std::vector<Pt> points;        // freehand: the path; arrow/rect/ellipse: start and end; text: top-left
    std::vector<TextRun> runs;     // text only
    std::vector<Erasure> erasures; // rubbed-out parts
    int number = 0;                // numbered marker only
};

// Radius of a numbered marker.
float StepRadius(int size, float scale);

void StartGraphics();
void StopGraphics();

float StrokeWidth(Tool tool, int size, float scale);
float EraserRadius(int size, float scale);
float TextPresetPx(int size, float scale);

bool IsTwoPointTool(Tool t);

// Area that drawing this shape can touch (generous, for repainting).
RECT ShapeBounds(const Shape& shape, float scale);

// Tight box around the shape (for the selection frame).
RECT ShapeBox(const Shape& shape, float scale);

// Object eraser: true if the circle (center p, radius r) touches the shape.
bool ShapeTouched(const Shape& shape, Pt p, float r, float scale);

// Selection tool: true if a click at p picks this shape.
bool ShapeHit(const Shape& shape, Pt p, float scale);

void MoveShape(Shape& shape, float dx, float dy);

// Scales a copy of the original shape around anchor a. Text keeps its proportions (uses sy).
void ScaleShape(Shape& shape, const Shape& original, Pt a, float sx, float sy);

// Text cursor shown at the end of the text being typed, in the look the next letters will have.
struct Caret {
    const Shape* shape = nullptr;
    TextStyle style;
    COLORREF color = 0;
    int index = -1;  // position in the text, -1 = at the end
};

// ---------- Editing text at the cursor ----------
// Positions count letters from the start of the whole text (all pieces together), 0..TextLength.

int TextLength(const Shape& text);

// Cursor position nearest to point p (a click inside the text).
int TextPositionAt(const Shape& text, Pt p);

// Cursor position on the line above (-1) or below (+1), or the start (-2) / end (+2) of the line.
int TextMoveOnLine(const Shape& text, int position, int how);

void TextInsert(Shape& text, int position, wchar_t ch, const TextStyle& style, COLORREF color);
void TextErase(Shape& text, int position);  // removes the letter at position

// Look of the letter just before the position (or the first letter). False for an empty text.
bool TextLookAt(const Shape& text, int position, TextStyle& style, COLORREF& color);

// Blurred copies of the screenshot, one per strength (size button). Blur objects show a piece of them.
struct BlurSource {
    const uint32_t* pixels[kSizeCount] = {};
    int width = 0;
    int height = 0;
};

// Blur strength in pixels for a size button.
int BlurRadius(int size, float scale);

// Draws shapes onto dc, limited to clip. offset is subtracted from every coordinate.
void DrawShapes(HDC dc, const RECT& clip, POINT offset, const std::vector<const Shape*>& shapes, const Caret& caret,
                const BlurSource& blur, float scale);
