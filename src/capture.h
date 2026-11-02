#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

// A frozen copy of one monitor's image, stored as 32-bit pixels (BGRX, top-down).
struct Snapshot {
    HBITMAP bitmap = nullptr;
    uint32_t* pixels = nullptr;  // owned by bitmap
    RECT bounds = {};            // monitor rectangle in virtual-screen pixels

    int Width() const { return bounds.right - bounds.left; }
    int Height() const { return bounds.bottom - bounds.top; }
};

// Monitor under the mouse cursor, or nullptr if the cursor is not on any monitor.
HMONITOR MonitorUnderCursor();

std::vector<HMONITOR> AllMonitors();

// Copies the current image of the monitor. Free with FreeSnapshot.
bool CaptureMonitor(HMONITOR monitor, Snapshot& out);

// Empty picture of the given size (used to compose the final image).
bool CreateBlankSnapshot(int width, int height, Snapshot& out);

// Makes a darkened grey copy used as the mask around the selected area.
bool MakeDimmedCopy(const Snapshot& source, Snapshot& out);

// Makes a blurred copy (three box blurs ~ a smooth Gaussian blur). radius is in pixels.
bool MakeBlurredCopy(const Snapshot& source, int radius, Snapshot& out);

void FreeSnapshot(Snapshot& snapshot);
