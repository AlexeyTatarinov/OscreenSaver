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
    TipLong, LongStatus, LongDoneTitle, LongDoneText, LongFailed,
};

void InitLanguage();
const wchar_t* T(Str id);
