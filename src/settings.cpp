#include "settings.h"

#include <commctrl.h>
#include <commdlg.h>

#include <string>

#include "autostart.h"
#include "config.h"
#include "i18n.h"

namespace {

const wchar_t kClass[] = L"OscreenSaver.Settings";

enum Id : int {
    IdHotkeyList = 200,
    IdHotkeyEdit,
    IdMagnifier,
    IdBackground,
    IdFrames,
    IdAutostart,
};

// Popular screenshot shortcuts (as in other screenshot tools); the last line is a custom one.
struct Preset {
    UINT vk;
    UINT mods;
};
const Preset kPresets[] = {
    {VK_SNAPSHOT, 0},
    {VK_SNAPSHOT, HOTKEYF_CONTROL},
    {VK_SNAPSHOT, HOTKEYF_SHIFT},
    {'S', HOTKEYF_CONTROL | HOTKEYF_SHIFT},
    {'X', HOTKEYF_CONTROL | HOTKEYF_SHIFT},
    {'S', HOTKEYF_CONTROL | HOTKEYF_ALT},
    {'S', HOTKEYF_ALT},
    {VK_F12, HOTKEYF_CONTROL},
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

HWND g_window = nullptr;
HWND g_owner = nullptr;
UINT g_changedMessage = 0;
HFONT g_font = nullptr;
COLORREF g_background = 0;
COLORREF g_customColors[16] = {};
float g_scale = 1.f;

int S(int v) { return static_cast<int>(static_cast<float>(v) * g_scale + 0.5f); }

HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
}

bool Checked(int id) { return IsDlgButtonChecked(g_window, id) == BST_CHECKED; }

void UpdateHotkeyEdit() {
    const int sel = static_cast<int>(SendDlgItemMessageW(g_window, IdHotkeyList, CB_GETCURSEL, 0, 0));
    EnableWindow(GetDlgItem(g_window, IdHotkeyEdit), sel == kPresetCount);
}

void Build(HWND w) {
    const Config& c = Cfg();
    const int left = 12, width = 336, inner = 24, field = 150, fieldW = 186;

    // Screenshot.
    Add(w, L"BUTTON", T(Str::SetGroupCapture), BS_GROUPBOX, left, 8, width, 150, 0);
    Add(w, L"STATIC", T(Str::SetHotkey), SS_LEFT, inner, 34, 124, 20, 0);
    HWND list = Add(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, field, 30, fieldW, 240,
                    IdHotkeyList);
    int selected = kPresetCount;
    for (int i = 0; i < kPresetCount; ++i) {
        SendMessageW(list, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(HotkeyName(kPresets[i].vk, kPresets[i].mods).c_str()));
        if (kPresets[i].vk == c.hotkeyVk && kPresets[i].mods == c.hotkeyMods) selected = i;
    }
    SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(Str::SetHotkeyCustom)));
    SendMessageW(list, CB_SETCURSEL, selected, 0);
    Add(w, L"STATIC", T(Str::SetHotkeyHint), SS_LEFT, inner, 64, width - 24, 34, 0);
    HWND edit = Add(w, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER, field, 98, fieldW, 24, IdHotkeyEdit);
    if (selected == kPresetCount) {
        SendMessageW(edit, HKM_SETHOTKEY, MAKEWORD(c.hotkeyVk, c.hotkeyMods), 0);
    }
    Add(w, L"BUTTON", T(Str::SetMagnifier), BS_AUTOCHECKBOX | WS_TABSTOP, inner, 128, width - 24, 22, IdMagnifier);
    CheckDlgButton(w, IdMagnifier, c.magnifier ? BST_CHECKED : BST_UNCHECKED);

    // Several areas.
    Add(w, L"BUTTON", T(Str::SetGroupCompare), BS_GROUPBOX, left, 166, width, 88, 0);
    Add(w, L"STATIC", T(Str::SetBackground), SS_LEFT, inner, 194, 124, 20, 0);
    Add(w, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, field, 189, 64, 26, IdBackground);
    Add(w, L"BUTTON", T(Str::SetFrames), BS_AUTOCHECKBOX | WS_TABSTOP, inner, 224, width - 24, 22, IdFrames);
    CheckDlgButton(w, IdFrames, c.frames ? BST_CHECKED : BST_UNCHECKED);

    // General.
    Add(w, L"BUTTON", T(Str::SetGroupGeneral), BS_GROUPBOX, left, 262, width, 54, 0);
    Add(w, L"BUTTON", T(Str::MenuAutostart), BS_AUTOCHECKBOX | WS_TABSTOP, inner, 284, width - 24, 22, IdAutostart);
    CheckDlgButton(w, IdAutostart, IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);

    Add(w, L"BUTTON", T(Str::SetOk), BS_DEFPUSHBUTTON | WS_TABSTOP, 182, 328, 80, 28, IDOK);
    Add(w, L"BUTTON", T(Str::SetCancel), BS_PUSHBUTTON | WS_TABSTOP, 268, 328, 80, 28, IDCANCEL);

    g_background = c.background;
    UpdateHotkeyEdit();
}

