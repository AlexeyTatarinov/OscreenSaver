#pragma once

#include <windows.h>

#include <string>

// Called after a screenshot was saved and copied: shows a short notice near the clock.
// Clicking the notice opens the folder with the file selected.
void NotifySaved(const std::wstring& path);

void NotifySaveFailed();

// The screenshot was copied to the clipboard without saving a file.
void NotifyCopied();

#include "capture.h"

// Reads the text in the picture in the background; the text is put on the clipboard and a notice
// is shown. Takes ownership of the picture.
void StartTextRecognition(Snapshot picture);

// Starts a long screenshot with scrolling of the area (screen pixels). The frozen screen must be
// closed first, so the real window under the area can scroll.
void StartLongScreenshot(const RECT& screenArea);
