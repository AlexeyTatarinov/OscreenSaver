#include "capture.h"

#include <algorithm>
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

namespace {

// One box blur along rows (step = 1) or columns (step = width), all three colour channels at once.
void BoxBlurLine(const uint32_t* src, uint32_t* dst, int count, int step, int r) {
    const int window = 2 * r + 1;
    uint32_t sb = 0, sg = 0, sr = 0;
    auto at = [&](int i) { return src[static_cast<size_t>(std::clamp(i, 0, count - 1)) * step]; };
    for (int i = -r; i <= r; ++i) {
        const uint32_t p = at(i);
        sb += p & 0xFF;
        sg += (p >> 8) & 0xFF;
        sr += (p >> 16) & 0xFF;
    }
    for (int i = 0; i < count; ++i) {
        dst[static_cast<size_t>(i) * step] = (sb / window) | ((sg / window) << 8) | ((sr / window) << 16);
        const uint32_t add = at(i + r + 1);
        const uint32_t sub = at(i - r);
        sb += (add & 0xFF) - (sub & 0xFF);
        sg += ((add >> 8) & 0xFF) - ((sub >> 8) & 0xFF);
        sr += ((add >> 16) & 0xFF) - ((sub >> 16) & 0xFF);
    }
}

}  // namespace

bool MakeBlurredCopy(const Snapshot& source, int radius, Snapshot& out) {
    const int w = source.Width();
    const int h = source.Height();
    if (!CreatePixelBitmap(w, h, out)) return false;
    out.bounds = source.bounds;
    std::vector<uint32_t> temp(static_cast<size_t>(w) * h);
    std::memcpy(out.pixels, source.pixels, temp.size() * 4);
    radius = std::max(1, radius);
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            BoxBlurLine(out.pixels + static_cast<size_t>(y) * w, temp.data() + static_cast<size_t>(y) * w, w, 1, radius);
        }
        for (int x = 0; x < w; ++x) {
            BoxBlurLine(temp.data() + x, out.pixels + x, h, w, radius);
        }
    }
    return true;
}

void FreeSnapshot(Snapshot& snapshot) {
    if (snapshot.bitmap) {
        DeleteObject(snapshot.bitmap);
    }
    snapshot = Snapshot{};
}