void Apply() {
    Config& c = Cfg();
    const int sel = static_cast<int>(SendDlgItemMessageW(g_window, IdHotkeyList, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < kPresetCount) {
        c.hotkeyVk = kPresets[sel].vk;
        c.hotkeyMods = kPresets[sel].mods;
    } else {
        const WORD hk = static_cast<WORD>(SendDlgItemMessageW(g_window, IdHotkeyEdit, HKM_GETHOTKEY, 0, 0));
        if (LOBYTE(hk) != 0) {  // an empty field keeps the previous shortcut
            c.hotkeyVk = LOBYTE(hk);
            c.hotkeyMods = HIBYTE(hk) & (HOTKEYF_SHIFT | HOTKEYF_CONTROL | HOTKEYF_ALT);
        }
    }
    c.magnifier = Checked(IdMagnifier);
    c.frames = Checked(IdFrames);
    c.background = g_background;
    SaveConfig();
    SetAutostart(Checked(IdAutostart));
    if (g_owner) PostMessageW(g_owner, g_changedMessage, 0, 0);
}

void PickBackground() {
    CHOOSECOLORW cc = {sizeof(cc)};
    cc.hwndOwner = g_window;
    cc.rgbResult = g_background;
    cc.lpCustColors = g_customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) {
        g_background = cc.rgbResult;
        InvalidateRect(GetDlgItem(g_window, IdBackground), nullptr, TRUE);
    }
}

// Colour button: the chosen colour with a small palette mark.
void DrawSwatch(const DRAWITEMSTRUCT* d) {
    RECT r = d->rcItem;
    DrawFrameControl(d->hDC, &r, DFC_BUTTON, DFCS_BUTTONPUSH | ((d->itemState & ODS_SELECTED) ? DFCS_PUSHED : 0));
    InflateRect(&r, -S(5), -S(5));
    HBRUSH fill = CreateSolidBrush(g_background);
    FillRect(d->hDC, &r, fill);
    DeleteObject(fill);
    FrameRect(d->hDC, &r, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    if (d->itemState & ODS_FOCUS) {
        RECT f = d->rcItem;
        InflateRect(&f, -S(3), -S(3));
        DrawFocusRect(d->hDC, &f);
    }
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IdHotkeyList:
                    if (HIWORD(wp) == CBN_SELCHANGE) UpdateHotkeyEdit();
                    return 0;
                case IdBackground:
                    PickBackground();
                    return 0;
                case IDOK:
                    Apply();
                    DestroyWindow(w);
                    return 0;
                case IDCANCEL:
                    DestroyWindow(w);
                    return 0;
            }
            break;
        case WM_DRAWITEM:
            if (wp == IdBackground) {
                DrawSwatch(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
                return TRUE;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CLOSE:
            DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            g_window = nullptr;
            if (g_font) DeleteObject(g_font);
            g_font = nullptr;
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

bool IsSettingsOpen() { return g_window != nullptr; }

bool SettingsDialogMessage(MSG* msg) {
    return g_window && IsDialogMessageW(g_window, msg);
}

void ShowSettings(HINSTANCE instance, HWND owner, UINT changedMessage) {
    if (g_window) {
        SetForegroundWindow(g_window);
        return;
    }
    g_owner = owner;
    g_changedMessage = changedMessage;

    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }

    // Size and font follow the scale of the screen with the mouse.
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    UINT dpi = GetDpiForSystem();
    g_scale = dpi / 96.f;
    NONCLIENTMETRICSW ncm = {sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT frame = {0, 0, S(360), S(368)};
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
    const int w = frame.right - frame.left, h = frame.bottom - frame.top;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    const RECT& wa = mi.rcWork;
    const int x = wa.left + (wa.right - wa.left - w) / 2;
    const int y = wa.top + (wa.bottom - wa.top - h) / 2;

    g_window = CreateWindowExW(0, kClass, T(Str::SetTitle), style, x, y, w, h, nullptr, nullptr, instance, nullptr);
    if (!g_window) return;
    Build(g_window);
    ShowWindow(g_window, SW_SHOW);
    SetForegroundWindow(g_window);
    SetFocus(GetDlgItem(g_window, IdHotkeyList));
}
