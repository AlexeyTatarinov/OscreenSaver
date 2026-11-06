# OscreenSaver

**Author: Oleksii Tatarinov**

OscreenSaver is a lightweight screenshot tool for Windows 10 and 11 (x64). Press **PrintScreen**, select an
area, mark it up and save it in a few seconds. The program runs quietly in the background, uses almost no
memory or processor time, and needs no installation.

Interface languages: **English** (default), **Ukrainian**, **Spanish** — chosen in the settings.

## What it is for

- Quickly capturing part of the screen to send in a chat, an e-mail or a bug report.
- Pointing at things: arrows, lines, frames, numbered markers, text notes.
- Hiding private data (passwords, names, account numbers) with blur before sharing.
- Comparing several parts of the screen in one picture.
- Capturing a whole long web page or document that does not fit on the screen.
- Copying text or a colour from anything visible on the screen.

## How to use it

1. Press **PrintScreen** (or the shortcut chosen in the settings). The screen freezes.
2. Drag with the mouse to select an area. A click on a window selects exactly that window;
   a click on an empty place selects the whole screen.
3. Draw on the selection with the tools on the panel next to it.
4. Press **Enter** to save the picture and copy it to the clipboard, or **Esc** to close without saving.

A notice appears after saving; click it to open the folder with the file.

## Features

**Capture**
- Freezes the monitor under the mouse cursor; everything outside the selection is covered by a grey mask.
- Window snapping: the window under the cursor is outlined and selected with one click.
- Magnifier next to the cursor with pixel coordinates and colour (optional).
- Several areas in one picture: hold **Ctrl** and drag (or use the "one more area" button) to add up to
  4 areas, from one or different monitors. They are joined side by side on a black background
  (colour and thin frames are configurable).
- **Long screenshot with scrolling**: select the part of a window that scrolls and press the
  long-screenshot button. The program scrolls the window and stitches the pictures together. If a
  program does not react to automatic scrolling, turn the mouse wheel yourself. **Enter** or **Esc**
  finishes.

**Drawing tools**
- Pencil, marker (semi-transparent), thick brush, straight line, arrow, rectangle, ellipse.
  Hold **Shift** while drawing a line or an arrow to keep it horizontal, vertical or at 45°.
- Text with mixed styles in one line: bold, italic, strikethrough, colour, size and any Windows font;
  a text cursor for editing.
- Blur for hiding private data.
- Numbered markers 1, 2, 3…
- Eyedropper: copies the colour under the cursor as #RRGGBB.
- Eraser with two modes: remove whole objects, or erase parts like in Paint.
- Selection tool: move, resize and delete drawn objects; click text to edit it.
- Colours: green, red, white, black and any custom colour; three line sizes.
- Unlimited undo and redo.

**After the capture**
- Save to a file (JPG or PNG) and copy to the clipboard, or copy only.
- Pin the picture on top of all windows (drag, zoom with the wheel).
- Copy the text from the picture (Windows text recognition, works offline).
- A sound after saving: camera shutter, soft click, chime, breaking glass, birds flying off,
  or your own WAV file.

## Keyboard shortcuts and mouse actions

All shortcuts below are the defaults. Only the screenshot key can be changed (Settings → Hotkey).

**Starting a screenshot**

| Keys / mouse | Action |
|---|---|
| PrintScreen | Freeze the screen and start a screenshot |
| Double-click the tray icon | Start a screenshot |
| Right-click the tray icon | Menu: take a screenshot, Settings, Exit |
| Click the "Saved" notice | Open the folder with the saved file |

**Choosing the area** (frozen screen)

| Keys / mouse | Action |
|---|---|
| Drag with the left mouse button | Select an area |
| Click on a window | Select exactly that window |
| Click on an empty place | Select the whole screen |
| Ctrl+A | Select the whole screen |
| Drag the frame or its handles | Move or resize the selected area |
| Ctrl + drag | Add one more area (up to 4); they are joined side by side into one picture |
| Click an added area | Make it active to move or resize it |
| Right-click | Clear the selection and choose again |
| Right-click a monitor | Choose this monitor (when the program asks which monitor to capture) |
| Tab | Switch between area selection and the last drawing tool |

