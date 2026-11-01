#pragma once

#include <windows.h>

// A frozen copy of one monitor's image.
struct Snapshot {
    HBITMAP bitmap = nullptr;
    RECT bounds = {};  // monitor rectangle in virtual-screen pixels

    int Width() const { return bounds.right - bounds.left; }
    int Height() const { return bounds.bottom - bounds.top; }
};

// Monitor under the mouse cursor. If the cursor is not on any monitor, returns nullptr
// (later stages show the monitor picker in that case).
HMONITOR MonitorUnderCursor();

// Copies the current image of the monitor. The caller owns snapshot.bitmap.
bool CaptureMonitor(HMONITOR monitor, Snapshot& out);

void FreeSnapshot(Snapshot& snapshot);
