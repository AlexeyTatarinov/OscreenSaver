#pragma once

#include <windows.h>

#include <string>

#include "capture.h"

// Saves the area (in snapshot pixels) as JPG into Pictures\OscreenSaver. Returns the file path.
bool SaveAreaAsJpg(const Snapshot& shot, const RECT& area, std::wstring& savedPath);

// Puts the area on the clipboard as an image.
bool CopyAreaToClipboard(const Snapshot& shot, const RECT& area, HWND owner);
