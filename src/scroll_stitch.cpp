#include "scroll_stitch.h"

#include <algorithm>
#include <cstring>

namespace scroll {
namespace {

constexpr double kMinMatch = 0.95;  // share of equal rows needed (allows a blinking cursor and similar)
constexpr int kMinOverlap = 16;     // at least this many rows must overlap to trust a match

uint64_t RowHash(const uint32_t* row, int width) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (int x = 0; x < width; ++x) {
        h ^= row[x] & 0x00FFFFFFu;  // alpha is ignored
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<uint64_t> RowHashes(const Frame& f) {
    std::vector<uint64_t> hashes(static_cast<size_t>(f.height));
    for (int y = 0; y < f.height; ++y) hashes[y] = RowHash(f.pixels + static_cast<size_t>(y) * f.width, f.width);
    return hashes;
}

}  // namespace

FixedBands FindFixedBands(const Frame& a, const Frame& b) {
    FixedBands bands;
    if (a.width != b.width || a.height != b.height || a.height == 0) return bands;
    const std::vector<uint64_t> ha = RowHashes(a), hb = RowHashes(b);
    int top = 0;
    while (top < a.height && ha[top] == hb[top]) ++top;
    if (top == a.height) {
        bands.identical = true;
        return bands;
    }
    int bottom = 0;
    while (bottom < a.height - top && ha[a.height - 1 - bottom] == hb[a.height - 1 - bottom]) ++bottom;
    bands.top = top;
    bands.bottom = bottom;
    return bands;
}

int FindScroll(const Frame& a, const Frame& b, const FixedBands& bands) {
    if (a.width != b.width || a.height != b.height) return -1;
    const std::vector<uint64_t> ha = RowHashes(a), hb = RowHashes(b);
    const int top = bands.top;
    const int content = a.height - bands.top - bands.bottom;
    if (content <= kMinOverlap) return -1;

    int bestShift = -1;
    double bestScore = 0;
    for (int shift = 1; shift <= content - kMinOverlap; ++shift) {
        const int overlap = content - shift;
        int equal = 0;
        for (int i = 0; i < overlap; ++i) {
            if (ha[top + shift + i] == hb[top + i]) ++equal;
        }
        const double score = static_cast<double>(equal) / overlap;
        if (score > bestScore + 1e-9) {  // the smallest shift wins a tie
            bestScore = score;
            bestShift = shift;
        }
    }
    return bestScore >= kMinMatch ? bestShift : -1;
}

Stitcher::Stitcher(const Frame& first) : width_(first.width), height_(first.height) {
    last_.assign(first.pixels, first.pixels + static_cast<size_t>(first.width) * first.height);
    body_ = last_;  // until the fixed bands are known, the whole first picture is the body
}

Stitcher::Result Stitcher::Add(const Frame& next) {
    if (next.width != width_ || next.height != height_) return Result::NoMatch;
    const Frame prev{last_.data(), width_, height_};
    const FixedBands bands = FindFixedBands(prev, next);
    if (bands.identical) return Result::EndReached;
    if (!bandsKnown_) {
        // The first move shows which bars do not scroll; the bottom bar is cut off the body and
        // added once at the very end.
        bands_ = bands;
        bandsKnown_ = true;
        body_.resize(static_cast<size_t>(height_ - bands_.bottom) * width_);
    }
    const int shift = FindScroll(prev, next, bands_);
    if (shift < 0) return Result::NoMatch;
    // New rows: the last `shift` rows of the scrolling part of the next picture.
    const int contentEnd = height_ - bands_.bottom;
    const int from = contentEnd - shift;
    body_.insert(body_.end(), next.pixels + static_cast<size_t>(from) * width_,
                 next.pixels + static_cast<size_t>(contentEnd) * width_);
    last_.assign(next.pixels, next.pixels + static_cast<size_t>(width_) * height_);
    return Result::Added;
}

std::vector<uint32_t> Stitcher::Picture(int& width, int& height) const {
    std::vector<uint32_t> out = body_;
    if (bandsKnown_ && bands_.bottom > 0) {
        const size_t from = static_cast<size_t>(height_ - bands_.bottom) * width_;
        out.insert(out.end(), last_.begin() + static_cast<std::ptrdiff_t>(from), last_.end());
    }
    width = width_;
    height = static_cast<int>(out.size() / static_cast<size_t>(std::max(1, width_)));
    return out;
}

}  // namespace scroll
