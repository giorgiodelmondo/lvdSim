// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/parsers/report_lines.hpp"

#include <limits>

namespace lvd::gui {

std::optional<std::int64_t> unitToFemtoseconds(std::string_view unit) {
    struct U {
        std::string_view name;
        std::int64_t fs;
    };
    static constexpr U kUnits[] = {{"fs", 1},
                                   {"ps", 1'000},
                                   {"ns", 1'000'000},
                                   {"us", 1'000'000'000},
                                   {"ms", 1'000'000'000'000},
                                   {"s", 1'000'000'000'000'000}};
    for (const U& u : kUnits)
        if (u.name == unit)
            return u.fs;
    return std::nullopt;
}

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

}  // namespace

std::optional<ReportLine> parseReportLine(std::string_view s) {
    std::size_t i = 0;
    while (s.substr(i, 2) == "% ")
        i += 2;
    if (s.substr(i, 3) != "** ")
        return std::nullopt;
    i += 3;

    struct Sev {
        std::string_view name;
        ReportSeverity sev;
    };
    static constexpr Sev kSev[] = {{"Note: ", ReportSeverity::Note},       {"Warning: ", ReportSeverity::Warning},
                                   {"Error: ", ReportSeverity::Error},     {"Failure: ", ReportSeverity::Failure},
                                   {"Fatal: ", ReportSeverity::Failure},   {"Debug: ", ReportSeverity::Note}};
    ReportLine r;
    bool found = false;
    for (const Sev& k : kSev) {
        if (s.substr(i, k.name.size()) == k.name) {
            r.severity = k.sev;
            i += k.name.size();
            found = true;
            break;
        }
    }
    if (!found)
        return std::nullopt;

    // <number>[.<fraction>][ ]<unit>+<delta>:
    r.stampBegin = i;
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    std::int64_t whole = 0;
    std::size_t digits = 0;
    for (; i < s.size() && isDigit(s[i]); ++i, ++digits) {
        const int d = s[i] - '0';
        if (whole > (kMax - d) / 10)
            return std::nullopt;
        whole = whole * 10 + d;
    }
    if (digits == 0)
        return std::nullopt;
    std::string_view fraction;
    if (i < s.size() && s[i] == '.') {
        const std::size_t f0 = ++i;
        while (i < s.size() && isDigit(s[i]))
            ++i;
        fraction = s.substr(f0, i - f0);
        if (fraction.empty())
            return std::nullopt;
    }
    if (i < s.size() && s[i] == ' ')
        ++i;
    const std::size_t u0 = i;
    while (i < s.size() && s[i] >= 'a' && s[i] <= 'z')
        ++i;
    const auto unit = unitToFemtoseconds(s.substr(u0, i - u0));
    if (!unit)
        return std::nullopt;
    if (whole > kMax / *unit)
        return std::nullopt;
    std::int64_t fs = whole * *unit;

    // A fraction must be a whole number of femtoseconds.
    std::int64_t scale = *unit;
    for (char c : fraction) {
        if (scale % 10 != 0) {
            if (c != '0')
                return std::nullopt;
            continue;
        }
        scale /= 10;
        const std::int64_t add = (c - '0') * scale;
        if (fs > kMax - add)
            return std::nullopt;
        fs += add;
    }

    if (i >= s.size() || s[i] != '+')
        return std::nullopt;
    ++i;
    long long delta = 0;
    std::size_t dd = 0;
    for (; i < s.size() && isDigit(s[i]); ++i, ++dd) {
        delta = delta * 10 + (s[i] - '0');
        if (delta > std::numeric_limits<int>::max())
            return std::nullopt;
    }
    if (dd == 0)
        return std::nullopt;
    r.stampEnd = i;
    if (s.substr(i, 2) != ": ")
        return std::nullopt;
    i += 2;
    r.timeFs = fs;
    r.delta = static_cast<int>(delta);
    r.message = std::string(s.substr(i));
    return r;
}

}  // namespace lvd::gui
