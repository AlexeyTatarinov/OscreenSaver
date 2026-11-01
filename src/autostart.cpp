#include "autostart.h"

#include <windows.h>

#include <iterator>
#include <string>

namespace {

const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kAppKey[] = L"Software\\OscreenSaver";
const wchar_t kValueName[] = L"OscreenSaver";

std::wstring QuotedExePath() {
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return L"\"" + std::wstring(path) + L"\"";
}

}  // namespace

bool IsAutostartEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const bool exists = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

void SetAutostart(bool enable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    if (enable) {
        const std::wstring value = QuotedExePath();
        RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kValueName);
    }
    RegCloseKey(key);
}

void ApplyFirstRunAutostart() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kAppKey, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    DWORD configured = 0;
    DWORD size = sizeof(configured);
    const bool known = RegQueryValueExW(key, L"AutostartConfigured", nullptr, nullptr,
                                        reinterpret_cast<BYTE*>(&configured), &size) == ERROR_SUCCESS;
    if (!known || configured == 0) {
        SetAutostart(true);
        configured = 1;
        RegSetValueExW(key, L"AutostartConfigured", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&configured),
                       sizeof(configured));
    } else if (IsAutostartEnabled()) {
        // Keep the stored path current in case the .exe was moved.
        SetAutostart(true);
    }
    RegCloseKey(key);
}
