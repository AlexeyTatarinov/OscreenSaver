#pragma once

#include <windows.h>

#include "annotate.h"

// The panel next to the selection: tools, colours, line sizes, undo/redo, save, close.
namespace toolbar {

// Button ids. Tools come first and match the Tool enum.
constexpr int kFirstColor = static_cast<int>(Tool::Count);
constexpr int kFirstSize = kFirstColor + kColorCount;
constexpr int kUndo = kFirstSize + kSizeCount;
constexpr int kRedo = kUndo + 1;
constexpr int kSave = kRedo + 1;
constexpr int kClose = kSave + 1;
constexpr int kButtonCount = kClose + 1;
constexpr int kNone = -1;

struct State {
    Tool tool = Tool::Area;
    int color = 0;
    int size = 1;
    bool canUndo = false;
    bool canRedo = false;
    int hover = kNone;
    bool dark = true;
};

// Panel rectangle for a selection: below it if there is room, otherwise above, otherwise inside.
RECT Place(const RECT& selection, const RECT& screen, float scale);

int HitTest(const RECT& panel, POINT p, float scale);
RECT ButtonRect(const RECT& panel, int id, float scale);

void Draw(HDC dc, const RECT& panel, const State& state, float scale);

// Hint text for a button (shown on hover).
const wchar_t* Tip(int id);

// True if Windows apps use the dark theme.
bool SystemUsesDarkTheme();

}  // namespace toolbar
