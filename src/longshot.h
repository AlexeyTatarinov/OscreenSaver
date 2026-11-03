#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

// Long screenshot with scrolling.
// The area (screen pixels) is captured, the window under it is scrolled with the mouse wheel, captured
// again, and so on; the pictures are joined into one tall picture (see scroll_stitch.h). It stops at
// the end of the page, at a height limit, or when the user presses Esc.
// The work runs in a background thread; a small status window shows the progress.

struct LongShotResult {
    std::vector<uint32_t> pixels;  // BGRX, rows from top to bottom
    int width = 0;
    int height = 0;
    bool scrolled = false;  // false: the window did not scroll at all
};

// Starts the long screenshot. When it is done, `doneMessage` is posted to `notify` with
// lParam = LongShotResult* (the receiver deletes it).
void StartLongShot(const RECT& screenArea, HWND notify, UINT doneMessage);

bool IsLongShotRunning();

// Esc pressed: stop and keep what was captured so far.
void StopLongShot();
