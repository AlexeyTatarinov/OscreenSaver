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
    /* MenuCapture */    {L"Take screenshot",
                          L"Сделать снимок",
                          L"Зробити знімок"},
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
    /* HintSelect */     {L"Select an area or click a window  •  Ctrl + mouse — one more area  •  Enter — save  •  Esc — cancel",
                          L"Выделите область или щёлкните по окну  •  Ctrl + мышь — ещё область  •  Enter — сохранить  •  Esc — отмена",
                          L"Виділіть область або клацніть по вікну  •  Ctrl + миша — ще область  •  Enter — зберегти  •  Esc — скасувати"},
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
    /* TipBlur */ {L"Blur: drag over what to hide; size buttons = strength", L"Размытие: обведите то, что нужно скрыть; толщина = сила размытия", L"Розмиття: обведіть те, що треба приховати; товщина = сила розмиття"},
    /* TipAddArea */ {L"One more area (or Ctrl + mouse): pictures are joined side by side (up to 4)", L"Ещё область (или Ctrl + мышь): картинки склеятся рядом (до 4)", L"Ще область (або Ctrl + миша): картинки склеяться поруч (до 4)"},
    /* HintNextArea */ {L"Select area %d of 4 (Ctrl + mouse)  \u2022  Enter \u2014 save  \u2022  Esc \u2014 cancel", L"Выделите область %d из 4 (Ctrl + мышь)  \u2022  Enter \u2014 сохранить  \u2022  Esc \u2014 отмена", L"Виділіть область %d з 4 (Ctrl + миша)  \u2022  Enter \u2014 зберегти  \u2022  Esc \u2014 скасувати"},
    /* TipFrames */ {L"Thin grey frame around each joined area: on / off", L"Тонкая серая рамка вокруг склеенных областей: вкл / выкл", L"Тонка сіра рамка навколо склеєних областей: увімк / вимк"},
    /* MenuSettings */ {L"Settings…", L"Настройки…", L"Налаштування…"},
    /* SetTitle */ {L"OscreenSaver — Settings", L"OscreenSaver — Настройки", L"OscreenSaver — Налаштування"},
    /* SetGroupCapture */ {L"Screenshot", L"Снимок экрана", L"Знімок екрана"},
    /* SetHotkey */ {L"Hotkey:", L"Горячая клавиша:", L"Гаряча клавіша:"},
    /* SetHotkeyCustom */ {L"Custom combination…", L"Своя комбинация…", L"Своя комбінація…"},
    /* SetHotkeyHint */ {L"For a custom combination choose the last line, click the field below and press the keys.", L"Для своей комбинации выберите последнюю строку, щёлкните поле ниже и нажмите клавиши.", L"Для своєї комбінації виберіть останній рядок, клацніть поле нижче й натисніть клавіші."},
    /* SetMagnifier */ {L"Magnifier next to the cursor (pixels, position, colour)", L"Лупа у курсора (пиксели, координаты, цвет)", L"Лупа біля курсора (пікселі, координати, колір)"},
    /* SetGroupCompare */ {L"Several areas joined into one picture", L"Несколько областей в одной картинке", L"Кілька областей в одній картинці"},
    /* SetBackground */ {L"Background colour:", L"Цвет фона:", L"Колір фону:"},
    /* SetFrames */ {L"Thin grey frame around each area", L"Тонкая серая рамка вокруг областей", L"Тонка сіра рамка навколо областей"},
    /* SetGroupGeneral */ {L"General", L"Общие", L"Загальні"},
    /* SetOk */ {L"OK", L"OK", L"OK"},
    /* SetCancel */ {L"Cancel", L"Отмена", L"Скасувати"},
    /* TrayTooltipFmt */ {L"OscreenSaver — %ls", L"OscreenSaver — %ls", L"OscreenSaver — %ls"},
    /* PinCopy */ {L"Copy\tCtrl+C", L"Копировать\tCtrl+C", L"Копіювати\tCtrl+C"},
    /* PinSave */ {L"Save to file", L"Сохранить в файл", L"Зберегти у файл"},
    /* PinActualSize */ {L"Actual size", L"Исходный размер", L"Початковий розмір"},
    /* PinClose */ {L"Close\tEsc", L"Закрыть\tEsc", L"Закрити\tEsc"},
    /* CopiedTitle */ {L"Screenshot copied to the clipboard", L"Снимок скопирован в буфер обмена", L"Знімок скопійовано в буфер обміну"},
    /* CopiedText */ {L"No file was saved.", L"Файл не сохранялся.", L"Файл не зберігався."},
    /* OcrDoneTitle */ {L"Text copied to the clipboard", L"Текст скопирован в буфер обмена", L"Текст скопійовано в буфер обміну"},
    /* OcrNone */ {L"No text was found in the screenshot.", L"На снимке не найден текст.", L"На знімку не знайдено тексту."},
    /* OcrUnavailable */ {L"Text recognition is not available: Windows has no recognition language installed.", L"Распознавание текста недоступно: в Windows не установлен язык распознавания.", L"Розпізнавання тексту недоступне: у Windows не встановлено мову розпізнавання."},
    /* ColorCopied */ {L"Copied %ls", L"Скопировано %ls", L"Скопійовано %ls"},
    /* TipStep */ {L"Numbered marker 1, 2, 3…", L"Нумерованная метка 1, 2, 3…", L"Нумерована мітка 1, 2, 3…"},
    /* TipPicker */ {L"Eyedropper: copies the colour under the cursor", L"Пипетка: копирует цвет под курсором", L"Піпетка: копіює колір під курсором"},
    /* TipCopy */ {L"Copy only, without saving a file (Ctrl+C)", L"Только копировать, без файла (Ctrl+C)", L"Лише копіювати, без файлу (Ctrl+C)"},
    /* TipPin */ {L"Pin on top of all windows", L"Закрепить поверх всех окон", L"Закріпити поверх усіх вікон"},
    /* TipOcr */ {L"Copy the text from the picture (text recognition)", L"Скопировать текст с картинки (распознавание)", L"Скопіювати текст із картинки (розпізнавання)"},
    /* SetGroupSave */ {L"Saving", L"Сохранение", L"Збереження"},
    /* SetFolder */ {L"Folder:", L"Папка:", L"Тека:"},
    /* SetBrowse */ {L"Browse…", L"Обзор…", L"Огляд…"},
    /* SetFormat */ {L"Format:", L"Формат:", L"Формат:"},
    /* SetQuality */ {L"JPG quality:", L"Качество JPG:", L"Якість JPG:"},
    /* SetName */ {L"File name:", L"Имя файла:", L"Ім’я файлу:"},
    /* SetNameHint */ {L"%Y year, %m month, %d day, %H hour, %M minutes, %S seconds", L"%Y год, %m месяц, %d день, %H час, %M минуты, %S секунды", L"%Y рік, %m місяць, %d день, %H година, %M хвилини, %S секунди"},
    /* SetClipboardOnly */ {L"Do not save files, only copy to the clipboard (Enter)", L"Не сохранять файл, только копировать в буфер (Enter)", L"Не зберігати файл, лише копіювати в буфер (Enter)"},
    /* TipLong */ {L"Long screenshot with scrolling: select the part of a window that scrolls", L"Длинный снимок с прокруткой: выделите прокручиваемую часть окна", L"Довгий знімок із прокручуванням: виділіть частину вікна, що прокручується"},
    /* LongStatus */ {L"Long screenshot: scrolling…  %d px  •  Esc — stop", L"Длинный снимок: прокрутка…  %d px  •  Esc — остановить", L"Довгий знімок: прокручування…  %d px  •  Esc — зупинити"},
    /* LongDoneTitle */ {L"Long screenshot is ready", L"Длинный снимок готов", L"Довгий знімок готовий"},
    /* LongDoneText */ {L"%d × %d px", L"%d × %d px", L"%d × %d px"},
    /* LongFailed */ {L"The window under the area did not scroll with the mouse wheel.", L"Окно под областью не прокручивается колесом мыши.", L"Вікно під областю не прокручується коліщатком миші."},
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
