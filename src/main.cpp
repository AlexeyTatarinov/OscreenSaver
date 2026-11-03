// OscreenSaver - screenshot tool for Windows.
// Copyright (C) 2026 Alexey Tatarinov. All rights reserved.
//
// Runs in the background with no visible window. Pressing PrintScreen freezes the monitor
// under the cursor. While idle the program waits on its message queue and uses no CPU.

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <commctrl.h>

#include <cwchar>
#include <string>

#include "annotate.h"
#include "app.h"
#include "autostart.h"
#include "config.h"
#include "i18n.h"
#include "longshot.h"
#include "ocr.h"
#include "overlay.h"
#include "save.h"
#include "settings.h"
#include "sound.h"

namespace {

const wchar_t kMainClass[] = L"OscreenSaver.Main";
const wchar_t kMutexName[] = L"Local\\OscreenSaver.SingleInstance";

constexpr UINT WM_APP_CAPTURE = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_OVERLAY_KEY = WM_APP + 3;
constexpr UINT WM_APP_SETTINGS_CHANGED = WM_APP + 4;
constexpr UINT WM_APP_OCR_DONE = WM_APP + 5;
constexpr UINT WM_APP_LONG_DONE = WM_APP + 6;
constexpr UINT kTrayId = 1;

enum MenuId : UINT { IdCapture = 100, IdSettings, IdExit };

HINSTANCE g_instance = nullptr;
HWND g_main = nullptr;
HHOOK g_keyboardHook = nullptr;
UINT g_taskbarCreated = 0;
std::wstring g_lastSaved;  // opened when the user clicks the "saved" notice
bool g_hotkeySwallowed = false;  // the key-down of the screenshot key was taken: take its key-up too

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// True if Ctrl, Shift and Alt are pressed exactly as in the chosen shortcut.
bool ModifiersMatch(UINT mods) {
    return KeyDown(VK_CONTROL) == ((mods & HOTKEYF_CONTROL) != 0) && KeyDown(VK_SHIFT) == ((mods & HOTKEYF_SHIFT) != 0) &&
           KeyDown(VK_MENU) == ((mods & HOTKEYF_ALT) != 0);
}

// Low-level keyboard hook: catches the screenshot shortcut (PrintScreen by default) system-wide and
// stops other programs from handling it (otherwise Windows 11 may open its own Snipping Tool).
// Only a message is posted here, so the hook returns instantly and never slows down typing.
LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && !IsSettingsOpen()) {  // in the settings window the shortcut is being typed
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        if ((key->vkCode == VK_ESCAPE || key->vkCode == VK_RETURN) && IsLongShotRunning()) {
            if (wp == WM_KEYDOWN) StopLongShot();  // finish and keep what was captured so far
            return 1;
        }
        const Config& cfg = Cfg();
        if (key->vkCode == cfg.hotkeyVk) {
            if (down && ModifiersMatch(cfg.hotkeyMods)) {
                g_hotkeySwallowed = true;
                PostMessageW(g_main, WM_APP_CAPTURE, 0, 0);
                return 1;
            }
            if (!down && g_hotkeySwallowed) {
                g_hotkeySwallowed = false;
                return 1;  // the matching key-up
            }
        }
        if ((key->vkCode == VK_ESCAPE || key->vkCode == VK_RETURN) && IsOverlayOpen() && !IsOverlayTyping()) {
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
    // Own icon in the size Windows uses next to the clock.
    static HICON icon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXSMICON),
                                                      GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    nid.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    wchar_t tip[128];
    swprintf(tip, 128, T(Str::TrayTooltipFmt), HotkeyName(Cfg().hotkeyVk, Cfg().hotkeyMods).c_str());
    lstrcpynW(nid.szTip, tip, ARRAYSIZE(nid.szTip));
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) Shell_NotifyIconW(NIM_ADD, &nid);
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

void CopyTextToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    }
    CloseClipboard();
}

// The long picture is saved and/or copied like any screenshot.
void FinishLongScreenshot(LongShotResult* r) {
    if (!r) return;
    if (!r->scrolled || r->width <= 0 || r->height <= 0) {
        ShowNotice(T(Str::AppName), T(Str::LongFailed), NIIF_WARNING);
        delete r;
        return;
    }
    Snapshot picture;
    if (!CreateBlankSnapshot(r->width, r->height, picture)) {
        NotifySaveFailed();
        delete r;
        return;
    }
    memcpy(picture.pixels, r->pixels.data(), r->pixels.size() * 4);
    const RECT all = {0, 0, r->width, r->height};
    wchar_t size[64];
    swprintf(size, 64, T(Str::LongDoneText), r->width, r->height);
    delete r;
    CopyAreaToClipboard(picture, all, g_main);
    if (Cfg().clipboardOnly) {
        g_lastSaved.clear();
        ShowNotice(T(Str::LongDoneTitle), size, NIIF_INFO);
        PlaySaveSound();
    } else {
        std::wstring path;
        if (SaveArea(picture, all, path)) {
            g_lastSaved = path;
            ShowNotice(T(Str::LongDoneTitle), size, NIIF_INFO);
            PlaySaveSound();
        } else {
            NotifySaveFailed();
        }
    }
    FreeSnapshot(picture);
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    const std::wstring capture = std::wstring(T(Str::MenuCapture)) + L"\t" +
                                 HotkeyName(Cfg().hotkeyVk, Cfg().hotkeyMods);
    AppendMenuW(menu, MF_STRING, IdCapture, capture.c_str());
    AppendMenuW(menu, MF_STRING, IdSettings, T(Str::MenuSettings));
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
        case IdSettings: ShowSettings(g_instance, g_main, WM_APP_SETTINGS_CHANGED); break;
        case IdExit: DestroyWindow(g_main); break;
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_CAPTURE:
            if (!IsLongShotRunning()) OpenOverlay(g_instance);
            return 0;
        case WM_APP_LONG_DONE:
            FinishLongScreenshot(reinterpret_cast<LongShotResult*>(lp));
            return 0;
        case WM_APP_OCR_DONE: {
            std::wstring* text = reinterpret_cast<std::wstring*>(lp);
            if (!wp) {
                ShowNotice(T(Str::AppName), T(Str::OcrUnavailable), NIIF_WARNING);
            } else if (!text || text->empty()) {
                ShowNotice(T(Str::AppName), T(Str::OcrNone), NIIF_INFO);
            } else {
                CopyTextToClipboard(*text);
                g_lastSaved.clear();
                const std::wstring preview = text->size() > 120 ? text->substr(0, 120) + L"…" : *text;
                ShowNotice(T(Str::OcrDoneTitle), preview.c_str(), NIIF_INFO);
            }
            delete text;
            return 0;
        }
        case WM_APP_SETTINGS_CHANGED:
            AddTrayIcon();  // the hint shows the new shortcut
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
    PlaySaveSound();
}

void NotifyCopied() {
    g_lastSaved.clear();  // nothing to open on a click
    ShowNotice(T(Str::CopiedTitle), T(Str::CopiedText), NIIF_INFO);
    PlaySaveSound();
}

void StartTextRecognition(Snapshot picture) {
    RecognizeText(picture, g_main, WM_APP_OCR_DONE);
}

void StartLongScreenshot(const RECT& screenArea) {
    StartLongShot(screenArea, g_main, WM_APP_LONG_DONE);
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

    LoadConfig();
    ApplyFirstRunAutostart();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // needed for JPG encoding
    StartGraphics();                                    // smooth (anti-aliased) drawing

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
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
        if (SettingsDialogMessage(&msg)) continue;  // Tab, Enter and Esc in the settings window
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
    StopGraphics();
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
