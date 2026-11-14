// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/search.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace lvd::wave {

namespace {

// ---- Sequences: what the walkers need from a change list -----------------------------------------------

struct SignalSeq {
    const Signal& s;
    std::size_t count() const noexcept { return s.changeCount(); }
    Time time(std::size_t i) const noexcept { return s.time(i); }
    std::size_t upper(Time t) const noexcept { return s.holdIndex(t) + 1; } // kNoIndex + 1 == 0
    std::size_t lower(Time t) const noexcept { return s.changesIn(t, t).first; }
};

struct BitsSeq {
    const BitsSequence& q;
    std::size_t count() const { return q.count(); }
    Time time(std::size_t i) const { return q.time(i); }
    std::size_t upper(Time t) const { // first index with time > t
        std::size_t lo = 0, hi = count();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (time(mid) <= t)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }
    std::size_t lower(Time t) const { // first index with time >= t
        std::size_t lo = 0, hi = count();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (time(mid) < t)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }
};

template <class Seq>
std::size_t groupEnd(const Seq& seq, std::size_t i) {
    const std::size_t n = seq.count();
    const Time t = seq.time(i);
    while (i + 1 < n && seq.time(i + 1) == t)
        ++i;
    return i;
}

template <class Seq>
std::size_t groupStart(const Seq& seq, std::size_t i) {
    const Time t = seq.time(i);
    while (i > 0 && seq.time(i - 1) == t)
        --i;
    return i;
}

/// Forward: index of the first change after `from` (0 without). Backward: one past the last change before
/// `from`.
template <class Seq>
std::size_t startIndex(const Seq& seq, std::optional<Time> from, SearchDirection dir) {
    if (!from)
        return dir == SearchDirection::Forward ? 0 : seq.count();
    return dir == SearchDirection::Forward ? seq.upper(*from) : seq.lower(*from);
}

// ---- Edges: the value at an event differs from the value at the event before ---------------------------

template <class Seq, class Same>
std::optional<Time> walkEdges(const Seq& seq, std::optional<Time> from, SearchDirection dir,
                              const Same& same) {
    const std::size_t n = seq.count();
    if (n == 0)
        return std::nullopt;
    const std::size_t s = startIndex(seq, from, dir);
    if (dir == SearchDirection::Forward) {
        std::size_t prev = s == 0 ? kNoIndex : s - 1; // s is the first change of a time step
        for (std::size_t i = s; i < n;) {
            const std::size_t g = groupEnd(seq, i);
            if (prev == kNoIndex || !same(g, prev))
                return seq.time(g);
            prev = g;
            i = g + 1;
        }
        return std::nullopt;
    }
    for (std::size_t i = s; i > 0;) {
        const std::size_t g = i - 1;
        const std::size_t gs = groupStart(seq, g);
        if (gs == 0 || !same(g, gs - 1))
            return seq.time(g);
        i = gs;
    }
    return std::nullopt;
}

// ---- "Becomes true": a state per change (1 true, 0 false, -1 not known, skipped) -----------------------

template <class Seq, class State>
std::optional<Time> walkBecomes(const Seq& seq, std::optional<Time> from, SearchDirection dir,
                                const State& state) {
    const std::size_t n = seq.count();
    if (n == 0)
        return std::nullopt;
    const std::size_t s = startIndex(seq, from, dir);
    if (dir == SearchDirection::Forward) {
        int prev = -1; // state of the last event before s (skipping unknown ones); -1: none, counts as false
        for (std::size_t i = s; i > 0 && prev < 0;) {
            const std::size_t g = i - 1;
            prev = state(g);
            i = groupStart(seq, g);
        }
        for (std::size_t i = s; i < n;) {
            const std::size_t g = groupEnd(seq, i);
            const int st = state(g);
            if (st >= 0) {
                if (st == 1 && prev != 1)
                    return seq.time(g);
                prev = st;
            }
            i = g + 1;
        }
        return std::nullopt;
    }
    std::optional<Time> candidate;
    for (std::size_t i = s; i > 0;) {
        const std::size_t g = i - 1;
        const int st = state(g);
        if (st == 1)
            candidate = seq.time(g);
        else if (st == 0 && candidate)
            return candidate;
        i = groupStart(seq, g);
    }
    return candidate; // true from the first known event on: that event is where it became true
}

// ---- Logic helpers ----------------------------------------------------------------------------------------

bool isOne(Logic v) noexcept {
    return v == Logic::L1 || v == Logic::H;
}
bool isZero(Logic v) noexcept {
    return v == Logic::L0 || v == Logic::L;
}

bool sameValue(const Signal& s, int bit, std::size_t a, std::size_t b) {
    switch (s.kind()) {
    case SignalKind::Real: {
        const double x = s.real(a), y = s.real(b);
        return x == y || (std::isnan(x) && std::isnan(y));
    }
    case SignalKind::String:
        return s.text(a) == s.text(b);
    case SignalKind::Logic:
        break;
    }
    if (bit >= 0)
        return s.bit(a, static_cast<std::uint32_t>(bit)) == s.bit(b, static_cast<std::uint32_t>(bit));
    for (std::uint32_t i = 0; i < s.width(); ++i)
        if (s.bit(a, i) != s.bit(b, i))
            return false;
    return true;
}

bool singleBit(const SearchTarget& t) {
    if (t.signal == nullptr || t.signal->kind() != SignalKind::Logic)
        return false;
    if (t.bit >= 0)
        return static_cast<std::uint32_t>(t.bit) < t.signal->width();
    return t.signal->width() == 1;
}

Logic bitOf(const SearchTarget& t, std::size_t change) {
    return t.signal->bit(change, t.bit >= 0 ? static_cast<std::uint32_t>(t.bit) : 0u);
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return std::string(s.substr(a, b - a));
}

// ---- Threshold crossings
// ------------------------------------------------------------------------------------

class Crossing {
public:
    Crossing(const Signal& s, const ValueQuery& q, const NumericIndex* idx)
        : s_(s)
        , thr_(q.threshold)
        , signed_(q.isSigned)
        , idx_(idx != nullptr && idx->valid() && idx->isSigned() == q.isSigned ? idx : nullptr)
        , n_(s.changeCount())
        , m_(idx_ != nullptr ? std::min(idx_->size(), n_) : 0) {}

