// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lvd::gui {

/// Severity of an HDL report or assertion, in increasing order of importance.
enum class ReportSeverity { Note, Warning, Error, Failure };

/// A transcript line that NVC printed for a report or assert statement, for example
/// `** Note: 100ns+0: reset released` or `** Failure: 2us+0: stopped`.
struct ReportLine {
    ReportSeverity severity = ReportSeverity::Note;
    std::int64_t timeFs = 0;     ///< simulation time of the report in femtoseconds
    int delta = 0;               ///< delta cycle after the time
    std::size_t stampBegin = 0;  ///< first character of the time stamp (`100ns+0`) in the line
    std::size_t stampEnd = 0;    ///< one past the last character of the time stamp
    std::string message;         ///< the text after the time stamp
};

/// Recognises one complete line (no line end). The shell's prompt ("% " one or more times) may precede it.
/// The time stamp is `<number><unit>+<delta>`: the unit is fs, ps, ns, us, ms or s, a space may separate number
/// and unit, and a fraction is accepted when it is a whole number of femtoseconds. Severities are Note,
/// Warning, Error, Failure; Fatal counts as Failure and Debug as Note. Lines without a time stamp (Verilog
/// `$display`, `(init):` reports, continuation lines, anything else) and times that overflow give nullopt.
/// Pure function: no Qt, never throws.
std::optional<ReportLine> parseReportLine(std::string_view line);

/// Femtoseconds of "100ns" style text pieces; exposed for tests. nullopt if the unit is unknown.
std::optional<std::int64_t> unitToFemtoseconds(std::string_view unit);

}  // namespace lvd::gui
