#pragma once

#include <windows.h>

// Settings window, opened from the menu of the icon next to the clock.
// When the user presses OK the new settings are saved and `changedMessage` is posted to `owner`.
void ShowSettings(HINSTANCE instance, HWND owner, UINT changedMessage);

// True while the settings window is open: the screenshot hotkey is paused, so it can be typed there.
bool IsSettingsOpen();

// Lets the settings window handle Tab, Enter and Esc. Call from the message loop.
bool SettingsDialogMessage(MSG* msg);
