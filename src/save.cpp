#include "save.h"

#include <objbase.h>
#include <shlobj.h>
#include <wincodec.h>

#include <cstring>
#include <cwchar>
#include <vector>

namespace {

constexpr float kJpgQuality = 0.90f;

template <typename T>
void Release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring OutputFolder() {
    std::wstring folder;
    PWSTR pictures = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) {
        folder = pictures;
    }
    CoTaskMemFree(pictures);
    if (folder.empty()) {
        return folder;
    }
    folder += L"\\OscreenSaver";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring NewFilePath() {
    const std::wstring folder = OutputFolder();
    if (folder.empty()) {
        return folder;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"OscreenSaver_%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    std::wstring path = folder + L"\\" + name + L".jpg";
    // Two screenshots in the same second get a counter instead of overwriting each other.
    for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; ++n) {
        path = folder + L"\\" + name + L"_" + std::to_wstring(n) + L".jpg";
    }
    return path;
}

}  // namespace

bool SaveAreaAsJpg(const Snapshot& shot, const RECT& area, std::wstring& savedPath) {
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    if (w <= 0 || h <= 0) {
        return false;
    }
    const std::wstring path = NewFilePath();
    if (path.empty()) {
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmap* source = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* options = nullptr;
    bool ok = false;

    // Copy the area into its own buffer: the encoder expects whole rows of exactly this width.
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const uint32_t* src = shot.pixels + static_cast<size_t>(area.top + y) * shot.Width() + area.left;
        std::memcpy(&pixels[static_cast<size_t>(y) * w], src, static_cast<size_t>(w) * 4);
    }
    const UINT stride = static_cast<UINT>(w) * 4;

    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) break;
        if (FAILED(factory->CreateBitmapFromMemory(static_cast<UINT>(w), static_cast<UINT>(h),
                                                   GUID_WICPixelFormat32bppBGR, stride,
                                                   stride * static_cast<UINT>(h),
                                                   reinterpret_cast<BYTE*>(pixels.data()), &source))) break;
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) break;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder))) break;
        if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(encoder->CreateNewFrame(&frame, &options))) break;

        PROPBAG2 quality = {};
        quality.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = kJpgQuality;
        options->Write(1, &quality, &value);

        if (FAILED(frame->Initialize(options))) break;
        if (FAILED(frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h)))) break;
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        if (FAILED(frame->SetPixelFormat(&format))) break;
        if (FAILED(frame->WriteSource(source, nullptr))) break;  // converts to the JPG pixel format
        if (FAILED(frame->Commit())) break;
        if (FAILED(encoder->Commit())) break;
        ok = true;
    } while (false);

    Release(options);
    Release(frame);
    Release(encoder);
    Release(stream);
    Release(source);
    Release(factory);

    if (ok) {
        savedPath = path;
    } else {
        DeleteFileW(path.c_str());
    }
    return ok;
}

bool CopyAreaToClipboard(const Snapshot& shot, const RECT& area, HWND owner) {
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    if (w <= 0 || h <= 0) {
        return false;
    }
    // 24-bit bottom-up DIB: the format every Windows program understands.
    const size_t rowBytes = (static_cast<size_t>(w) * 3 + 3) & ~static_cast<size_t>(3);
    const size_t imageBytes = rowBytes * h;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageBytes);
    if (!memory) {
        return false;
    }
    auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(memory));
    std::memset(header, 0, sizeof(*header));
    header->biSize = sizeof(*header);
    header->biWidth = w;
    header->biHeight = h;
    header->biPlanes = 1;
    header->biBitCount = 24;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(imageBytes);

    BYTE* out = reinterpret_cast<BYTE*>(header + 1);
    for (int y = 0; y < h; ++y) {
        const uint32_t* src = shot.pixels + static_cast<size_t>(area.top + y) * shot.Width() + area.left;
        BYTE* dst = out + rowBytes * (h - 1 - y);
        for (int x = 0; x < w; ++x) {
            const uint32_t p = src[x];
            dst[x * 3 + 0] = static_cast<BYTE>(p);
            dst[x * 3 + 1] = static_cast<BYTE>(p >> 8);
            dst[x * 3 + 2] = static_cast<BYTE>(p >> 16);
        }
    }
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_DIB, memory) != nullptr;
    CloseClipboard();
    if (!ok) {
        GlobalFree(memory);
    }
    return ok;
}
