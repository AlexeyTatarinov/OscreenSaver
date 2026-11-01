#pragma once

// Interface strings in three languages: Russian, Ukrainian, English.
// The language follows the Windows interface language; anything else falls back to English.
enum class Str {
    AppName,
    TrayTooltip,
    MenuCapture,
    MenuAutostart,
    MenuExit,
    AlreadyRunning,
    HookFailed,
};

void InitLanguage();
const wchar_t* T(Str id);
