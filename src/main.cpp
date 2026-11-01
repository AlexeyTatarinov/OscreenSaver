// OscreenSaver - screenshot tool for Windows.
// Copyright (C) 2026 Alexey Tatarinov. All rights reserved.
//
// Runs in the background with no visible window. Pressing PrintScreen freezes the monitor
// under the cursor. While idle the program waits on its message queue and uses no CPU.

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "app.h"
#include "autostart.h"
#include "i18n.h"
#include "overlay.h"

namespace {

const wchar_t kMainClass[] = L"OscreenSaver.Main";
const wchar_t kMutexName[] = L"Local\\OscreenSaver.SingleInstance";

constexpr UINT WM_APP_CAPTURE = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_OVERLAY_KEY = WM_APP + 3;
constexpr UINT kTrayId = 1;

enum MenuId : UINT { IdCapture = 100, IdAutostart, IdExit };

HINSTANCE g_instance = nullptr;
HWND g_main = nullptr;
HHOOK g_keyboardHook = nullptr;
UINT g_taskbarCreated = 0;
std::wstring g_lastSaved;  // opened when the user clicks the "saved" notice

// Low-level keyboard hook: catches PrintScreen system-wide and stops Windows from handling it
// (otherwise Windows 11 may open its own Snipping Tool). Only a message is posted here, so the
// hook returns instantly and never slows down typing.
LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        if (key->vkCode == VK_SNAPSHOT) {
            if (down) {
                PostMessageW(g_main, WM_APP_CAPTURE, 0, 0);
            }
            return 1;  // swallow both key-down and key-up
        }
        if ((key->vkCode == VK_ESCAPE || key->vkCode == VK_RETURN) && IsOverlayOpen()) {
            if (down) {
                PostMessageW(g_main, WM_APP_OVERLAY_KEY, key->vkCode, 0);
            }
            return 1;  // the key belongs to the frozen screen, not to the program underneath
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wp, lp);
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = g_main;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);  // TODO: own icon
    lstrcpynW(nid.szTip, T(Str::TrayTooltip), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = g_main;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowNotice(const wchar_t* title, const wchar_t* text, DWORD icon) {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = g_main;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = icon | NIIF_NOSOUND;
    lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void OpenSavedFolder() {
    if (g_lastSaved.empty()) return;
    const std::wstring args = L"/select,\"" + g_lastSaved + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IdCapture, T(Str::MenuCapture));
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED), IdAutostart,
                T(Str::MenuAutostart));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdExit, T(Str::MenuExit));

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_main);  // required so the menu closes when clicking elsewhere
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0,
                                    g_main, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case IdCapture: PostMessageW(g_main, WM_APP_CAPTURE, 0, 0); break;
        case IdAutostart: SetAutostart(!IsAutostartEnabled()); break;
        case IdExit: DestroyWindow(g_main); break;
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_CAPTURE:
            OpenOverlay(g_instance);
            return 0;
        case WM_APP_OVERLAY_KEY:
            OverlayKey(static_cast<unsigned>(wp));
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                ShowTrayMenu();
            } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
                PostMessageW(hwnd, WM_APP_CAPTURE, 0, 0);
            } else if (LOWORD(lp) == NIN_BALLOONUSERCLICK) {
                OpenSavedFolder();
            }
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    if (msg == g_taskbarCreated && g_taskbarCreated != 0) {
        AddTrayIcon();  // Explorer restarted: put the icon back
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void NotifySaved(const std::wstring& path) {
    g_lastSaved = path;
    ShowNotice(T(Str::SavedTitle), T(Str::SavedText), NIIF_INFO);
}

void NotifySaveFailed() {
    ShowNotice(T(Str::AppName), T(Str::SaveFailed), NIIF_ERROR);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    InitLanguage();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, T(Str::AlreadyRunning), T(Str::AppName), MB_OK | MB_ICONINFORMATION);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    ApplyFirstRunAutostart();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // needed for JPG encoding

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
    RegisterOverlayClass(instance);

    // Hidden window: receives tray clicks and capture requests. Never shown.
    g_main = CreateWindowExW(0, kMainClass, L"OscreenSaver", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                             instance, nullptr);
    if (!g_main) {
        return 1;
    }
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, instance, 0);
    if (!g_keyboardHook) {
        MessageBoxW(nullptr, T(Str::HookFailed), T(Str::AppName), MB_OK | MB_ICONERROR);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
