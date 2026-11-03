#pragma once

#include <windows.h>

#include <string>

// Settings chosen in the settings window. Kept in the registry (HKCU\Software\OscreenSaver).
struct Config {
    UINT hotkeyVk = VK_SNAPSHOT;   // key that starts a screenshot
    UINT hotkeyMods = 0;           // HOTKEYF_SHIFT / HOTKEYF_CONTROL / HOTKEYF_ALT
    COLORREF background = RGB(0, 0, 0);  // free space in a picture joined from several areas
    bool frames = true;            // thin grey frame around each joined area
    bool magnifier = false;        // magnified pixels next to the cursor while selecting
};

Config& Cfg();
void LoadConfig();
void SaveConfig();

// Readable name of a key combination, e.g. "Ctrl+Shift+S" or "PrintScreen".
std::wstring HotkeyName(UINT vk, UINT mods);
