#pragma once

#include <windows.h>

// Full-screen window that shows the frozen image of the monitor.
// Stage 1: shows the image and closes on Esc. Area selection and drawing come in later stages.

bool RegisterOverlayClass(HINSTANCE instance);

// Freezes the monitor under the cursor and opens the overlay. Does nothing if one is already open.
void OpenOverlay(HINSTANCE instance);

bool IsOverlayOpen();
