// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::gui {

using lvd::wave::Time;

/// Horizontal view of the waveform area: which time is at the left edge and how much time one pixel is.
struct ViewState {
    double startTime = 0.0;    ///< Time at x = 0, in timescale units.
    double timePerPixel = 1.0; ///< Must be positive.
    int widthPx = 800;

    double timeAt(double x) const noexcept { return startTime + x * timePerPixel; }
    double xAt(double t) const noexcept { return (t - startTime) / timePerPixel; }
    double endTime() const noexcept { return timeAt(widthPx); }

    /// Zoom by factor (>1 zooms in) keeping the time under anchorX fixed.
    void zoomAt(double anchorX, double factor) noexcept;
    /// Fit [t0, t1] into the width. A degenerate range is widened.
    void fit(double t0, double t1) noexcept;
    /// Move by a number of pixels (positive moves the view to later times). Never goes before time 0.
    void panPixels(double dx) noexcept;
    /// Keep the view inside [0, limit] and the scale inside sensible bounds.
    void clamp(double limit) noexcept;
};

/// Ruler tick positions for a view: times (timescale units) of major ticks at least minSpacingPx apart,
/// on a 1-2-5 decade grid. O(width / minSpacingPx).
std::vector<double> rulerTicks(const ViewState& v, double minSpacingPx);

/// "12.5 ns" style text. timescaleExp is the power of ten of one time unit in seconds (-15 is femtoseconds).
/// Picks fs, ps, ns, us, ms or s so the number is in [1, 1000), with up to three decimals, trailing zeros
/// trimmed.
std::string formatTime(double time, int timescaleExp);

/// Parses "1500 ps", "12.5ns", "3 us" into timescale units (rounded). A number without a unit uses
/// defaultUnitExp (power of ten of seconds, -9 for ns). Returns nullopt for malformed or negative input.
std::optional<double> parseTime(std::string_view text, int timescaleExp, int defaultUnitExp = -9);

} // namespace lvd::gui
