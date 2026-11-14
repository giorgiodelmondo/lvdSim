// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/memory.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <map>
#include <tuple>

namespace lvd::wave {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool parseInt(std::string_view s, std::int64_t& out) {
    if (s.empty())
        return false;
    const char* b = s.data();
    const char* e = s.data() + s.size();
    const auto r = std::from_chars(b, e, out);
    return r.ec == std::errc() && r.ptr == e;
}

// "[a:b]" with integers a and b
bool isRangeGroup(std::string_view g) {
    if (g.size() < 5 || g.front() != '[' || g.back() != ']')
        return false;
    const std::size_t colon = g.find(':');
    if (colon == std::string_view::npos)
        return false;
    std::int64_t a = 0, b = 0;
    return parseInt(g.substr(1, colon - 1), a) && parseInt(g.substr(colon + 1, g.size() - colon - 2), b);
}

} // namespace

bool parseElementName(std::string_view name, std::string& base, std::int64_t& index, std::string& range) {
    if (name.size() < 4 || name.back() != ']')
        return false;
    std::string_view rest = name;
    range.clear();
    std::size_t open = rest.rfind('[');
    if (open == std::string_view::npos)
        return false;
    if (isRangeGroup(rest.substr(open))) {
        range = std::string(rest.substr(open));
        rest = rest.substr(0, open);
        if (rest.empty() || rest.back() != ']')
            return false;
        open = rest.rfind('[');
        if (open == std::string_view::npos)
            return false;
    }
    if (open == 0 || !parseInt(rest.substr(open + 1, rest.size() - open - 2), index))
        return false;
    base = std::string(rest.substr(0, open));
    return true;
}

std::vector<MemoryInfo> findMemories(const WaveStore& store, std::size_t minWords) {
    std::vector<MemoryInfo> out;
    std::string base, range;
    for (ScopeId sc = 0; sc < store.scopeCount(); ++sc) {
        struct Group {
            std::string range;
            std::uint32_t width = 0;
            bool valid = true;
            std::vector<std::pair<std::int64_t, SignalId>> elems; // dump order
        };
        std::map<std::string, Group> groups;
        for (SignalId id : store.scope(sc).signalIds) {
            std::int64_t index = 0;
            if (!parseElementName(store.entry(id).name(), base, index, range))
                continue;
            const Signal& s = store.signal(id);
            Group& g = groups[base];
            if (g.elems.empty()) {
                g.range = range;
                g.width = s.width();
            }
            if (s.kind() != SignalKind::Logic || s.width() != g.width || range != g.range)
                g.valid = false;
            g.elems.emplace_back(index, id);
        }
        for (auto& [name, g] : groups) {
            if (!g.valid || g.elems.size() < std::max<std::size_t>(minWords, 1))
                continue;
            std::int64_t lo = g.elems.front().first, hi = lo;
            for (const auto& e : g.elems) {
                lo = std::min(lo, e.first);
                hi = std::max(hi, e.first);
            }
            // Sparse groups (names that only look like elements) are not memories.
            const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1;
            if (span > 2 * g.elems.size() + 16)
                continue;
            MemoryInfo m;
            m.scope = sc;
            m.name = name;
            m.elementRange = g.range;
            m.width = g.width;
            m.left = g.elems.front().first;
            m.right = g.elems.back().first;
            m.low = lo;
            m.words.assign(static_cast<std::size_t>(span), kNoSignal);
            for (const auto& e : g.elems)
                m.words[static_cast<std::size_t>(e.first - lo)] = e.second;
            const std::string scopePath = store.scopePath(sc);
            m.path = scopePath.empty() ? name : scopePath + "." + name;
            out.push_back(std::move(m));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const MemoryInfo& a, const MemoryInfo& b) { return a.path < b.path; });
    return out;
}

