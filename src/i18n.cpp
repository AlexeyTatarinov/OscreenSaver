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
    /* TipArea */ {L"Select area (Tab)", L"Выделение области (Tab)", L"Виділення області (Tab)"},
    /* TipPencil */ {L"Pencil (Tab)", L"Карандаш (Tab)", L"Олівець (Tab)"},
    /* TipMarker */ {L"Marker", L"Маркер", L"Маркер"},
    /* TipBrush */ {L"Brush", L"Кисть", L"Пензель"},
    /* TipArrow */ {L"Arrow", L"Стрелка", L"Стрілка"},
    /* TipRect */ {L"Rectangle", L"Прямоугольник", L"Прямокутник"},
    /* TipEllipse */ {L"Ellipse", L"Эллипс", L"Еліпс"},
    /* TipText */ {L"Text", L"Текст", L"Текст"},
    /* TipEraser */ {L"Eraser", L"Ластик", L"Гумка"},
    /* TipColor */ {L"Colour", L"Цвет", L"Колір"},
    /* TipSize */ {L"Line size", L"Толщина", L"Товщина"},
    /* TipUndo */ {L"Undo (Ctrl+Z)", L"Отменить (Ctrl+Z)", L"Скасувати (Ctrl+Z)"},
    /* TipRedo */ {L"Redo (Ctrl+Y)", L"Вернуть (Ctrl+Y)", L"Повернути (Ctrl+Y)"},
    /* TipSave */ {L"Save and copy (Enter)", L"Сохранить и скопировать (Enter)", L"Зберегти та скопіювати (Enter)"},
    /* TipClose */ {L"Close without saving (Esc)", L"Закрыть без сохранения (Esc)", L"Закрити без збереження (Esc)"},
    /* TipCustomColor */ {L"Any colour…", L"Другой цвет…", L"Інший колір…"},
    /* TipBold */ {L"Bold (Ctrl+B)", L"Жирный (Ctrl+B)", L"Жирний (Ctrl+B)"},
    /* TipItalic */ {L"Italic (Ctrl+I)", L"Курсив (Ctrl+I)", L"Курсив (Ctrl+I)"},
    /* TipStrike */ {L"Strikethrough", L"Зачёркнутый", L"Закреслений"},
    /* TipFont */ {L"Font and size…", L"Шрифт и размер…", L"Шрифт і розмір…"},
    /* TipSelect */ {L"Select: move, resize; double-click a text to edit it", L"Выбор: перемещение, размер; двойной щелчок по тексту — правка", L"Вибір: переміщення, розмір; подвійний клік по тексту — редагування"},
    /* TipEraseWhole */ {L"Erase whole objects", L"Стирать объекты целиком", L"Стирати об’єкти цілком"},
    /* TipErasePart */ {L"Erase parts (like Paint)", L"Стирать частично (как в Paint)", L"Стирати частково (як у Paint)"},
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
