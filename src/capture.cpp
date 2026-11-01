#include "capture.h"

HMONITOR MonitorUnderCursor() {
    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        return nullptr;
    }
    return MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
}

bool CaptureMonitor(HMONITOR monitor, Snapshot& out) {
    MONITORINFO info = {sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }
    const RECT r = info.rcMonitor;
    const int width = r.right - r.left;
    const int height = r.bottom - r.top;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    bool ok = false;
    if (memory && bitmap) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        // CAPTUREBLT includes layered (semi-transparent) windows in the picture.
        ok = BitBlt(memory, 0, 0, width, height, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(memory, old);
    }
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (!ok) {
        if (bitmap) DeleteObject(bitmap);
        return false;
    }
    out.bitmap = bitmap;
    out.bounds = r;
    return true;
}

void FreeSnapshot(Snapshot& snapshot) {
    if (snapshot.bitmap) {
        DeleteObject(snapshot.bitmap);
        snapshot.bitmap = nullptr;
    }
}
