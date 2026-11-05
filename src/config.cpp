#include "config.h"

#include <commctrl.h>
#include <shlobj.h>

namespace {

const wchar_t kKey[] = L"Software\\OscreenSaver";
Config g_config;

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
        return value;
    }
    return fallback;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

std::wstring ReadString(HKEY key, const wchar_t* name, const std::wstring& fallback) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return buf;
    return fallback;
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

}  // namespace

Config& Cfg() { return g_config; }

void LoadConfig() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    const Config defaults;
    g_config.hotkeyVk = ReadDword(key, L"HotkeyVk", defaults.hotkeyVk);
    g_config.hotkeyMods = ReadDword(key, L"HotkeyMods", defaults.hotkeyMods);
    g_config.background = ReadDword(key, L"Background", defaults.background);
    g_config.frames = ReadDword(key, L"Frames", defaults.frames) != 0;
    g_config.magnifier = ReadDword(key, L"Magnifier", defaults.magnifier) != 0;
    g_config.folder = ReadString(key, L"Folder", defaults.folder);
    g_config.png = ReadDword(key, L"Png", defaults.png) != 0;
    g_config.jpgQuality = static_cast<int>(ReadDword(key, L"JpgQuality", defaults.jpgQuality));
    g_config.nameTemplate = ReadString(key, L"NameTemplate", defaults.nameTemplate);
    g_config.clipboardOnly = ReadDword(key, L"ClipboardOnly", defaults.clipboardOnly) != 0;
    g_config.sound = static_cast<int>(ReadDword(key, L"Sound", static_cast<DWORD>(defaults.sound)));
    g_config.soundFile = ReadString(key, L"SoundFile", defaults.soundFile);
    g_config.language = static_cast<int>(ReadDword(key, L"Language", static_cast<DWORD>(defaults.language)));
    if (g_config.hotkeyVk == 0 || g_config.hotkeyVk > 0xFE) g_config.hotkeyVk = defaults.hotkeyVk;
    if (g_config.jpgQuality < 1 || g_config.jpgQuality > 100) g_config.jpgQuality = defaults.jpgQuality;
    if (g_config.nameTemplate.empty()) g_config.nameTemplate = defaults.nameTemplate;
    RegCloseKey(key);
}

void SaveConfig() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    WriteDword(key, L"HotkeyVk", g_config.hotkeyVk);
    WriteDword(key, L"HotkeyMods", g_config.hotkeyMods);
    WriteDword(key, L"Background", g_config.background);
    WriteDword(key, L"Frames", g_config.frames ? 1 : 0);
    WriteDword(key, L"Magnifier", g_config.magnifier ? 1 : 0);
    WriteString(key, L"Folder", g_config.folder);
    WriteDword(key, L"Png", g_config.png ? 1 : 0);
    WriteDword(key, L"JpgQuality", static_cast<DWORD>(g_config.jpgQuality));
    WriteString(key, L"NameTemplate", g_config.nameTemplate);
    WriteDword(key, L"ClipboardOnly", g_config.clipboardOnly ? 1 : 0);
    WriteDword(key, L"Sound", static_cast<DWORD>(g_config.sound));
    WriteString(key, L"SoundFile", g_config.soundFile);
    WriteDword(key, L"Language", static_cast<DWORD>(g_config.language));
    RegCloseKey(key);
}

std::wstring HotkeyName(UINT vk, UINT mods) {
    std::wstring name;
    if (mods & HOTKEYF_CONTROL) name += L"Ctrl+";
    if (mods & HOTKEYF_SHIFT) name += L"Shift+";
    if (mods & HOTKEYF_ALT) name += L"Alt+";
    switch (vk) {
        case VK_SNAPSHOT: return name + L"PrintScreen";
        case VK_PAUSE: return name + L"Pause";
        case VK_SCROLL: return name + L"Scroll Lock";
        case VK_INSERT: return name + L"Insert";
        case VK_DELETE: return name + L"Delete";
        case VK_HOME: return name + L"Home";
        case VK_END: return name + L"End";
        case VK_PRIOR: return name + L"Page Up";
        case VK_NEXT: return name + L"Page Down";
    }
    if (vk >= VK_F1 && vk <= VK_F24) return name + L"F" + std::to_wstring(vk - VK_F1 + 1);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return name + static_cast<wchar_t>(vk);
    wchar_t text[64] = {};
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), text, 64) > 0) return name + text;
    return name + L"#" + std::to_wstring(vk);
}

std::wstring SaveFolder() {
    if (!g_config.folder.empty()) return g_config.folder;
    std::wstring folder;
    PWSTR pictures = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) folder = pictures;
    CoTaskMemFree(pictures);
    return folder.empty() ? folder : folder + L"\\OscreenSaver";
}