    std::optional<Time> forward(std::optional<Time> from, SearchCondition c) const {
        const SignalSeq seq{s_};
        std::size_t j = startIndex(seq, from, SearchDirection::Forward);
        int prev = -1;
        for (std::size_t k = j; k > 0 && prev < 0;)
            prev = state(--k);
        if (prev < 0) { // nothing known before: the first known value only sets the state
            std::size_t k = j;
            while (k < n_ && state(k) < 0)
                ++k;
            if (k >= n_)
                return std::nullopt;
            const std::size_t g = groupEnd(seq, k);
            prev = lastKnown(k, g);
            j = g + 1;
        }
        while (j < n_) {
            const std::size_t k = firstOther(j, prev);
            if (k == kNoIndex)
                return std::nullopt;
            const std::size_t g = groupEnd(seq, k);
            const int st = lastKnown(k, g);
            if (st != prev) {
                if (wanted(c, st))
                    return s_.time(k);
                prev = st;
            }
            j = g + 1;
        }
        return std::nullopt;
    }

    std::optional<Time> backward(std::optional<Time> from, SearchCondition c) const {
        const SignalSeq seq{s_};
        std::size_t e = startIndex(seq, from, SearchDirection::Backward);
        std::size_t k0 = e;
        int cur = -1;
        while (k0 > 0 && cur < 0)
            cur = state(--k0);
        if (cur < 0)
            return std::nullopt;
        while (e > 0) {
            const std::size_t k = lastOther(e, cur);
            if (k == kNoIndex)
                return std::nullopt; // the same state since the beginning: no crossing
            std::size_t t = k + 1;
            while (t < e && state(t) < 0)
                ++t;
            if (t >= e)
                return std::nullopt; // cannot happen: the change that set `cur` is after k
            const std::size_t gs = groupStart(seq, t);
            int before = -1;
            for (std::size_t i = gs; i > 0 && before < 0;)
                before = state(--i);
            if (before >= 0 && before != cur) {
                if (wanted(c, cur))
                    return s_.time(t);
                cur = before;
            } else if (before < 0) {
                return std::nullopt;
            }
            e = gs;
        }
        return std::nullopt;
    }

private:
    static bool wanted(SearchCondition c, int newState) {
        return c == SearchCondition::CrossEither || (c == SearchCondition::CrossRising) == (newState == 1);
    }
    double value(std::size_t i) const { return i < m_ ? idx_->value(i) : numericValue(s_, i, signed_); }
    int state(std::size_t i) const {
        const double v = value(i);
        if (std::isnan(v))
            return -1;
        return v >= thr_ ? 1 : 0;
    }
    int lastKnown(std::size_t k,
                  std::size_t g) const { // state of the last known change in [k, g]; k is known
        for (std::size_t i = g; i > k; --i)
            if (const int st = state(i); st >= 0)
                return st;
        return state(k);
    }
    bool hit(const MinMax& r, int st) const {
        if (r.empty())
            return false;
        return st == 1 ? r.min < thr_ : r.max >= thr_;
    }
    /// First index >= j whose known state differs from st.
    std::size_t firstOther(std::size_t j, int st) const {
        std::size_t lo = j;
        if (idx_ != nullptr) {
            std::size_t len = 16;
            while (lo < m_) {
                std::size_t hi = std::min(m_, lo + len);
                if (hit(idx_->range(lo, hi), st)) {
                    while (hi - lo > 1) {
                        const std::size_t mid = lo + (hi - lo) / 2;
                        if (hit(idx_->range(lo, mid), st))
                            hi = mid;
                        else
                            lo = mid;
                    }
                    return lo;
                }
                lo = hi;
                len *= 2;
            }
        }
        for (std::size_t i = lo; i < n_; ++i)
            if (const int x = state(i); x >= 0 && x != st)
                return i;
        return kNoIndex;
    }
    /// Last index < e whose known state differs from st.
    std::size_t lastOther(std::size_t e, int st) const {
        std::size_t hi = e;
        for (; hi > m_; --hi)
            if (const int x = state(hi - 1); x >= 0 && x != st)
                return hi - 1;
        if (idx_ == nullptr)
            return kNoIndex;
        std::size_t len = 16;
        while (hi > 0) {
            std::size_t lo = hi > len ? hi - len : 0;
            if (hit(idx_->range(lo, hi), st)) {
                while (hi - lo > 1) {
                    const std::size_t mid = lo + (hi - lo) / 2;
                    if (hit(idx_->range(mid, hi), st))
                        lo = mid;
                    else
                        hi = mid;
                }
                return lo;
            }
            hi = lo;
            len *= 2;
        }
        return kNoIndex;
    }

