#pragma once

#include <windows.h>

#include <string>

#include "capture.h"

// Saves the area (in snapshot pixels) with the format, folder and file name from the settings.
// Returns the file path.
bool SaveArea(const Snapshot& shot, const RECT& area, std::wstring& savedPath);

// File name from a template: %Y %m %d %H %M %S become the date and time (used by the settings preview).
std::wstring ExpandNameTemplate(const std::wstring& tpl);

// Puts the area on the clipboard as an image.
bool CopyAreaToClipboard(const Snapshot& shot, const RECT& area, HWND owner);
