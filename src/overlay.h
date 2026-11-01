#pragma once

#include <windows.h>

// Full-screen window that shows the frozen image of the monitor.
// Everything outside the selected area is covered by a grey mask.
// Mouse: drag to select, drag inside to move, drag the handles to resize, single click = whole screen,
// right click = clear selection. Keys: Enter / Ctrl+S / double click = save + copy, Ctrl+A = whole
// screen, Esc = close without saving.

bool RegisterOverlayClass(HINSTANCE instance);

// Freezes the monitor under the cursor and opens the overlay. Does nothing if one is already open.
void OpenOverlay(HINSTANCE instance);

bool IsOverlayOpen();

// Esc and Enter are caught by the global keyboard hook while the overlay is open, so they work even
// if Windows did not give the overlay keyboard focus.
void OverlayKey(unsigned vk);
