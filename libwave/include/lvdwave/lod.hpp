// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <cstddef>
#include <limits>
#include <vector>

namespace lvd::wave {

struct MinMax {
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();

    bool empty() const noexcept { return min > max; }
    void add(double v) noexcept;
    void merge(const MinMax& o) noexcept;
};

/// Numeric view of a signal with a min/max pyramid, so the minimum and maximum over any run of value
/// changes is found without touching the raw changes. Used for analog rendering and zoomed-out views.
///
/// Numeric value per change: Real signals use the double. Logic signals up to 64 bits use the integer
/// value (two's complement when signed) if every bit is 0/1/L/H, otherwise the change has no value
/// (NaN) and is ignored by queries. String signals have no numeric view.
///
/// Memory is 8 bytes per change plus about 1/8 of that for the pyramid.
class NumericIndex {
public:
    /// Build for a signal. O(n).
    static NumericIndex build(const Signal& signal, bool isSigned = false);

    /// Append the changes added to the signal since the last build or update. Amortised O(new changes).
    void update(const Signal& signal);

    bool valid() const noexcept { return valid_; }
    bool isSigned() const noexcept { return signed_; }
    std::size_t size() const noexcept { return values_.size(); }
    /// Numeric value of change i; NaN if none.
    double value(std::size_t i) const noexcept { return values_[i]; }

    /// Min and max over changes [first, last). Empty result if the range has no numeric change.
    /// O(log n).
    MinMax range(std::size_t first, std::size_t last) const noexcept;

private:
    static constexpr std::size_t kFanout = 16;

    void rebuildFrom(std::size_t firstChanged);

    bool valid_ = false;
    bool signed_ = false;
    std::vector<double> values_;
    std::vector<std::vector<MinMax>> levels_;  // levels_[0] summarises kFanout raw values per entry
};

/// Numeric value of one change of a signal under the rules above (NaN if none).
double numericValue(const Signal& signal, std::size_t change, bool isSigned) noexcept;

}  // namespace lvd::wave
