// SPDX-License-Identifier: GPL-3.0-or-later
// Messages nvc prints while analysing or running a design, read back into a list (the Problems list of the
// GUI, the summary of lvdsim-cli compile, the error counts of a regression run). nvc's full message style is
// understood:
//
//   ** Error: no visible declaration for FOO
//      > src/a.vhd:5
//      |
//    5 |   foo <= 1;
//      |   ^^^ did you mean NOW?
//
// and the run time form `** Error: 10ns+0: text` followed by `   Process :tb:_p0 at src/tb.vhd:8`.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lvd::tools::diag {

enum class Severity { Note, Warning, Error, Failure, Fatal };
const char* severityName(Severity s);

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message; ///< first line, without the "** Error: " prefix
    std::string file;    ///< as nvc printed it; empty if the message has no location
    int line = 0;
    int column = 0;     ///< 1 based, from the ^ marker; 0 if unknown
    std::string detail; ///< the lines that followed (source excerpt, hints), without trailing newline
};

std::vector<Diagnostic> parse(std::string_view output);

/// Number of messages of each severity in the output ("** Error:" ... at the start of a line, also when the
/// simulator printed them with a time stamp).
struct Counts {
    int notes = 0, warnings = 0, errors = 0, failures = 0, fatals = 0;
    int problems() const noexcept { return errors + failures + fatals; }
};
Counts count(std::string_view output);

} // namespace lvd::tools::diag
