#include "i18n.h"

#include <windows.h>

namespace {

enum Lang { En = 0, Ru = 1, Uk = 2 };
Lang g_lang = En;

// Columns: English, Russian, Ukrainian.
const wchar_t* const kStrings[][3] = {
    /* AppName */        {L"OscreenSaver", L"OscreenSaver", L"OscreenSaver"},
    /* TrayTooltip */    {L"OscreenSaver — press PrintScreen",
                          L"OscreenSaver — нажмите PrintScreen",
                          L"OscreenSaver — натисніть PrintScreen"},
    /* MenuCapture */    {L"Take screenshot\tPrintScreen",
                          L"Сделать снимок\tPrintScreen",
                          L"Зробити знімок\tPrintScreen"},
    /* MenuAutostart */  {L"Start with Windows",
                          L"Запускать вместе с Windows",
                          L"Запускати разом із Windows"},
    /* MenuExit */       {L"Exit", L"Выход", L"Вихід"},
    /* AlreadyRunning */ {L"OscreenSaver is already running.",
                          L"OscreenSaver уже запущен.",
                          L"OscreenSaver вже запущено."},
    /* HookFailed */     {L"Could not register the PrintScreen key.",
                          L"Не удалось подключить клавишу PrintScreen.",
                          L"Не вдалося підключити клавішу PrintScreen."},
    /* HintSelect */     {L"Select an area with the mouse  •  Enter — save  •  Esc — cancel",
                          L"Выделите область мышью  •  Enter — сохранить  •  Esc — отмена",
                          L"Виділіть область мишею  •  Enter — зберегти  •  Esc — скасувати"},
    /* HintPickMonitor */{L"Right-click to choose this monitor",
                          L"Нажмите правую кнопку мыши, чтобы выбрать этот монитор",
                          L"Натисніть праву кнопку миші, щоб вибрати цей монітор"},
    /* SavedTitle */     {L"Screenshot saved and copied",
                          L"Снимок сохранён и скопирован",
                          L"Знімок збережено та скопійовано"},
    /* SavedText */      {L"Click to open the folder.",
                          L"Нажмите, чтобы открыть папку.",
                          L"Натисніть, щоб відкрити теку."},
    /* SaveFailed */     {L"Could not save the screenshot.",
                          L"Не удалось сохранить снимок.",
                          L"Не вдалося зберегти знімок."},
};

}  // namespace

void InitLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_UKRAINIAN: g_lang = Uk; break;
        case LANG_RUSSIAN:   g_lang = Ru; break;
        default:             g_lang = En; break;
    }
}

const wchar_t* T(Str id) {
    return kStrings[static_cast<int>(id)][g_lang];
}
