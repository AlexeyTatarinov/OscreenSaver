#include "scroll_stitch.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace scroll {
namespace {

constexpr int kMinOverlap = 16;     // at least this many rows must overlap to trust a match
constexpr int kBins = 48;           // each row is described by the brightness of 48 parts of it
constexpr double kSameRow = 3.0;    // average brightness difference (0..255) still counted as "same row"
constexpr double kMaxScore = 6.0;   // average difference allowed for a found scroll position

// Real windows do not repeat pixels exactly after scrolling: smooth text edges, scaled screens
// (125 %, 150 %) and a moving scrollbar change a few pixels. So rows are compared by their
// brightness in 48 parts, with a small tolerance, instead of exactly.
using RowFeatures = std::vector<uint8_t>;  // kBins values per row

RowFeatures Features(const Frame& f) {
    RowFeatures out(static_cast<size_t>(f.height) * kBins);
    const int bins = std::min(kBins, std::max(1, f.width));
    for (int y = 0; y < f.height; ++y) {
        const uint32_t* row = f.pixels + static_cast<size_t>(y) * f.width;
        for (int b = 0; b < kBins; ++b) {
            const int x0 = static_cast<int>(static_cast<long long>(b % bins) * f.width / bins);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(b % bins + 1) * f.width / bins));
            unsigned sum = 0;
            for (int x = x0; x < x1; ++x) {
                const uint32_t p = row[x];
                sum += ((p >> 16) & 0xFF) * 3 + ((p >> 8) & 0xFF) * 6 + (p & 0xFF);  // ~ brightness * 10
            }
            out[static_cast<size_t>(y) * kBins + b] = static_cast<uint8_t>(sum / (10u * static_cast<unsigned>(x1 - x0)));
        }
    }
    return out;
}

double RowDistance(const RowFeatures& a, int ya, const RowFeatures& b, int yb) {
    const uint8_t* pa = a.data() + static_cast<size_t>(ya) * kBins;
    const uint8_t* pb = b.data() + static_cast<size_t>(yb) * kBins;
    int sum = 0;
    for (int i = 0; i < kBins; ++i) sum += std::abs(static_cast<int>(pa[i]) - static_cast<int>(pb[i]));
    return static_cast<double>(sum) / kBins;
}

}  // namespace

FixedBands FindFixedBands(const Frame& a, const Frame& b) {
    FixedBands bands;
    if (a.width != b.width || a.height != b.height || a.height == 0) return bands;
    const RowFeatures fa = Features(a), fb = Features(b);
    int top = 0;
    while (top < a.height && RowDistance(fa, top, fb, top) <= kSameRow) ++top;
    if (top == a.height) {
        bands.identical = true;
        return bands;
    }
    int bottom = 0;
    while (bottom < a.height - top && RowDistance(fa, a.height - 1 - bottom, fb, a.height - 1 - bottom) <= kSameRow) {
        ++bottom;
    }
    bands.top = top;
    bands.bottom = bottom;
    return bands;
}

int FindScroll(const Frame& a, const Frame& b, const FixedBands& bands, int expected) {
    if (a.width != b.width || a.height != b.height) return -1;
    const RowFeatures fa = Features(a), fb = Features(b);
    const int top = bands.top;
    const int content = a.height - bands.top - bands.bottom;
    if (content <= kMinOverlap) return -1;

    // For every possible shift: the average difference of the rows that should meet.
    int bestShift = -1;
    double bestScore = 1e9;
    for (int shift = 1; shift <= content - kMinOverlap; ++shift) {
        const int overlap = content - shift;
        const int step = overlap > 400 ? 2 : 1;  // long overlaps: every second row is enough
        double sum = 0;
        int count = 0;
        for (int i = 0; i < overlap; i += step) {
            sum += RowDistance(fa, top + shift + i, fb, top + i);
            ++count;
            if (sum / overlap * step > bestScore + 2) break;  // already clearly worse
        }
        const double score = sum / std::max(1, count);
        // Equal scores (plain areas): the shift closest to the expected one wins.
        const bool better = score < bestScore - 0.05 ||
                            (score <= bestScore + 0.05 && expected > 0 &&
                             std::abs(shift - expected) < std::abs(bestShift - expected));
        if (better) {
            bestScore = std::min(bestScore, score);
            bestShift = shift;
        }
    }
    return bestScore <= kMaxScore ? bestShift : -1;
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
    const int shift = FindScroll(prev, next, bands_, lastShift_);
    if (shift < 0) return Result::NoMatch;
    // New rows: the last `shift` rows of the scrolling part of the next picture.
    const int contentEnd = height_ - bands_.bottom;
    const int from = contentEnd - shift;
    body_.insert(body_.end(), next.pixels + static_cast<size_t>(from) * width_,
                 next.pixels + static_cast<size_t>(contentEnd) * width_);
    last_.assign(next.pixels, next.pixels + static_cast<size_t>(width_) * height_);
    lastShift_ = shift;
    return Result::Added;
}

int Stitcher::Height() const {
    const int body = static_cast<int>(body_.size() / static_cast<size_t>(std::max(1, width_)));
    return body + (bandsKnown_ ? bands_.bottom : 0);
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
