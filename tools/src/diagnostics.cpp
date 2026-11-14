// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostics.hpp"

#include "text_util.hpp"

#include <cctype>
#include <optional>

namespace lvd::tools::diag {

const char* severityName(Severity s) {
    switch (s) {
    case Severity::Note:
        return "Note";
    case Severity::Warning:
        return "Warning";
    case Severity::Error:
        return "Error";
    case Severity::Failure:
        return "Failure";
    case Severity::Fatal:
        return "Fatal";
    }
    return "";
}

namespace {

std::vector<std::string_view> lines(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const std::size_t nl = s.find('\n', pos);
        std::string_view l = s.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
        out.push_back(l);
        pos = nl == std::string_view::npos ? s.size() : nl + 1;
    }
    return out;
}

/// "** Error: text" -> severity and text. Also accepts a leading time stamp form "** Error: 10ns+0: text"
/// (kept in the text).
std::optional<std::pair<Severity, std::string_view>> header(std::string_view l) {
    static const std::pair<const char*, Severity> kinds[] = {{"** Note:", Severity::Note},
                                                             {"** Warning:", Severity::Warning},
                                                             {"** Error:", Severity::Error},
                                                             {"** Failure:", Severity::Failure},
                                                             {"** Fatal:", Severity::Fatal}};
    for (const auto& [prefix, sev] : kinds)
        if (text::startsWith(l, prefix))
            return std::make_pair(sev, text::trim(l.substr(std::char_traits<char>::length(prefix))));
    return std::nullopt;
}

/// "file:line" or "file:line:col" at the end of `s` (file may contain a drive letter's colon).
bool location(std::string_view s, std::string& file, int& line, int& col) {
    s = text::trim(s);
    std::vector<int> nums;
    std::string_view rest = s;
    for (int k = 0; k < 2; ++k) {
        const std::size_t c = rest.rfind(':');
        if (c == std::string_view::npos || c + 1 >= rest.size())
            break;
        const std::string_view tail = rest.substr(c + 1);
        bool digits = !tail.empty();
        for (const char ch : tail)
            digits = digits && std::isdigit(static_cast<unsigned char>(ch)) != 0;
        if (!digits)
            break;
        nums.insert(nums.begin(), std::stoi(std::string(tail)));
        rest = rest.substr(0, c);
    }
    if (nums.empty() || rest.empty())
        return false;
    // A single letter before the colon would be a drive letter alone: not a file.
    if (rest.size() == 1 && std::isalpha(static_cast<unsigned char>(rest[0])) != 0)
        return false;
    file = std::string(rest);
    line = nums[0];
    col = nums.size() > 1 ? nums[1] : 0;
    return true;
}

} // namespace

std::vector<Diagnostic> parse(std::string_view output) {
    std::vector<Diagnostic> out;
    const std::vector<std::string_view> ls = lines(output);
    for (std::size_t i = 0; i < ls.size(); ++i) {
        const auto h = header(ls[i]);
        if (!h)
            continue;
        Diagnostic d;
        d.severity = h->first;
        d.message = std::string(h->second);
        std::string detail;
        int gutter = -1; // column of '|' in the excerpt
        std::size_t k = i + 1;
        for (; k < ls.size() && !header(ls[k]); ++k) {
            const std::string_view l = ls[k];
            // Continuation lines are indented; anything else is other output.
            if (l.empty() || (l[0] != ' ' && l[0] != '\t'))
                break;
            const std::string_view t = text::trim(l);
            if (d.file.empty() && text::startsWith(t, "> ")) {
                location(t.substr(2), d.file, d.line, d.column);
            } else if (d.file.empty() && t.find(" at ") != std::string_view::npos) {
                // "Process :tb:_p0 at src/tb.vhd:8"
                location(t.substr(t.rfind(" at ") + 4), d.file, d.line, d.column);
            }
            const std::size_t bar = l.find('|');
            if (bar != std::string_view::npos && gutter < 0) {
                bool numbered = true; // " 5 |" source line or "   |" spacer
                for (std::size_t c = 0; c < bar; ++c)
                    numbered =
                        numbered && (l[c] == ' ' || std::isdigit(static_cast<unsigned char>(l[c])) != 0);
                if (numbered)
                    gutter = static_cast<int>(bar);
            }
            if (gutter >= 0 && d.column == 0 && static_cast<int>(l.size()) > gutter + 2 &&
                l[static_cast<std::size_t>(gutter)] == '|') {
                const std::size_t caret = l.find('^', static_cast<std::size_t>(gutter));
                if (caret != std::string_view::npos &&
                    l.find_first_not_of(" ^", static_cast<std::size_t>(gutter) + 1) >= caret)
                    d.column = static_cast<int>(caret) - gutter - 1;
            }
            if (!detail.empty())
                detail += '\n';
            detail += l;
        }
        d.detail = detail;
        out.push_back(std::move(d));
        i = k - 1;
    }
    return out;
}

Counts count(std::string_view output) {
    Counts c;
    for (const std::string_view l : lines(output)) {
        const auto h = header(l);
        if (!h)
            continue;
        switch (h->first) {
        case Severity::Note:
            ++c.notes;
            break;
        case Severity::Warning:
            ++c.warnings;
            break;
        case Severity::Error:
            ++c.errors;
            break;
        case Severity::Failure:
            ++c.failures;
            break;
        case Severity::Fatal:
            ++c.fatals;
            break;
        }
    }
    return c;
}

} // namespace lvd::tools::diag
