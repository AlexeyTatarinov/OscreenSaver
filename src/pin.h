#pragma once

#include <windows.h>

#include "capture.h"

// Shows a finished screenshot in a small window that stays on top of all other windows.
// Drag to move, mouse wheel to zoom, double-click or Esc to close, right-click for a menu
// (copy, save, close). The window takes ownership of the picture.
void PinPicture(Snapshot picture, POINT screenTopLeft);
