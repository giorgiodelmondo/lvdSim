// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/row_source.hpp"

#include <lvdwave/radix.hpp>

#include <algorithm>
#include <limits>

namespace lvd::gui {

using namespace lvd::wave;

SignalSource::SignalSource(const Signal& signal, const NumericIndex* index, bool analog)
    : s_(signal)
    , index_(index) {
    if (analog && index != nullptr && index->valid())
        class_ = WaveClass::Analog;
    else if (signal.kind() == SignalKind::Logic && signal.width() == 1)
        class_ = WaveClass::Scalar;
    else
        class_ = WaveClass::Bus;
}

bool SignalSource::isUnknown(std::size_t change) const {
    if (s_.kind() != SignalKind::Logic)
        return false;
    for (std::uint32_t i = 0; i < s_.width(); ++i)
        if (!isKnown(s_.bit(change, i)))
            return true;
    return false;
}

std::string SignalSource::text(std::size_t change, Radix radix) const {
    return formatValue(s_, change, radix);
}

BitSource::BitSource(const Signal& signal, std::uint32_t bit) {
    for (std::size_t i : signal.bitChanges(bit)) {
        times_.push_back(signal.time(i));
        values_.push_back(signal.bit(i, bit));
    }
}

std::size_t BitSource::holdIndex(Time t) const {
    auto it = std::upper_bound(times_.begin(), times_.end(), t);
    return it == times_.begin() ? kNoIndex : static_cast<std::size_t>(it - times_.begin()) - 1;
}

std::pair<std::size_t, std::size_t> BitSource::changesIn(Time a, Time b) const {
    auto lo = std::lower_bound(times_.begin(), times_.end(), a);
    auto hi = std::lower_bound(lo, times_.end(), b);
    return {static_cast<std::size_t>(lo - times_.begin()), static_cast<std::size_t>(hi - times_.begin())};
}

std::string BitSource::text(std::size_t change, Radix) const {
    return std::string(1, toChar(values_[change]));
}

BusSource::BusSource(const WaveSpace& space, std::vector<BusMember> members, bool analog, bool isSigned)
    : members_(std::move(members)) {
    for (const BusMember& m : members_) {
        const Signal* s = space.findPath(m.path);
        signals_.push_back(s);
        if (s == nullptr) {
            width_ += 1;
            continue;
        }
        if (s->kind() == SignalKind::Logic)
            width_ += m.bit >= 0 ? 1u : s->width();
        if (m.bit >= 0) {
            for (std::size_t i : s->bitChanges(static_cast<std::uint32_t>(m.bit)))
                times_.push_back(s->time(i));
        } else {
            for (std::size_t i = 0; i < s->changeCount(); ++i)
                times_.push_back(s->time(i));
        }
    }
    std::sort(times_.begin(), times_.end());
    times_.erase(std::unique(times_.begin(), times_.end()), times_.end());
    if (analog && width_ >= 1 && width_ <= 64)
        buildIndex(isSigned);
}

void BusSource::buildIndex(bool isSigned) {
    // One sweep over the change times with a cursor per member (no search, no string per change). The value
    // is the same as numericValueOfBits(bits(change), isSigned).
    const std::size_t m = members_.size();
    std::vector<std::size_t> cursor(m, kNoIndex);
    std::vector<double> values;
    values.reserve(times_.size());
    for (const Time t : times_) {
        std::uint64_t v = 0;
        bool known = true;
        for (std::size_t k = 0; k < m && known; ++k) {
            const Signal* s = signals_[k];
            if (s == nullptr) {
                known = false; // a missing member reads as U
                break;
            }
            if (s->kind() != SignalKind::Logic)
                continue;
            std::size_t& c = cursor[k];
            const std::size_t n = s->changeCount();
            while ((c == kNoIndex ? 0 : c + 1) < n && s->time(c == kNoIndex ? 0 : c + 1) <= t)
                c = c == kNoIndex ? 0 : c + 1;
            if (c == kNoIndex) {
                known = false;
                break;
            }
            const int bit = members_[k].bit;
            if (bit >= 0) {
                if (static_cast<std::uint32_t>(bit) >= s->width()) {
                    known = false;
                    break;
                }
                const Logic b = s->bit(c, static_cast<std::uint32_t>(bit));
                known = isKnown(b);
                v = (v << 1) | (bitValue(b) ? 1u : 0u);
                continue;
            }
            const std::uint32_t w = s->width();
            for (std::uint32_t i = w; i-- > 0 && known;) {
                const Logic b = s->bit(c, i);
                known = isKnown(b);
                v = (v << 1) | (bitValue(b) ? 1u : 0u);
            }
        }
        if (!known) {
            values.push_back(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        if (isSigned && width_ < 64 && (v >> (width_ - 1)) != 0)
            v |= ~std::uint64_t{0} << width_; // sign extend
        values.push_back(isSigned ? static_cast<double>(static_cast<std::int64_t>(v))
                                  : static_cast<double>(v));
    }
    index_ = NumericIndex::fromValues(std::move(values), isSigned);
    analog_ = true;
}

std::size_t BusSource::holdIndex(Time t) const {
    auto it = std::upper_bound(times_.begin(), times_.end(), t);
    return it == times_.begin() ? kNoIndex : static_cast<std::size_t>(it - times_.begin()) - 1;
}

std::pair<std::size_t, std::size_t> BusSource::changesIn(Time a, Time b) const {
    auto lo = std::lower_bound(times_.begin(), times_.end(), a);
    auto hi = std::lower_bound(lo, times_.end(), b);
    return {static_cast<std::size_t>(lo - times_.begin()), static_cast<std::size_t>(hi - times_.begin())};
}

std::string BusSource::bits(std::size_t change) const {
    // Same composition as lvd::wave::busValueAt, with the members resolved once.
    const Time t = times_[change];
    std::string out;
    for (std::size_t k = 0; k < members_.size(); ++k) {
        const Signal* s = signals_[k];
        const int bit = members_[k].bit;
        if (s == nullptr) {
            out += 'U';
            continue;
        }
        if (s->kind() != SignalKind::Logic)
            continue;
        const std::size_t idx = s->holdIndex(t);
        if (bit >= 0) {
            if (idx == kNoIndex || static_cast<std::uint32_t>(bit) >= s->width())
                out += 'U';
            else
                out += toChar(s->bit(idx, static_cast<std::uint32_t>(bit)));
            continue;
        }
        if (idx == kNoIndex)
            out.append(s->width(), 'U');
        else
            out += s->bits(idx);
    }
    return out;
}

bool BusSource::isUnknown(std::size_t change) const {
    const std::string b = bits(change);
    return std::any_of(b.begin(), b.end(),
                       [](char c) { return c != '0' && c != '1' && c != 'L' && c != 'H'; });
}

std::string BusSource::text(std::size_t change, Radix radix) const {
    return formatBits(bits(change), radix);
}

} // namespace lvd::gui
