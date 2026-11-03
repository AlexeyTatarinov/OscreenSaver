// Test for the long-screenshot groundwork: a tall made-up "page" with a fixed top bar and a fixed
// bottom bar is shown through a small window and scrolled by different steps; the stitched picture
// must be exactly the top bar + the whole page + the bottom bar.

#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

#include "../src/scroll_stitch.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::vector<uint32_t> MakeRows(int width, int height, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<uint32_t> px(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        // Every row differs: text-like random dots on a light background.
        for (int x = 0; x < width; ++x) {
            px[static_cast<size_t>(y) * width + x] = (rng() % 7 == 0) ? 0x202020u : 0xF0F0F0u;
        }
    }
    return px;
}

// What the window shows when the page is scrolled to `offset`.
std::vector<uint32_t> View(const std::vector<uint32_t>& page, const std::vector<uint32_t>& header,
                           const std::vector<uint32_t>& footer, int width, int headerH, int footerH, int viewH,
                           int offset) {
    std::vector<uint32_t> v = header;
    const int contentH = viewH - headerH - footerH;
    v.insert(v.end(), page.begin() + static_cast<std::ptrdiff_t>(offset) * width,
             page.begin() + static_cast<std::ptrdiff_t>(offset + contentH) * width);
    v.insert(v.end(), footer.begin(), footer.end());
    return v;
}

// Small random changes like smooth text edges after scrolling.
void AddNoise(std::vector<uint32_t>& px, unsigned seed) {
    std::mt19937 rng(seed);
    for (uint32_t& p : px) {
        if (rng() % 5 == 0) {
            const int d = static_cast<int>(rng() % 7) - 3;
            auto ch = [&](int shift) {
                const int v = std::clamp(static_cast<int>((p >> shift) & 0xFF) + d, 0, 255);
                return static_cast<uint32_t>(v) << shift;
            };
            p = ch(16) | ch(8) | ch(0);
        }
    }
}

void RunCase(int headerH, int footerH, std::vector<int> steps, const char* name, bool noise = false) {
    const int width = 64, pageH = 900, viewH = 240;
    const auto page = MakeRows(width, pageH, 7);
    const auto header = MakeRows(width, headerH, 11);
    const auto footer = MakeRows(width, footerH, 13);
    const int contentH = viewH - headerH - footerH;

    int offset = 0;
    auto first = View(page, header, footer, width, headerH, footerH, viewH, offset);
    scroll::Stitcher stitcher(scroll::Frame{first.data(), width, viewH});
    bool ended = false;
    for (size_t i = 0; !ended; ++i) {
        const int step = steps[i % steps.size()];
        offset = std::min(offset + step, pageH - contentH);  // the page stops at its end
        auto next = View(page, header, footer, width, headerH, footerH, viewH, offset);
        if (noise) AddNoise(next, static_cast<unsigned>(100 + i));
        const auto r = stitcher.Add(scroll::Frame{next.data(), width, viewH});
        if (r == scroll::Stitcher::Result::EndReached) ended = true;
        if (r == scroll::Stitcher::Result::NoMatch) {
            Check(false, name);
            return;
        }
    }
    int w = 0, h = 0;
    const auto result = stitcher.Picture(w, h);
    std::vector<uint32_t> expected = header;
    expected.insert(expected.end(), page.begin(), page.end());
    expected.insert(expected.end(), footer.begin(), footer.end());
    if (noise) {
        // With noise only the size and the order of the content can be checked.
        Check(w == width && h == headerH + pageH + footerH, name);
        return;
    }
    Check(w == width && h == headerH + pageH + footerH && result == expected, name);
}

}  // namespace

int main() {
    RunCase(20, 15, {57}, "fixed top and bottom bars, even steps");
    RunCase(0, 0, {40, 73, 19, 120}, "no bars, uneven steps");
    RunCase(32, 0, {100}, "top bar only, large steps");
    RunCase(20, 15, {57, 90}, "small pixel differences after scrolling", true);

    // Two identical pictures: nothing moved.
    const auto a = MakeRows(32, 100, 3);
    const auto bands = scroll::FindFixedBands(scroll::Frame{a.data(), 32, 100}, scroll::Frame{a.data(), 32, 100});
    Check(bands.identical, "identical pictures mean the end of the page");

    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
