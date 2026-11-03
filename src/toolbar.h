#pragma once

#include <windows.h>

#include "annotate.h"

// The panel next to the selection: tools, colours, line sizes, undo/redo, save, close.
namespace toolbar {

// Button ids. Tools come first and match the Tool enum.
constexpr int kFirstColor = static_cast<int>(Tool::Count);
constexpr int kCustomColor = kFirstColor + kColorCount;  // opens the colour window
constexpr int kFirstSize = kCustomColor + 1;
constexpr int kEraseWhole = kFirstSize + kSizeCount;     // eraser buttons: shown only for the eraser
constexpr int kErasePart = kEraseWhole + 1;
constexpr int kBold = kErasePart + 1;                    // text buttons: shown only for the text tool
constexpr int kItalic = kBold + 1;
constexpr int kStrike = kItalic + 1;
constexpr int kFont = kStrike + 1;                        // opens the font window
constexpr int kUndo = kFont + 1;
constexpr int kRedo = kUndo + 1;
constexpr int kAddArea = kRedo + 1;                       // one more area for a side-by-side picture
constexpr int kFrames = kAddArea + 1;                     // frames around joined areas on/off
constexpr int kOcr = kFrames + 1;                         // copy the text from the picture
constexpr int kLong = kOcr + 1;                           // long screenshot with scrolling
constexpr int kPin = kLong + 1;                           // pin the picture on top of all windows
constexpr int kCopy = kPin + 1;                           // copy only, no file
constexpr int kSave = kCopy + 1;
constexpr int kClose = kSave + 1;
constexpr int kButtonCount = kClose + 1;
constexpr int kNone = -1;

struct State {
    Tool tool = Tool::Area;
    int color = kDefaultColor;  // 0..kColorCount-1, or kColorCount for a colour from the colour window
    COLORREF customColor = RGB(0, 120, 215);
    bool hasCustom = false;
    int size = 1;               // highlighted size button, -1 = none (text with a size from the font window)
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool eraseParts = false;  // eraser rubs out parts (like Paint) instead of removing whole objects
    bool canUndo = false;
    bool canRedo = false;
    bool canAddArea = false;
    bool canLong = false;
    bool compare = false;  // several areas: the frame button is shown
    bool frames = true;
    int hover = kNone;
    bool dark = true;
};

// Panel rectangle for a selection: below it if there is room, otherwise above, otherwise inside.
RECT Place(const RECT& selection, const RECT& screen, float scale, const State& state);

int HitTest(const RECT& panel, POINT p, float scale, const State& state);
RECT ButtonRect(const RECT& panel, int id, float scale, const State& state);

void Draw(HDC dc, const RECT& panel, const State& state, float scale);

// Hint text for a button (shown on hover).
const wchar_t* Tip(int id);

// True if Windows apps use the dark theme.
bool SystemUsesDarkTheme();

// The accent colour chosen in Windows settings (Personalisation > Colours).
COLORREF AccentColor();

// Rounded label in the Windows style (hints, size of the selection, button tips).
void DrawLabel(HDC dc, const RECT& box, const wchar_t* text, bool dark, float scale);

}  // namespace toolbar
