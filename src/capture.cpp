#include "capture.h"

#include <cstring>

namespace {

bool CreatePixelBitmap(int width, int height, Snapshot& out) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    out.bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    out.pixels = static_cast<uint32_t*>(bits);
    return out.bitmap != nullptr;
}

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
    return TRUE;
}

}  // namespace

HMONITOR MonitorUnderCursor() {
    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        return nullptr;
    }
    return MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
}

std::vector<HMONITOR> AllMonitors() {
    std::vector<HMONITOR> result;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&result));
    return result;
}

bool CaptureMonitor(HMONITOR monitor, Snapshot& out) {
    MONITORINFO info = {sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }
    const RECT r = info.rcMonitor;
    const int width = r.right - r.left;
    const int height = r.bottom - r.top;
    if (!CreatePixelBitmap(width, height, out)) {
        return false;
    }
    out.bounds = r;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(memory, out.bitmap);
    // CAPTUREBLT includes layered (semi-transparent) windows in the picture.
    const bool ok = BitBlt(memory, 0, 0, width, height, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
    SelectObject(memory, old);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    GdiFlush();

    if (!ok) {
        FreeSnapshot(out);
    }
    return ok;
}

bool CreateBlankSnapshot(int width, int height, Snapshot& out) {
    out.bounds = RECT{0, 0, width, height};
    return CreatePixelBitmap(width, height, out);
}

bool MakeDimmedCopy(const Snapshot& source, Snapshot& out) {
    if (!CreatePixelBitmap(source.Width(), source.Height(), out)) {
        return false;
    }
    out.bounds = source.bounds;
    const size_t count = static_cast<size_t>(source.Width()) * source.Height();
    const uint32_t* src = source.pixels;
    uint32_t* dst = out.pixels;
    // Each channel: half brightness plus a grey tint -> a grey veil over the picture.
    for (size_t i = 0; i < count; ++i) {
        dst[i] = ((src[i] >> 1) & 0x007F7F7Fu) + 0x00282828u;
    }
    return true;
}

void FreeSnapshot(Snapshot& snapshot) {
    if (snapshot.bitmap) {
        DeleteObject(snapshot.bitmap);
    }
    snapshot = Snapshot{};
}