**Finishing**

| Keys / mouse | Action |
|---|---|
| Enter | Save to a file and copy to the clipboard (only copy, if "clipboard only" is on in the settings) |
| Ctrl+S | Save to a file and copy to the clipboard |
| Double-click inside the area | Save to a file and copy to the clipboard |
| Ctrl+C | Copy to the clipboard only, without saving a file |
| Esc | Close without saving |

**Drawing**

| Keys / mouse | Action |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+Y or Ctrl+Shift+Z | Redo |
| Shift while drawing a line or an arrow | Keep it horizontal, vertical or at 45° |
| Delete | Delete the object picked with the selection tool |

**Typing text** (Text tool)

| Keys | Action |
|---|---|
| Enter | Finish the text |
| Shift+Enter | New line |
| Esc | Cancel the text being typed |
| Ctrl+B / Ctrl+I | Bold / italic for the letters typed next |
| ← → ↑ ↓, Home, End | Move the text cursor |
| Backspace / Delete | Delete the letter before / after the cursor |
| Ctrl+Z | Undo |

**Long screenshot with scrolling**

| Keys / mouse | Action |
|---|---|
| Mouse wheel down | Scroll the window yourself, if it does not scroll automatically |
| Enter or Esc | Finish and keep what has been captured |

**Picture pinned on top of the screen**

| Keys / mouse | Action |
|---|---|
| Drag with the mouse | Move the picture |
| Mouse wheel | Zoom in / out |
| Ctrl+C | Copy the picture |
| Right-click | Menu: copy, save to file, actual size, close |
| Double-click or Esc | Close the picture |

**Settings window — own shortcut field**

| Keys | Action |
|---|---|
| Any combination, e.g. Ctrl+Shift+S | Becomes the new screenshot key |
| Backspace / Delete | Clear the field |

## Settings

Open the settings from the menu of the tray icon (next to the clock) → **Settings…**

- Screenshot shortcut: popular presets or any custom combination.
- Magnifier on or off.
- Save folder, file format (JPG/PNG), JPG quality, file name template with date and time.
- Copy to the clipboard only, without saving files.
- Background colour and frames for pictures joined from several areas.
- Interface language: English, Ukrainian, Spanish.
- Start with Windows.
- Sound after saving.

Settings are stored in the Windows registry under `HKCU\Software\OscreenSaver`.

## Getting the program

1. Open the [**Releases**](https://github.com/AlexeyTatarinov/OscreenSaver/releases/latest) page of this repository.
2. Download `OscreenSaver.exe`.
3. Run it. No installation is needed; the icon appears next to the clock.

To check that the file is genuine, compare its SHA-256 checksum with the one shown on the release page:
open PowerShell in the download folder and run `Get-FileHash OscreenSaver.exe`.

## Building from source

Requirements: Visual Studio 2022 (C++ desktop workload) and CMake 3.20 or newer.

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

The program is written in C++ with the Win32 API, GDI+, WIC and Windows.Media.Ocr; it has no external
dependencies.

## Credits

- Breaking glass sound: "glass shatter.wav" by datasoundsample, Freesound, licensed under CC BY 4.0.
- Birds sound: wing sounds from the *Ninja Adventure* asset pack by Pixel-boy & AAA, CC0.

Details are in [`res/sounds/CREDITS.md`](res/sounds/CREDITS.md).

## Author

**Oleksii Tatarinov** — idea, design and requirements.

## License

OscreenSaver is released under the [MIT License](LICENSE). You may use, copy, change and share it freely,
as long as the copyright notice with the author's name is kept.

Copyright © 2026 Oleksii Tatarinov.
The two recorded sounds keep their own licences (see Credits).
