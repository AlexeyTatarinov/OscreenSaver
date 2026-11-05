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

    // Saving.
    std::wstring folder;           // empty = Pictures\OscreenSaver
    bool png = false;              // false = JPG
    int jpgQuality = 90;           // 1..100
    std::wstring nameTemplate = L"OscreenSaver_%Y-%m-%d_%H-%M-%S";  // %Y %m %d %H %M %S = date and time
    bool clipboardOnly = false;    // Enter copies to the clipboard without saving a file

    // Sound after saving or copying: 0 shutter, 1 click, 2 chime, 3 own file.
    int sound = 0;

    int language = 0;              // 0 English, 1 Ukrainian, 2 Spanish (enum Lang)
    std::wstring soundFile;
};

// Folder where screenshots are saved (the chosen one, or Pictures\OscreenSaver).
std::wstring SaveFolder();

Config& Cfg();
void LoadConfig();
void SaveConfig();

// Readable name of a key combination, e.g. "Ctrl+Shift+S" or "PrintScreen".
std::wstring HotkeyName(UINT vk, UINT mods);