    const Signal& s_;
    double thr_;
    bool signed_;
    const NumericIndex* idx_;
    std::size_t n_;
    std::size_t m_; ///< changes covered by the index
};

} // namespace

// ---- Edges
// ----------------------------------------------------------------------------------------------------

std::optional<Time> findEdge(const SearchTarget& target, std::optional<Time> from, SearchDirection dir,
                             EdgeKind kind) {
    if (target.signal == nullptr)
        return std::nullopt;
    const Signal& s = *target.signal;
    if (target.bit >= 0 &&
        (s.kind() != SignalKind::Logic || static_cast<std::uint32_t>(target.bit) >= s.width()))
        return std::nullopt;
    const SignalSeq seq{s};
    if (kind == EdgeKind::Any)
        return walkEdges(seq, from, dir,
                         [&](std::size_t a, std::size_t b) { return sameValue(s, target.bit, a, b); });
    if (!singleBit(target))
        return std::nullopt;
    const bool rising = kind == EdgeKind::Rising;
    return walkBecomes(seq, from, dir, [&](std::size_t i) {
        const Logic v = bitOf(target, i);
        return (rising ? isOne(v) : isZero(v)) ? 1 : 0;
    });
}

std::optional<Time> findEdge(const BitsSequence& q, std::optional<Time> from, SearchDirection dir) {
    const BitsSeq seq{q};
    return walkEdges(seq, from, dir, [&](std::size_t a, std::size_t b) { return q.bits(a) == q.bits(b); });
}