std::optional<MemoryInfo> findMemory(const WaveStore& store, std::string_view path) {
    std::string p(path);
    while (!p.empty() && p.front() == '/')
        p.erase(p.begin());
    std::replace(p.begin(), p.end(), '/', '.');
    p = lower(p);
    for (MemoryInfo& m : findMemories(store, 1))
        if (lower(m.path) == p)
            return std::move(m);
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------

MemoryImage::MemoryImage(const WaveStore& store, MemoryInfo info)
    : store_(store)
    , info_(std::move(info)) {
    const std::size_t n = info_.words.size();
    signals_.resize(n, nullptr);
    for (std::size_t w = 0; w < n; ++w)
        if (info_.words[w] != kNoSignal)
            signals_[w] = &store_.signal(info_.words[w]);
    scanned_.assign(n, 0);
    changes_.resize(n);
    update();
}

bool MemoryImage::update() {
    std::uint64_t generation = 0;
    for (const Signal* s : signals_)
        if (s != nullptr)
            generation += s->generation();
    bool reset = false;
    if (generation != generation_) {
        // The words were cleared (a restarted live run): the writes start again
        std::fill(scanned_.begin(), scanned_.end(), 0);
        for (auto& c : changes_)
            c.clear();
        writes_.clear();
        generation_ = generation;
        reset = true;
    }
    std::vector<Write> batch;
    for (std::size_t w = 0; w < signals_.size(); ++w) {
        const Signal* s = signals_[w];
        if (s == nullptr)
            continue;
        const std::size_t count = s->changeCount();
        std::size_t i = scanned_[w];
        if (i == count)
            continue;
        if (i == 0)
            i = 1; // the first value is the initial value
        for (; i < count; ++i) {
            if (s->sameValue(i, i - 1))
                continue;
            changes_[w].push_back(static_cast<std::uint32_t>(i));
            batch.push_back({s->time(i), static_cast<std::uint32_t>(w)});
        }
        scanned_[w] = count;
    }
    if (batch.empty())
        return reset;
    const auto less = [](const Write& a, const Write& b) {
        return std::tie(a.time, a.word) < std::tie(b.time, b.word);
    };
    std::stable_sort(batch.begin(), batch.end(), less);
    // New writes are almost always later than the old ones: only the overlapping tail is merged.
    const auto start = std::upper_bound(writes_.begin(), writes_.end(), batch.front(), less);
    const auto offset = start - writes_.begin();
    const auto mid = static_cast<std::ptrdiff_t>(writes_.size());
    writes_.insert(writes_.end(), batch.begin(), batch.end());
    std::inplace_merge(writes_.begin() + offset, writes_.begin() + mid, writes_.end(), less);
    return true;
}

const Signal* MemoryImage::wordSignal(std::size_t word) const noexcept {
    return word < signals_.size() ? signals_[word] : nullptr;
}

std::size_t MemoryImage::changeAt(std::size_t word, Time t) const noexcept {
    const Signal* s = wordSignal(word);
    return s == nullptr ? kNoIndex : s->holdIndex(t);
}

std::string MemoryImage::bits(std::size_t word, Time t) const {
    const std::size_t c = changeAt(word, t);
    return c == kNoIndex ? std::string() : signals_[word]->bits(c);
}

std::string MemoryImage::format(std::size_t word, Time t, Radix radix) const {
    const std::size_t c = changeAt(word, t);
    return c == kNoIndex ? std::string("-") : formatValue(*signals_[word], c, radix);
}

std::optional<Time> MemoryImage::lastWrite(std::size_t word, Time t) const noexcept {
    if (word >= changes_.size())
        return std::nullopt;
    const std::size_t n = writeCount(word, t);
    if (n == 0)
        return std::nullopt;
    return signals_[word]->time(changes_[word][n - 1]);
}

std::size_t MemoryImage::writeCount(std::size_t word, Time t) const noexcept {
    if (word >= changes_.size())
        return 0;
    const auto& c = changes_[word];
    const Signal* s = signals_[word];
    const auto it =
        std::upper_bound(c.begin(), c.end(), t, [s](Time v, std::uint32_t idx) { return v < s->time(idx); });
    return static_cast<std::size_t>(it - c.begin());
}

std::vector<std::size_t> MemoryImage::wordsWrittenAt(Time t) const {
    const auto lo = std::lower_bound(writes_.begin(), writes_.end(), t,
                                     [](const Write& w, Time v) { return w.time < v; });
    std::vector<std::size_t> out;
    for (auto it = lo; it != writes_.end() && it->time == t; ++it)
        if (out.empty() || out.back() != it->word)
            out.push_back(it->word);
    return out;
}

std::optional<Time> MemoryImage::lastWriteTime(Time t) const noexcept {
    const auto it = std::upper_bound(writes_.begin(), writes_.end(), t,
                                     [](Time v, const Write& w) { return v < w.time; });
    if (it == writes_.begin())
        return std::nullopt;
    return std::prev(it)->time;
}

std::optional<Time> MemoryImage::nextWriteTime(Time t) const noexcept {
    const auto it = std::upper_bound(writes_.begin(), writes_.end(), t,
                                     [](Time v, const Write& w) { return v < w.time; });
    if (it == writes_.end())
        return std::nullopt;
    return it->time;
}

std::optional<Time> MemoryImage::previousWriteTime(Time t) const noexcept {
    const auto it = std::lower_bound(writes_.begin(), writes_.end(), t,
                                     [](const Write& w, Time v) { return w.time < v; });
    if (it == writes_.begin())
        return std::nullopt;
    return std::prev(it)->time;
}

std::optional<std::size_t> MemoryImage::findWord(const ValuePattern& pattern, Time t,
                                                 std::optional<std::size_t> from, SearchDirection dir,
                                                 bool wrap) const {
    const std::size_t n = depth();
    if (n == 0)
        return std::nullopt;
    const auto matches = [&](std::size_t w) {
        const std::size_t c = changeAt(w, t);
        return c != kNoIndex && pattern.matches(SearchTarget{signals_[w], -1}, c);
    };
    const bool fwd = dir == SearchDirection::Forward;
    const auto last = static_cast<std::int64_t>(n) - 1;
    // Start just outside the range without `from`, so that the first step lands on the first (last) word.
    std::int64_t pos = from ? std::min(static_cast<std::int64_t>(*from), last) : (fwd ? -1 : last + 1);
    for (std::size_t k = 0; k < n; ++k) {
        pos += fwd ? 1 : -1;
        if (pos < 0 || pos > last) {
            if (!wrap)
                return std::nullopt;
            pos = fwd ? 0 : last;
        }
        if (matches(static_cast<std::size_t>(pos)))
            return static_cast<std::size_t>(pos);
    }
    return std::nullopt;
}

std::string MemoryImage::memoryFileText(Time t, Radix radix, unsigned wordsPerLine,
                                        std::string_view timeText) const {
    if (radix != Radix::Binary)
        radix = Radix::Hex;
    if (wordsPerLine == 0)
        wordsPerLine = 1;
    std::string out = "// " + info_.path;
    out += " [" + std::to_string(info_.low) + ":" + std::to_string(info_.high()) + "], " +
           std::to_string(info_.width) + " bit words";
    if (!timeText.empty())
        out += ", at " + std::string(timeText);
    out += "\n// written by lvdsim; read it with $readmem";
    out += radix == Radix::Binary ? "b\n" : "h\n";
    out += "@" + formatAddress(info_.low, Radix::Hex) + "\n";
    const std::size_t digits = radix == Radix::Binary ? info_.width : (info_.width + 3) / 4;
    for (std::size_t w = 0; w < depth(); ++w) {
        const std::string b = bits(w, t);
        std::string text = b.empty() ? std::string(digits, 'x') : lower(formatBits(b, radix));
        for (char& c : text)
            if (c != 'z' && !std::isxdigit(static_cast<unsigned char>(c)))
                c = 'x'; // u, w, -: $readmem knows x and z only
        out += text;
        out += (w + 1) % wordsPerLine == 0 || w + 1 == depth() ? '\n' : ' ';
    }
    return out;
}

// ---------------------------------------------------------------------------------------------

Result<AddMemRequest> parseAddMem(const std::vector<std::string>& words) {
    using R = Result<AddMemRequest>;
    AddMemRequest req;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string w = lower(words[i]);
        const auto needValue = [&]() -> const std::string* {
            return i + 1 < words.size() ? &words[++i] : nullptr;
        };
        if (w == "-addressradix" || w == "-dataradix" || w == "-radix") {
            const std::string* v = needValue();
            if (v == nullptr)
                return R::failure("add mem: " + words[i] + " needs a value");
            Radix r;
            if (!parseRadix(*v, r))
                return R::failure("add mem: unknown radix '" + *v + "'");
            if (w == "-addressradix")
                req.addressRadix = r;
            else
                req.dataRadix = r;
        } else if (w == "-wordsperline") {
            const std::string* v = needValue();
            std::int64_t n = 0;
            if (v == nullptr || !parseInt(*v, n) || n < 1 || n > 256)
                return R::failure("add mem: -wordsperline needs a number from 1 to 256");
            req.wordsPerLine = static_cast<unsigned>(n);
        } else if (w.size() > 1 && w[0] == '-') {
            return R::failure("add mem: unknown option '" + words[i] + "'");
        } else {
            req.paths.push_back(words[i]);
        }
    }
    return req;
}

