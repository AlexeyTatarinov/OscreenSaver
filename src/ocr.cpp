#include "ocr.h"

#include <string>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
// Windows.Media.Ocr: the text recognition of Windows 10/11, reached through C++/WinRT.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#define OSS_HAS_OCR 1
#else
#define OSS_HAS_OCR 0  // other compilers (test builds) have no WinRT headers
#endif

#include <algorithm>
#include <cstring>

void RecognizeText(Snapshot picture, HWND notify, UINT doneMessage) {
    int w = picture.Width();
    int h = picture.Height();
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    std::memcpy(pixels.data(), picture.pixels, pixels.size() * 4);
    FreeSnapshot(picture);

    std::thread([pixels = std::move(pixels), w, h, notify, doneMessage]() mutable {
        auto* text = new std::wstring;
        bool ok = false;
#if OSS_HAS_OCR
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;
            OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (engine) {
                // Very large pictures are made smaller to the size the recognizer accepts.
                const int limit = static_cast<int>(OcrEngine::MaxImageDimension());
                const float k = std::min(1.f, static_cast<float>(limit) / static_cast<float>(std::max(w, h)));
                const int sw = std::max(1, static_cast<int>(w * k));
                const int sh = std::max(1, static_cast<int>(h * k));
                winrt::Windows::Storage::Streams::Buffer buffer(static_cast<uint32_t>(sw * sh * 4));
                auto* dst = reinterpret_cast<uint32_t*>(buffer.data());
                for (int y = 0; y < sh; ++y) {
                    const int srcY = std::min(h - 1, static_cast<int>(y / k));
                    for (int x = 0; x < sw; ++x) {
                        const int srcX = std::min(w - 1, static_cast<int>(x / k));
                        dst[static_cast<size_t>(y) * sw + x] = pixels[static_cast<size_t>(srcY) * w + srcX] | 0xFF000000u;
                    }
                }
                buffer.Length(static_cast<uint32_t>(sw * sh * 4));
                SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, sw, sh, BitmapAlphaMode::Premultiplied);
                bitmap.CopyFromBuffer(buffer);
                const OcrResult result = engine.RecognizeAsync(bitmap).get();
                for (const OcrLine& line : result.Lines()) {
                    if (!text->empty()) text->append(L"\r\n");
                    text->append(line.Text().c_str());
                }
                ok = true;
            }
        } catch (...) {
            ok = false;
        }
        winrt::uninit_apartment();
#endif
        if (!PostMessageW(notify, doneMessage, ok ? 1 : 0, reinterpret_cast<LPARAM>(text))) delete text;
    }).detach();
}
