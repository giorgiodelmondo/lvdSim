// SPDX-License-Identifier: GPL-3.0-or-later
// Small string helpers shared by the project, scanner and regression code (header only, no state).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::tools::text {

inline std::string lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

inline bool startsWith(std::string_view s, std::string_view p) {
    return s.substr(0, p.size()) == p;
}

inline bool endsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

/// Splits at white space; a word in double quotes may contain spaces (a backslash before a quote or a
/// backslash inside quotes keeps it). Quotes in the middle of a word (name="a b") group as well.
inline std::vector<std::string> splitWords(std::string_view s) {
    std::vector<std::string> out;
    std::string word;
    bool inWord = false, quoted = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted) {
            if (c == '\\' && i + 1 < s.size() && (s[i + 1] == '"' || s[i + 1] == '\\')) {
                word += s[++i];
            } else if (c == '"') {
                quoted = false;
            } else {
                word += c;
            }
        } else if (c == '"') {
            quoted = true;
            inWord = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (inWord)
                out.push_back(word);
            word.clear();
            inWord = false;
        } else {
            word += c;
            inWord = true;
        }
    }
    if (inWord)
        out.push_back(word);
    return out;
}

/// The word quoted for splitWords when it is empty or contains white space, quotes or backslashes.
inline std::string quoteWord(std::string_view w) {
    const bool plain = !w.empty() && std::none_of(w.begin(), w.end(), [](unsigned char c) {
        return std::isspace(c) != 0 || c == '"' || c == '\\';
    });
    if (plain)
        return std::string(w);
    std::string r = "\"";
    for (const char c : w) {
        if (c == '"' || c == '\\')
            r += '\\';
        r += c;
    }
    return r + "\"";
}

/// 64-bit FNV-1a, used for content hashes of source files (not cryptographic; detects edits).
inline std::uint64_t fnv1a(std::string_view data, std::uint64_t h = 1469598103934665603ULL) {
    for (const char c : data) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

/// Hash with a separator so that {"ab","c"} and {"a","bc"} differ.
inline std::uint64_t hashWords(const std::vector<std::string>& words) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const std::string& w : words) {
        h = fnv1a(w, h);
        h = fnv1a(std::string_view("\x1f", 1), h);
    }
    return h;
}

} // namespace lvd::tools::text
