# OscreenSaver

**Author: Alexey Tatarinov**

OscreenSaver is a lightweight screenshot tool for Windows 10 and 11 (x64). Press **PrintScreen**, select an
area, mark it up and save it in a few seconds. The program runs quietly in the background, uses almost no
memory or processor time, and needs no installation.

Interface languages: **English** (default), **Ukrainian**, **Spanish** — chosen in the settings.

## What it is for

- Quickly capturing part of the screen to send in a chat, an e-mail or a bug report.
- Pointing at things: arrows, frames, numbered markers, text notes.
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
- Pencil, marker (semi-transparent), thick brush, arrow, rectangle, ellipse.
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

## Keyboard shortcuts

| Key | Action |
|---|---|
| PrintScreen | Take a screenshot (can be changed in the settings) |
| Enter / Ctrl+S | Save and copy |
| Ctrl+C | Copy to the clipboard only |
| Esc | Close without saving |
| Tab | Switch between area selection and the last drawing tool |
| Ctrl + drag | Add one more area |
| Ctrl+A | Select the whole screen |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Delete | Delete the selected object |
| Ctrl+B / Ctrl+I | Bold / italic text |
| Enter / Shift+Enter (in text) | Finish the text / new line |

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

1. Open the **Actions** tab of this repository.
2. Choose the latest successful **Build** run (green check mark).
3. Download **OscreenSaver** from the **Artifacts** section at the bottom of the page.
4. Unpack the archive and run `OscreenSaver.exe`. No installation is needed.

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

**Alexey Tatarinov** — idea, design and requirements.

Copyright © 2026 Alexey Tatarinov. All rights reserved.
