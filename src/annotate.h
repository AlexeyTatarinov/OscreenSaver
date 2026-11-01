#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Drawing tools. The order matches the buttons on the panel.
enum class Tool { Area, Pencil, Marker, Brush, Arrow, Rect, Ellipse, Text, Eraser, Count };

// Fixed colours on the panel: green, red, white, black. Any other colour is picked in the colour window.
constexpr int kColorCount = 4;
constexpr int kDefaultColor = 1;  // red
extern const COLORREF kPalette[kColorCount];
constexpr int kSizeCount = 3;

struct TextStyle {
    std::wstring font = L"Segoe UI";
    float px = 24.f;  // letter height in screen pixels
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool underline = false;
};

// One drawn object. Coordinates are in monitor pixels.
struct Shape {
    Tool tool = Tool::Pencil;
    COLORREF color = 0;
    int size = 1;                 // 0 = thin, 1 = medium, 2 = thick
    std::vector<POINT> points;    // freehand: the path; arrow/rect/ellipse: start and end; text: origin
    std::wstring text;
    TextStyle style;              // text only
};

void StartGraphics();
void StopGraphics();

// Line width in pixels for a tool and size level.
float StrokeWidth(Tool tool, int size, float scale);

// Radius of the eraser circle.
float EraserRadius(int size, float scale);

// Text heights for the three size buttons.
float TextPresetPx(int size, float scale);

// Area that drawing this shape can touch (generous).
RECT ShapeBounds(const Shape& shape, float scale);

// True if the eraser circle (center p, radius r) touches the shape.
bool ShapeTouched(const Shape& shape, POINT p, float r, float scale);

// True if p is on the text (used to edit an existing text by clicking it).
bool TextContains(const Shape& shape, POINT p, float scale);

// Draws shapes onto dc, limited to clip. offset is subtracted from every coordinate.
// If caret is true, a text cursor is drawn at the end of the last shape (text being typed).
void DrawShapes(HDC dc, const RECT& clip, POINT offset, const std::vector<const Shape*>& shapes, bool caret,
                float scale);
