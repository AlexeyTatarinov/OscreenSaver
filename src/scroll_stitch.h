#pragma once

// Groundwork for the future "long screenshot with scrolling" (not connected to the interface yet).
//
// How the feature will work:
//   1. The user selects an area inside a scrollable window and presses a "long screenshot" button.
//   2. The program captures the area, scrolls the window under it a little (mouse wheel), waits for
//      the window to redraw, and captures again - many times.
//   3. Each new picture is compared with the previous one to find how far the content moved, and
//      only the new rows are added below. Parts that do not move (a site's top bar, a status bar)
//      are kept once: at the top and at the bottom.
//   4. When a new picture is the same as the previous one, the end of the page is reached.
// This file is step 3: pure picture comparison, independent of Windows, with its own test.
// Steps 1, 2 and 4 are in longshot.cpp.

#include <cstdint>
#include <vector>

namespace scroll {

// A picture as 32-bit pixels, rows from top to bottom, `width` pixels per row.
struct Frame {
    const uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
};

// Rows that stay in place between two pictures: fixed top and bottom bars.
struct FixedBands {
    int top = 0;
    int bottom = 0;
    bool identical = false;  // the pictures are the same: nothing moved (end of the page)
};

FixedBands FindFixedBands(const Frame& before, const Frame& after);

// How many pixels the content between the fixed bands moved up from `before` to `after`.
// Returns -1 if no reliable match was found (for example, the window jumped or changed).
int FindScroll(const Frame& before, const Frame& after, const FixedBands& bands);

// Builds one tall picture from pictures taken while scrolling down.
class Stitcher {
public:
    explicit Stitcher(const Frame& first);

    enum class Result { Added, EndReached, NoMatch };

    // Adds the next picture (same size as the first).
    Result Add(const Frame& next);

    // The tall picture so far, including the fixed bottom bar of the last picture.
    std::vector<uint32_t> Picture(int& width, int& height) const;

    // How many pixels the content moved at the last successful Add, and the height so far.
    int LastShift() const { return lastShift_; }
    int Height() const;
    int ContentHeight() const { return height_ - bands_.top - bands_.bottom; }

private:
    int width_;
    int height_;
    std::vector<uint32_t> body_;     // the fixed top bar + all content rows so far
    std::vector<uint32_t> last_;     // the previous picture
    FixedBands bands_;
    bool bandsKnown_ = false;
    int lastShift_ = 0;
};

}  // namespace scroll
