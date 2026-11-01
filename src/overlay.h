#pragma once

#include <windows.h>

// Full-screen window that shows the frozen image of the monitor.
// Everything outside the selected area is covered by a grey mask.
// Area mode: drag to select, drag inside to move, drag the handles to resize, single click = whole
// screen, right click = clear selection, double click inside = save.
// After the first selection the pencil is switched on. The panel next to the selection holds the
// drawing tools, colours, line sizes, undo/redo, save and close. Tab switches between area mode and
// the last drawing tool.
// Keys: Enter / Ctrl+S = save + copy, Ctrl+Z / Ctrl+Y = undo / redo, Ctrl+A = whole screen,
// Esc = close without saving. While typing text: Enter = finish, Shift+Enter = new line, Esc = drop.

bool RegisterOverlayClass(HINSTANCE instance);

// Freezes the monitor under the cursor and opens the overlay. Does nothing if one is already open.
void OpenOverlay(HINSTANCE instance);

bool IsOverlayOpen();

// Esc and Enter are caught by the global keyboard hook while the overlay is open, so they work even
// if Windows did not give the overlay keyboard focus.
void OverlayKey(unsigned vk);

// True while text is being typed: then Esc and Enter belong to the text, not to the hook.
bool IsOverlayTyping();
