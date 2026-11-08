// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_map.hpp"

#include <algorithm>
#include <cctype>

namespace lvd::tools {

namespace {

std::string lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) {
            if (pos < text.size()) lines.emplace_back(text.substr(pos));
            break;
        }
        lines.emplace_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

bool isComment(const std::string& t) { return t.empty() || t[0] == ';' || t[0] == '#'; }

// "[Library]" -> "library"; empty if the line is not a section header.
std::string sectionName(const std::string& trimmedLine) {
    if (trimmedLine.size() >= 2 && trimmedLine.front() == '[' && trimmedLine.back() == ']')
        return lower(trim(std::string_view(trimmedLine).substr(1, trimmedLine.size() - 2)));
    return {};
}

bool splitKeyValue(const std::string& line, std::string& key, std::string& value) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = trim(std::string_view(line).substr(0, eq));
    value = trim(std::string_view(line).substr(eq + 1));
    return !key.empty();
}

}  // namespace

LibraryMap LibraryMap::parse(std::string_view iniText) {
    LibraryMap m;
    bool inLibrary = false;
    for (const std::string& raw : splitLines(iniText)) {
        const std::string t = trim(raw);
        if (isComment(t)) continue;
        const std::string sec = sectionName(t);
        if (!sec.empty()) {
            inLibrary = sec == "library";
            continue;
        }
        std::string k, v;
        if (inLibrary && splitKeyValue(t, k, v)) m.set(k, v);
    }
    return m;
}

std::optional<std::string> LibraryMap::pathOf(std::string_view name) const {
    const std::string n = lower(name);
    for (const auto& [k, v] : entries_)
        if (lower(k) == n) return v;
    return std::nullopt;
}

void LibraryMap::set(std::string_view name, std::string path) {
    const std::string n = lower(name);
    for (auto& [k, v] : entries_) {
        if (lower(k) == n) {
            v = std::move(path);
            return;
        }
    }
    entries_.emplace_back(std::string(name), std::move(path));
}

std::string withLibraryMapping(std::string_view iniText, std::string_view name, std::string_view path) {
    const bool crlf = iniText.find("\r\n") != std::string_view::npos;
    const std::string eol = crlf ? "\r\n" : "\n";
    std::vector<std::string> lines = splitLines(iniText);
    for (std::string& l : lines)
        if (!l.empty() && l.back() == '\r') l.pop_back();

    const std::string entry = std::string(name) + " = " + std::string(path);
    const std::string key = lower(name);

    std::ptrdiff_t sectionStart = -1, sectionEnd = static_cast<std::ptrdiff_t>(lines.size());
    std::ptrdiff_t lastKey = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        const std::string sec = sectionName(t);
        if (!sec.empty()) {
            if (sectionStart >= 0) {
                sectionEnd = static_cast<std::ptrdiff_t>(i);
                break;
            }
            if (sec == "library") {
                sectionStart = static_cast<std::ptrdiff_t>(i);
                lastKey = sectionStart;
            }
            continue;
        }
        if (sectionStart < 0 || isComment(t)) continue;
        std::string k, v;
        if (splitKeyValue(t, k, v)) {
            if (lower(k) == key) {
                lines[i] = entry;
                goto done;
            }
            lastKey = static_cast<std::ptrdiff_t>(i);
        }
    }
    if (sectionStart >= 0) {
        lines.insert(lines.begin() + (lastKey + 1), entry);
        (void)sectionEnd;
    } else {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
        lines.emplace_back("[Library]");
        lines.push_back(entry);
    }
done:
    std::string out;
    for (const std::string& l : lines) out += l + eol;
    return out;
}

std::optional<std::string> resolveLibrary(std::string_view name, const LibraryMap& map,
                                          const std::function<bool(const std::string&)>& dirExists) {
    if (auto p = map.pathOf(name)) return p;
    const std::string n(name);
    if (dirExists(n)) return n;
    return std::nullopt;
}

}  // namespace lvd::tools
