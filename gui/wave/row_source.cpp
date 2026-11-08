// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/row_source.hpp"

#include <lvdwave/radix.hpp>

#include <algorithm>

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

std::string BitSource::text(std::size_t change, Radix) const { return std::string(1, toChar(values_[change])); }

BusSource::BusSource(const WaveStore& store, std::vector<BusMember> members)
    : store_(store)
    , members_(std::move(members)) {
    for (const BusMember& m : members_) {
        const SignalId id = store_.findSignal(m.path);
        if (id == kNoSignal) continue;
        const Signal& s = store_.signal(id);
        if (m.bit >= 0) {
            for (std::size_t i : s.bitChanges(static_cast<std::uint32_t>(m.bit))) times_.push_back(s.time(i));
        } else {
            for (std::size_t i = 0; i < s.changeCount(); ++i) times_.push_back(s.time(i));
        }
    }
    std::sort(times_.begin(), times_.end());
    times_.erase(std::unique(times_.begin(), times_.end()), times_.end());
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

std::string BusSource::bitsAt(std::size_t change) const {
    return busValueAt(store_, members_, times_[change]);
}

bool BusSource::isUnknown(std::size_t change) const {
    const std::string bits = bitsAt(change);
    return std::any_of(bits.begin(), bits.end(),
                       [](char c) { return c != '0' && c != '1' && c != 'L' && c != 'H'; });
}

std::string BusSource::text(std::size_t change, Radix radix) const {
    return formatBits(bitsAt(change), radix);
}

} // namespace lvd::gui
