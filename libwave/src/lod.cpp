// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/lod.hpp>

#include <algorithm>
#include <cmath>

namespace lvd::wave {

void MinMax::add(double v) noexcept {
    if (std::isnan(v)) return;
    min = std::min(min, v);
    max = std::max(max, v);
}

void MinMax::merge(const MinMax& o) noexcept {
    min = std::min(min, o.min);
    max = std::max(max, o.max);
}

double numericValue(const Signal& s, std::size_t change, bool isSigned) noexcept {
    switch (s.kind()) {
        case SignalKind::Real:
            return s.real(change);
        case SignalKind::String:
            return std::nan("");
        case SignalKind::Logic:
            break;
    }
    const std::uint32_t w = s.width();
    if (w > 64) return std::nan("");
    std::uint64_t v = 0;
    for (std::uint32_t i = 0; i < w; ++i) {
        const Logic b = s.bit(change, i);
        if (!isKnown(b)) return std::nan("");
        if (bitValue(b)) v |= std::uint64_t{1} << i;
    }
    if (isSigned && w < 64 && (v >> (w - 1)) != 0) v |= ~std::uint64_t{0} << w;  // sign extend
    if (isSigned) return static_cast<double>(static_cast<std::int64_t>(v));
    return static_cast<double>(v);
}

NumericIndex NumericIndex::build(const Signal& signal, bool isSigned) {
    NumericIndex idx;
    idx.signed_ = isSigned;
    idx.valid_ = signal.kind() == SignalKind::Real || (signal.kind() == SignalKind::Logic && signal.width() <= 64);
    if (idx.valid_) idx.update(signal);
    return idx;
}

void NumericIndex::update(const Signal& signal) {
    if (!valid_) return;
    const std::size_t from = values_.size();
    const std::size_t n = signal.changeCount();
    values_.reserve(n);
    for (std::size_t i = from; i < n; ++i) values_.push_back(numericValue(signal, i, signed_));
    if (n > from) rebuildFrom(from);
}

void NumericIndex::rebuildFrom(std::size_t firstChanged) {
    // Level 0 is built from raw values, each higher level from the one below.
    std::size_t changed = firstChanged;
    std::size_t srcCount = values_.size();
    for (std::size_t level = 0;; ++level) {
        const std::size_t blocks = (srcCount + kFanout - 1) / kFanout;
        if (level >= levels_.size()) levels_.emplace_back();
        std::vector<MinMax>& dst = levels_[level];
        dst.resize(blocks);

        for (std::size_t b = changed / kFanout; b < blocks; ++b) {
            MinMax m;
            const std::size_t lo = b * kFanout;
            const std::size_t hi = std::min(lo + kFanout, srcCount);
            if (level == 0) {
                for (std::size_t i = lo; i < hi; ++i) m.add(values_[i]);
            } else {
                const std::vector<MinMax>& src = levels_[level - 1];
                for (std::size_t i = lo; i < hi; ++i) m.merge(src[i]);
            }
            dst[b] = m;
        }

        if (blocks <= 1) {
            levels_.resize(level + 1);
            break;
        }
        changed = changed / kFanout;
        srcCount = blocks;
    }
}

MinMax NumericIndex::range(std::size_t first, std::size_t last) const noexcept {
    MinMax out;
    last = std::min(last, values_.size());
    if (first >= last) return out;

    std::size_t l = first, h = last;

    // Ragged edges at the raw level.
    while (l < h && l % kFanout != 0) out.add(values_[l++]);
    while (l < h && h % kFanout != 0) out.add(values_[--h]);
    if (l >= h) return out;
    l /= kFanout;
    h /= kFanout;

    for (std::size_t level = 0; level < levels_.size() && l < h; ++level) {
        const std::vector<MinMax>& v = levels_[level];
        while (l < h && l % kFanout != 0) out.merge(v[l++]);
        while (l < h && h % kFanout != 0) out.merge(v[--h]);
        l /= kFanout;
        h /= kFanout;
    }
    return out;
}

}  // namespace lvd::wave