// ---- Value patterns
// --------------------------------------------------------------------------------------------

Result<ValuePattern> ValuePattern::parse(std::string_view text, Radix radix, SignalKind kind,
                                         std::uint32_t width) {
    using R = Result<ValuePattern>;
    ValuePattern p;
    p.kind_ = kind;
    p.width_ = kind == SignalKind::Logic ? std::max<std::uint32_t>(width, 1) : 1;
    const std::string t = trim(text);
    if (t.empty())
        return R::failure("enter a value");

    if (kind == SignalKind::String) {
        p.text_ = lower(t);
        return p;
    }
    if (kind == SignalKind::Real) {
        char* end = nullptr;
        p.real_ = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0')
            return R::failure("\"" + t + "\" is not a number");
        return p;
    }

    std::string v;
    for (char c : t)
        if (c != '_' && !std::isspace(static_cast<unsigned char>(c)))
            v += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::uint32_t w = p.width_;
    if (radix == Radix::Default)
        radix = w == 1 ? Radix::Binary : Radix::Hex;
    const auto tooWide = [&] {
        return R::failure("\"" + t + "\" has more bits than the signal (" + std::to_string(w) + ")");
    };
    const auto isWild = [](char c) { return c == '?' || c == '-'; };

    switch (radix) {
    case Radix::Binary: {
        if (v.size() > 2 && v[0] == '0' && v[1] == 'B')
            v.erase(0, 2);
        for (char c : v)
            if (std::string_view("01XZUWLH?-").find(c) == std::string_view::npos)
                return R::failure("\"" + t + "\" is not a binary value (0 1 X Z U W L H, ? for any bit)");
        const char lead = v.front();
        const char pad = (lead == 'X' || lead == 'Z' || lead == 'U') ? lead : '0';
        for (std::uint32_t b = 0; b < std::max<std::size_t>(w, v.size()); ++b) {
            const char c = b < v.size() ? v[v.size() - 1 - b] : pad;
            if (b >= w) {
                if (c != '0' && !isWild(c))
                    return tooWide();
                continue;
            }
            Group g;
            g.lo = b;
            g.count = 1;
            if (isWild(c))
                g.kind = GroupKind::Any;
            else if (c == '0' || c == '1')
                g.value = c == '1' ? 1u : 0u;
            else
                g.exact = c;
            p.groups_.push_back(g);
        }
        return p;
    }
    case Radix::Octal:
    case Radix::Hex: {
        const std::uint32_t gs = radix == Radix::Hex ? 4 : 3;
        if (gs == 4 && v.size() > 2 && v[0] == '0' && v[1] == 'X')
            v.erase(0, 2);
        const std::uint32_t groups = (w + gs - 1) / gs;
        const char lead = v.front();
        const char pad = (lead == 'X' || lead == 'Z' || lead == 'U') ? lead : '0';
        for (std::uint32_t k = 0; k < std::max<std::size_t>(groups, v.size()); ++k) {
            const char c = k < v.size() ? v[v.size() - 1 - k] : pad;
            int digit = -1;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            const bool letter = c == 'X' || c == 'Z' || c == 'U' || isWild(c);
            if (!letter && (digit < 0 || digit >= (1 << gs)))
                return R::failure("\"" + t + "\" is not a" + (gs == 4 ? " hex" : "n octal") +
                                  " value (digits, X, Z, U, ? for any digit)");
            if (k >= groups) {
                if (digit != 0 && !isWild(c))
                    return tooWide();
                continue;
            }
            Group g;
            g.lo = k * gs;
            g.count = std::min(gs, w - g.lo);
            if (c == 'X')
                g.kind = GroupKind::Unknown;
            else if (c == 'Z')
                g.kind = GroupKind::AllZ;
            else if (c == 'U')
                g.kind = GroupKind::AllU;
            else if (isWild(c))
                g.kind = GroupKind::Any;
            else {
                if (static_cast<unsigned>(digit) >> g.count)
                    return tooWide();
                g.value = static_cast<std::uint64_t>(digit);
            }
            p.groups_.push_back(g);
        }
        return p;
    }
    case Radix::Unsigned:
    case Radix::Signed: {
        Group all;
        all.lo = 0;
        all.count = w;
        if (v == "X" || v == "Z" || v == "U" || v == "?") {
            all.kind = v == "X"   ? GroupKind::Unknown
                       : v == "Z" ? GroupKind::AllZ
                       : v == "U" ? GroupKind::AllU
                                  : GroupKind::Any;
            p.groups_.push_back(all);
            return p;
        }
        bool negative = false;
        if (v.front() == '-' || v.front() == '+') {
            negative = v.front() == '-';
            v.erase(0, 1);
        }
        if (v.empty() || !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return R::failure("\"" + t + "\" is not a decimal number");
        if (negative && radix == Radix::Unsigned)
            return R::failure("\"" + t + "\" is negative; choose the signed radix");
        // Decimal digits to binary, least significant bit first.
        std::vector<int> dec;
        for (char c : v)
            dec.push_back(c - '0');
        std::vector<bool> bits;
        auto isZeroDec = [&] { return std::all_of(dec.begin(), dec.end(), [](int d) { return d == 0; }); };
        while (!isZeroDec()) {
            int rem = 0;
            for (int& d : dec) {
                const int cur = rem * 10 + d;
                d = cur / 2;
                rem = cur % 2;
            }
            bits.push_back(rem != 0);
        }
        const std::size_t mag = bits.size(); // magnitude needs this many bits
        if (radix == Radix::Unsigned) {
            if (mag > w)
                return tooWide();
        } else {
            // Positive: mag <= w - 1. Negative: magnitude <= 2^(w-1).
            const bool powerOfTwo = mag > 0 && std::count(bits.begin(), bits.end(), true) == 1;
            if (!negative && mag > w - 1 && mag != 0)
                return R::failure("\"" + t + "\" does not fit in " + std::to_string(w) + " signed bits");
            if (negative && (mag > w || (mag == w && !powerOfTwo)))
                return R::failure("\"" + t + "\" does not fit in " + std::to_string(w) + " signed bits");
        }
        bits.resize(w, false);
        if (negative) { // two's complement: invert and add one
            bool carry = true;
            for (std::size_t i = 0; i < w; ++i) {
                const bool b = !bits[i];
                bits[i] = b != carry;
                carry = b && carry;
            }
        }
        for (std::uint32_t lo = 0; lo < w; lo += 64) {
            Group g;
            g.lo = lo;
            g.count = std::min<std::uint32_t>(64, w - lo);
            for (std::uint32_t k = 0; k < g.count; ++k)
                if (bits[lo + k])
                    g.value |= std::uint64_t{1} << k;
            p.groups_.push_back(g);
        }
        return p;
    }
    case Radix::Ascii: {
        const std::string raw = trim(text); // case matters here
        const std::uint32_t groups = (w + 7) / 8;
        if (raw.size() > groups)
            return tooWide();
        for (std::uint32_t k = 0; k < groups; ++k) {
            Group g;
            g.lo = k * 8;
            g.count = std::min<std::uint32_t>(8, w - g.lo);
            if (k < raw.size()) {
                const char c = raw[raw.size() - 1 - k];
                if (c == '?')
                    g.kind = GroupKind::Unknown;
                else
                    g.value = static_cast<unsigned char>(c);
                if (g.kind == GroupKind::Value && (g.value >> g.count) != 0)
                    return tooWide();
            }
            p.groups_.push_back(g);
        }
        return p;
    }
    case Radix::Default:
        break;
    }
    return R::failure("unknown radix");
}

template <class BitAt>
bool ValuePattern::matchGroups(const BitAt& bitAt) const {
    for (const Group& g : groups_) {
        if (g.exact != 0) {
            if (toChar(bitAt(g.lo)) != g.exact)
                return false;
            continue;
        }
        switch (g.kind) {
        case GroupKind::Any:
            break;
        case GroupKind::Value:
            for (std::uint32_t k = 0; k < g.count; ++k) {
                const Logic b = bitAt(g.lo + k);
                if (!isKnown(b) || bitValue(b) != (((g.value >> k) & 1u) != 0))
                    return false;
            }
            break;
        case GroupKind::Unknown:
        case GroupKind::AllZ:
        case GroupKind::AllU: {
            bool allZ = true, allU = true, definite = true;
            for (std::uint32_t k = 0; k < g.count; ++k) {
                const Logic b = bitAt(g.lo + k);
                allZ = allZ && b == Logic::Z;
                allU = allU && b == Logic::U;
                definite = definite && isKnown(b);
            }
            if (g.kind == GroupKind::AllZ && !allZ)
                return false;
            if (g.kind == GroupKind::AllU && !allU)
                return false;
            if (g.kind == GroupKind::Unknown && (definite || allZ || allU))
                return false;
            break;
        }
        }
    }
    return true;
}

bool ValuePattern::matches(const SearchTarget& target, std::size_t change) const {
    if (target.signal == nullptr)
        return false;
    const Signal& s = *target.signal;
    switch (kind_) {
    case SignalKind::String:
        return s.kind() == SignalKind::String && lower(s.text(change)) == text_;
    case SignalKind::Real:
        return s.kind() == SignalKind::Real && s.real(change) == real_;
    case SignalKind::Logic:
        break;
    }
    if (s.kind() != SignalKind::Logic)
        return false;
    if (target.bit >= 0) {
        if (width_ != 1 || static_cast<std::uint32_t>(target.bit) >= s.width())
            return false;
        const auto b = static_cast<std::uint32_t>(target.bit);
        return matchGroups([&](std::uint32_t) { return s.bit(change, b); });
    }
    if (s.width() != width_)
        return false;
    return matchGroups([&](std::uint32_t i) { return s.bit(change, i); });
}

bool ValuePattern::matchesBits(std::string_view msbFirst) const {
    if (kind_ != SignalKind::Logic || msbFirst.size() != width_)
        return false;
    const std::size_t n = msbFirst.size();
    return matchGroups([&](std::uint32_t i) { return fromChar(msbFirst[n - 1 - i]); });
}

// ---- Value searches
// --------------------------------------------------------------------------------------------

std::optional<Time> findValue(const SearchTarget& target, const ValueQuery& query, std::optional<Time> from,
                              SearchDirection dir, const NumericIndex* index) {
    if (target.signal == nullptr)
        return std::nullopt;
    const Signal& s = *target.signal;
    const SignalSeq seq{s};
    switch (query.condition) {
    case SearchCondition::Equals:
    case SearchCondition::NotEquals: {
        if (!query.pattern)
            return std::nullopt;
        const ValuePattern& p = *query.pattern;
        const bool want = query.condition == SearchCondition::Equals;
        return walkBecomes(seq, from, dir,
                           [&](std::size_t i) { return p.matches(target, i) == want ? 1 : 0; });
    }
    case SearchCondition::CrossRising:
    case SearchCondition::CrossFalling:
    case SearchCondition::CrossEither: {
        if (target.bit >= 0)
            return std::nullopt;
        const bool numeric =
            s.kind() == SignalKind::Real || (s.kind() == SignalKind::Logic && s.width() <= 64);
        if (!numeric)
            return std::nullopt;
        const Crossing c(s, query, index);
        return dir == SearchDirection::Forward ? c.forward(from, query.condition)
                                               : c.backward(from, query.condition);
    }
    }
    return std::nullopt;
}

std::optional<Time> findValue(const BitsSequence& q, const ValueQuery& query, std::optional<Time> from,
                              SearchDirection dir) {
    if (!query.pattern ||
        (query.condition != SearchCondition::Equals && query.condition != SearchCondition::NotEquals))
        return std::nullopt;
    const ValuePattern& p = *query.pattern;
    const bool want = query.condition == SearchCondition::Equals;
    const BitsSeq seq{q};
    return walkBecomes(seq, from, dir,
                       [&](std::size_t i) { return p.matchesBits(q.bits(i)) == want ? 1 : 0; });
}

} // namespace lvd::wave
