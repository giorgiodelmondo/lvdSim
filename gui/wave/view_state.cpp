// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/view_state.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace lvd::gui {

void ViewState::zoomAt(double anchorX, double factor) noexcept {
    if (factor <= 0.0)
        return;
    const double anchorTime = timeAt(anchorX);
    timePerPixel /= factor;
    startTime = anchorTime - anchorX * timePerPixel;
}

void ViewState::fit(double t0, double t1) noexcept {
    if (t1 <= t0)
        t1 = t0 + 1.0;
    startTime = t0;
    timePerPixel = (t1 - t0) / std::max(1, widthPx);
}

void ViewState::panPixels(double dx) noexcept {
    startTime = std::max(0.0, startTime + dx * timePerPixel);
}

void ViewState::clamp(double limit) noexcept {
    timePerPixel = std::clamp(timePerPixel, 1e-3, 1e18);
    startTime = std::clamp(startTime, 0.0, std::max(0.0, limit));
}

std::vector<double> rulerTicks(const ViewState& v, double minSpacingPx) {
    std::vector<double> ticks;
    if (v.timePerPixel <= 0.0 || v.widthPx <= 0)
        return ticks;

    const double minStep = minSpacingPx * v.timePerPixel;
    const double decade = std::pow(10.0, std::floor(std::log10(minStep)));
    double step = decade;
    for (double m : {1.0, 2.0, 5.0, 10.0}) {
        step = decade * m;
        if (step >= minStep)
            break;
    }

    double t = std::ceil(v.startTime / step) * step;
    const double end = v.endTime();
    for (; t <= end && ticks.size() < 10000; t += step)
        ticks.push_back(t);
    return ticks;
}

std::string formatTime(double time, int timescaleExp) {
    static const struct {
        int exp;
        const char* name;
    } kUnits[] = {{-15, "fs"}, {-12, "ps"}, {-9, "ns"}, {-6, "us"}, {-3, "ms"}, {0, "s"}};

    const double seconds = time * std::pow(10.0, timescaleExp);
    int unit = 0;
    for (int i = 0; i < 6; ++i) {
        unit = i;
        if (std::fabs(seconds) < std::pow(10.0, kUnits[i].exp) * 1000.0)
            break;
    }
    double value = seconds / std::pow(10.0, kUnits[unit].exp);
    if (std::fabs(value) < 1e-9)
        value = 0.0;

    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", value);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    return s + " " + kUnits[unit].name;
}

std::optional<double> parseTime(std::string_view text, int timescaleExp, int defaultUnitExp) {
    std::string s(text);
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    if (s.empty()) return std::nullopt;

    char* end = nullptr;
    const double value = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(value) || value < 0.0) return std::nullopt;

    std::string unit(end);
    unit.erase(unit.begin(), std::find_if(unit.begin(), unit.end(), notSpace));
    std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) { return std::tolower(c); });

    int exp = defaultUnitExp;
    if (unit == "fs") exp = -15;
    else if (unit == "ps") exp = -12;
    else if (unit == "ns") exp = -9;
    else if (unit == "us") exp = -6;
    else if (unit == "ms") exp = -3;
    else if (unit == "s") exp = 0;
    else if (!unit.empty()) return std::nullopt;

    return std::round(value * std::pow(10.0, exp - timescaleExp));
}

} // namespace lvd::gui
