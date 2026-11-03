#pragma once

#include <windows.h>

#include "capture.h"

// Reads the text in a picture with the text recognition built into Windows (no internet needed),
// in the background. When done, posts `doneMessage` to `notify`:
//   wParam = 1 if recognition worked, 0 if it is not available;
//   lParam = std::wstring* with the text (may be empty); the receiver deletes it.
// Takes ownership of the picture.
void RecognizeText(Snapshot picture, HWND notify, UINT doneMessage);