std::string formatAddress(std::int64_t address, Radix radix, int digits) {
    std::string s;
    const bool neg = address < 0;
    std::uint64_t v = neg ? 0 - static_cast<std::uint64_t>(address) : static_cast<std::uint64_t>(address);
    unsigned base = 10;
    switch (radix) {
    case Radix::Binary:
        base = 2;
        break;
    case Radix::Octal:
        base = 8;
        break;
    case Radix::Hex:
    case Radix::Default:
    case Radix::Ascii:
        base = 16;
        break;
    case Radix::Unsigned:
    case Radix::Signed:
        base = 10;
        break;
    }
    do {
        const unsigned d = static_cast<unsigned>(v % base);
        s.insert(s.begin(), static_cast<char>(d < 10 ? '0' + d : 'A' + d - 10));
        v /= base;
    } while (v != 0);
    if (base != 10)
        while (static_cast<int>(s.size()) < digits)
            s.insert(s.begin(), '0');
    if (neg)
        s.insert(s.begin(), '-');
    return s;
}

std::optional<std::int64_t> parseAddress(std::string_view text, Radix radix) {
    std::string t;
    for (char c : text)
        if (c != '_' && !std::isspace(static_cast<unsigned char>(c)))
            t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.empty())
        return std::nullopt;
    bool neg = false;
    if (t[0] == '-') {
        neg = true;
        t.erase(0, 1);
    }
    int base = 10;
    switch (radix) {
    case Radix::Binary:
        base = 2;
        break;
    case Radix::Octal:
        base = 8;
        break;
    case Radix::Hex:
    case Radix::Default:
    case Radix::Ascii:
        base = 16;
        break;
    default:
        break;
    }
    const auto strip = [&](std::string_view prefix, int b) {
        if (t.size() > prefix.size() && t.compare(0, prefix.size(), prefix) == 0) {
            t.erase(0, prefix.size());
            base = b;
            return true;
        }
        return false;
    };
    if (!strip("0x", 16) && !strip("0b", 2) && !strip("0o", 8)) {
        // VHDL based literal 16#ff#, bit string x"ff"
        const std::size_t hash = t.find('#');
        if (hash != std::string::npos && t.back() == '#') {
            std::int64_t b = 0;
            if (!parseInt(t.substr(0, hash), b) || (b != 2 && b != 8 && b != 10 && b != 16))
                return std::nullopt;
            base = static_cast<int>(b);
            t = t.substr(hash + 1, t.size() - hash - 2);
        } else if (t.size() > 3 && (t[0] == 'x' || t[0] == 'b' || t[0] == 'o') && t[1] == '"' &&
                   t.back() == '"') {
            base = t[0] == 'x' ? 16 : t[0] == 'b' ? 2 : 8;
            t = t.substr(2, t.size() - 3);
        }
    }
    if (t.empty())
        return std::nullopt;
    std::uint64_t v = 0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v, base);
    if (r.ec != std::errc() || r.ptr != t.data() + t.size() || v > static_cast<std::uint64_t>(INT64_MAX))
        return std::nullopt;
    return neg ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
}

} // namespace lvd::wave
