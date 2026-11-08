// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/display_model.hpp>
#include <lvdwave/lod.hpp>
#include <lvdwave/store.hpp>

#include <string>
#include <utility>
#include <vector>

namespace lvd::gui {

using lvd::wave::Logic;
using lvd::wave::Radix;
using lvd::wave::Time;

/// How a row is drawn.
enum class WaveClass {
    Scalar, ///< one bit: levels and edges
    Bus,    ///< vector, enumeration, real or virtual bus: value segments with text
    Analog, ///< numeric trace
};

/// Read-only view of the changes behind one wave row. The renderer depends on this interface only, so
/// it works for plain signals and for virtual buses alike.
class RowSource {
public:
    virtual ~RowSource() = default;

    virtual WaveClass waveClass() const = 0;
    virtual std::size_t count() const = 0;
    virtual Time time(std::size_t change) const = 0;
    /// Index of the change in effect at t, or lvd::wave::kNoIndex. O(log n).
    virtual std::size_t holdIndex(Time t) const = 0;
    /// Changes with t0 <= time < t1 as [first, last). O(log n).
    virtual std::pair<std::size_t, std::size_t> changesIn(Time t0, Time t1) const = 0;

    /// Scalar rows: logic value of a change.
    virtual Logic scalar(std::size_t change) const = 0;
    /// Bus rows: true if the value of the change contains X, U, W, Z or don't care.
    virtual bool isUnknown(std::size_t change) const = 0;
    /// Text of a change's value in a radix.
    virtual std::string text(std::size_t change, Radix radix) const = 0;
    /// Analog rows (and anything with a numeric view): the min/max index, or nullptr.
    virtual const lvd::wave::NumericIndex* numeric() const = 0;
};

/// A plain signal. Does not own the signal or the index; both must outlive it.
class SignalSource final : public RowSource {
public:
    SignalSource(const lvd::wave::Signal& signal, const lvd::wave::NumericIndex* index, bool analog);

    WaveClass waveClass() const override { return class_; }
    std::size_t count() const override { return s_.changeCount(); }
    Time time(std::size_t c) const override { return s_.time(c); }
    std::size_t holdIndex(Time t) const override { return s_.holdIndex(t); }
    std::pair<std::size_t, std::size_t> changesIn(Time a, Time b) const override {
        return s_.changesIn(a, b);
    }
    Logic scalar(std::size_t c) const override { return s_.bit(c, 0); }
    bool isUnknown(std::size_t change) const override;
    std::string text(std::size_t change, Radix radix) const override;
    const lvd::wave::NumericIndex* numeric() const override { return index_; }

private:
    const lvd::wave::Signal& s_;
    const lvd::wave::NumericIndex* index_;
    WaveClass class_;
};

/// One bit of a Logic signal drawn as a scalar. Only the changes of that bit are kept, so a constant
/// bit of a busy bus is a flat line. Building is O(changes of the signal).
class BitSource final : public RowSource {
public:
    BitSource(const lvd::wave::Signal& signal, std::uint32_t bit);

    WaveClass waveClass() const override { return WaveClass::Scalar; }
    std::size_t count() const override { return times_.size(); }
    Time time(std::size_t c) const override { return times_[c]; }
    std::size_t holdIndex(Time t) const override;
    std::pair<std::size_t, std::size_t> changesIn(Time a, Time b) const override;
    Logic scalar(std::size_t c) const override { return values_[c]; }
    bool isUnknown(std::size_t change) const override { return !lvd::wave::isKnown(values_[change]); }
    std::string text(std::size_t change, Radix radix) const override;
    const lvd::wave::NumericIndex* numeric() const override { return nullptr; }

private:
    std::vector<Time> times_;
    std::vector<Logic> values_;
};

/// A virtual bus: the union of the change times of its members, value composed on demand. A member may
/// be a whole signal or a single bit. Building is O(total member changes). Members missing from the
/// store are skipped.
class BusSource final : public RowSource {
public:
    BusSource(const lvd::wave::WaveStore& store, std::vector<lvd::wave::BusMember> members);

    WaveClass waveClass() const override { return WaveClass::Bus; }
    std::size_t count() const override { return times_.size(); }
    Time time(std::size_t c) const override { return times_[c]; }
    std::size_t holdIndex(Time t) const override;
    std::pair<std::size_t, std::size_t> changesIn(Time a, Time b) const override;
    Logic scalar(std::size_t) const override { return Logic::X; }
    bool isUnknown(std::size_t change) const override;
    std::string text(std::size_t change, Radix radix) const override;
    const lvd::wave::NumericIndex* numeric() const override { return nullptr; }

private:
    std::string bitsAt(std::size_t change) const;

    const lvd::wave::WaveStore& store_;
    std::vector<lvd::wave::BusMember> members_;
    std::vector<Time> times_;
};

} // namespace lvd::gui
