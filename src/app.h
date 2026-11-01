#pragma once

#include <string>

// Called after a screenshot was saved and copied: shows a short notice near the clock.
// Clicking the notice opens the folder with the file selected.
void NotifySaved(const std::wstring& path);

void NotifySaveFailed();
