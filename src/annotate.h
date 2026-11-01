#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Drawing tools. The order matches the buttons on the panel.
enum class Tool { Area, Pencil, Marker, Brush, Arrow, Rect, Ellipse, Text, Eraser, Count };

constexpr int kColorCount = 6;
constexpr int kSizeCount = 3;
extern const COLORREF kPalette[kColorCount];

// One drawn object. Coordinates are in monitor pixels.
struct Shape {
    Tool tool = Tool::Pencil;
    COLORREF color = 0;
    int size = 1;                 // 0 = thin, 1 = medium, 2 = thick
    std::vector<POINT> points;    // freehand: the path; arrow/rect/ellipse: start and end; text: origin
    std::wstring text;
};

void StartGraphics();
void StopGraphics();

// Line width in pixels for a tool and size level.
float StrokeWidth(Tool tool, int size, float scale);

// Area that drawing this shape can touch (generous). Text returns an empty rect.
RECT ShapeBounds(const Shape& shape, float scale);

// Draws shapes onto dc, limited to clip. offset is subtracted from every coordinate.
// If caret is true, a text cursor is drawn at the end of the last shape (text being typed).
void DrawShapes(HDC dc, const RECT& clip, POINT offset, const std::vector<const Shape*>& shapes, bool caret,
                float scale);
