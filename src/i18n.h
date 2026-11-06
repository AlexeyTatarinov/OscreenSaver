#pragma once

// Interface strings in three languages: English, Ukrainian, Spanish.
// The language is chosen in the settings; English is the default.
enum class Str {
    AppName,
    TrayTooltip,
    MenuCapture,
    MenuAutostart,
    MenuExit,
    AlreadyRunning,
    HookFailed,
    HintSelect,
    HintPickMonitor,
    SavedTitle,
    SavedText,
    SaveFailed,
    TipArea, TipPencil, TipMarker, TipBrush, TipArrow, TipRect, TipEllipse, TipText, TipEraser,
    TipColor, TipSize, TipUndo, TipRedo, TipSave, TipClose,
    TipCustomColor, TipBold, TipItalic, TipStrike, TipFont,
    TipSelect, TipEraseWhole, TipErasePart,
    TipBlur, TipAddArea, HintNextArea,
    TipFrames,
    MenuSettings, SetTitle, SetGroupCapture, SetHotkey, SetHotkeyCustom, SetHotkeyHint, SetMagnifier,
    SetGroupCompare, SetBackground, SetFrames, SetGroupGeneral, SetOk, SetCancel, TrayTooltipFmt,
    PinCopy, PinSave, PinActualSize, PinClose, CopiedTitle, CopiedText, OcrDoneTitle, OcrNone, OcrUnavailable,
    ColorCopied, TipStep, TipPicker, TipCopy, TipPin, TipOcr,
    SetGroupSave, SetFolder, SetBrowse, SetFormat, SetQuality, SetName, SetNameHint, SetClipboardOnly,
    TipLong, LongStatus, LongDoneTitle, LongDoneText, LongFailed, LongManual, LongTooFast, SetSound, SoundShutter, SoundClick, SoundChime, SoundFile, SoundFilter, SoundGlass, SoundBirds, SetLanguage, SetHotkeyLocked, SetHotkeyPress, TipLine,
    Count
};

// The numbers are kept in the settings.
enum class Lang : int { En = 0, Uk = 1, Es = 2, Count };

// Reads the language from the settings (call after LoadConfig and whenever the settings change).
void InitLanguage();
Lang CurrentLanguage();
const wchar_t* LanguageName(Lang l);  // in its own language, e.g. "Українська"
const wchar_t* T(Str id);
