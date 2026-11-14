// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/search.hpp>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

using namespace lvd::wave;

namespace {

constexpr auto Fwd = SearchDirection::Forward;
constexpr auto Bwd = SearchDirection::Backward;

ValuePattern pat(std::string_view text, Radix r, std::uint32_t width, SignalKind k = SignalKind::Logic) {
    auto p = ValuePattern::parse(text, r, k, width);
    REQUIRE(p.ok());
    return p.value();
}

ValueQuery eq(std::string_view text, Radix r, std::uint32_t width, bool notEquals = false) {
    ValueQuery q;
    q.condition = notEquals ? SearchCondition::NotEquals : SearchCondition::Equals;
    q.pattern = pat(text, r, width);
    return q;
}

// Brute force reference: events = distinct times with the value of their last change.
struct Event {
    Time t;
    std::size_t last;
};
std::vector<Event> events(const Signal& s) {
    std::vector<Event> ev;
    for (std::size_t i = 0; i < s.changeCount(); ++i) {
        if (!ev.empty() && ev.back().t == s.time(i))
            ev.back().last = i;
        else
            ev.push_back({s.time(i), i});
    }
    return ev;
}

} // namespace

TEST_CASE("edges of a clock, forward and backward") {
    WaveStore st;
    Signal& clk = st.mutableSignal(st.addSignal(kRootScope, "clk", SignalKind::Logic, 1));
    for (Time t = 0; t <= 100; t += 10)
        clk.appendLogic(t, (t / 10) % 2 ? "1" : "0");
    const SearchTarget c{&clk};

    REQUIRE(findEdge(c, std::nullopt, Fwd) == Time{0});
    REQUIRE(findEdge(c, Time{0}, Fwd) == Time{10});
    REQUIRE(findEdge(c, Time{15}, Fwd) == Time{20});
    REQUIRE(findEdge(c, Time{100}, Fwd) == std::nullopt);
    REQUIRE(findEdge(c, Time{15}, Bwd) == Time{10});
    REQUIRE(findEdge(c, Time{10}, Bwd) == Time{0});
    REQUIRE(findEdge(c, Time{0}, Bwd) == std::nullopt);
    REQUIRE(findEdge(c, std::nullopt, Bwd) == Time{100});

    // Rising: 0 -> 1 at 10, 30, 50 ...; falling at 20, 40 ...; the first value 0 counts as falling.
    REQUIRE(findEdge(c, Time{0}, Fwd, EdgeKind::Rising) == Time{10});
    REQUIRE(findEdge(c, Time{10}, Fwd, EdgeKind::Rising) == Time{30});
    REQUIRE(findEdge(c, Time{10}, Fwd, EdgeKind::Falling) == Time{20});
    REQUIRE(findEdge(c, Time{30}, Bwd, EdgeKind::Rising) == Time{10});
    REQUIRE(findEdge(c, Time{30}, Bwd, EdgeKind::Falling) == Time{20});
    REQUIRE(findEdge(c, Time{10}, Bwd, EdgeKind::Falling) == Time{0});
    REQUIRE(findEdge(c, Time{10}, Bwd, EdgeKind::Rising) == std::nullopt);
}

TEST_CASE("repeated values and delta glitches are not edges") {
    WaveStore st;
    Signal& s = st.mutableSignal(st.addSignal(kRootScope, "s", SignalKind::Logic, 4));
    s.appendLogic(0, "0000");
    s.appendLogic(5, "0000"); // same value again: no edge
    s.appendLogic(10, "0001");
    s.appendLogic(10, "0000"); // glitch at 10 ends where it started
    s.appendLogic(20, "0001");
    s.appendLogic(20, "0010"); // two deltas, value at 20 is 0010
    const SearchTarget t{&s};
    REQUIRE(findEdge(t, Time{0}, Fwd) == Time{20});
    REQUIRE(findEdge(t, std::nullopt, Bwd) == Time{20});
    REQUIRE(findEdge(t, Time{20}, Bwd) == Time{0});

    // One bit of the vector: bit 1 rises at 20, bit 0 stays 0 at the end of every step.
    REQUIRE(findEdge({&s, 1}, Time{0}, Fwd, EdgeKind::Rising) == Time{20});
    REQUIRE(findEdge({&s, 0}, Time{0}, Fwd) == std::nullopt);
    // Rising/falling need a single bit.
    REQUIRE(findEdge(t, Time{0}, Fwd, EdgeKind::Rising) == std::nullopt);
}

TEST_CASE("value patterns in each radix") {
    WaveStore st;
    Signal& s = st.mutableSignal(st.addSignal(kRootScope, "s", SignalKind::Logic, 8));
    s.appendLogic(0, "UUUUUUUU");
    s.appendLogic(1, "10100101"); // A5
    s.appendLogic(2, "1010XXXX"); // AX
    s.appendLogic(3, "ZZZZZZZZ");
    s.appendLogic(4, "11111111"); // -1 / 255
    s.appendLogic(5, "01000001"); // 'A'
    s.appendLogic(6, "0000HL01");
    const SearchTarget t{&s};
    auto m = [&](std::string_view text, Radix r, std::size_t change) {
        return pat(text, r, 8).matches(t, change);
    };

    CHECK(m("A5", Radix::Hex, 1));
    CHECK(m("a5", Radix::Hex, 1));
    CHECK(m("0xA5", Radix::Hex, 1));
    CHECK_FALSE(m("A4", Radix::Hex, 1));
    CHECK(m("A?", Radix::Hex, 1));
    CHECK(m("AX", Radix::Hex, 2));
    CHECK_FALSE(m("AX", Radix::Hex, 1));
    CHECK(m("A-", Radix::Hex, 2));
    CHECK(m("Z", Radix::Hex, 3)); // left-extended with Z
    CHECK(m("ZZ", Radix::Hex, 3));
    CHECK_FALSE(m("X", Radix::Hex, 3));
    CHECK(m("U", Radix::Hex, 0));
    CHECK(m("1010_0101", Radix::Binary, 1));
    CHECK(m("1010????", Radix::Binary, 2));
    CHECK(m("1010XXXX", Radix::Binary, 2));
    CHECK(m("0000HL01", Radix::Binary, 6));
    CHECK(m("00001001", Radix::Binary, 6)); // 1 matches H, 0 matches L
    CHECK(m("245", Radix::Octal, 1));
    CHECK(m("165", Radix::Unsigned, 1));
    CHECK(m("255", Radix::Unsigned, 4));
    CHECK(m("-1", Radix::Signed, 4));
    CHECK(m("-91", Radix::Signed, 1));
    CHECK(m("X", Radix::Unsigned, 2));
    CHECK(m("Z", Radix::Unsigned, 3));
    CHECK(m("U", Radix::Signed, 0));
    CHECK_FALSE(m("X", Radix::Unsigned, 3));
    CHECK(m("A", Radix::Ascii, 5));
    CHECK(m("5", Radix::Hex, 6) == false);
    CHECK(m("9", Radix::Hex, 6)); // H L read as 1 0

    // Refused values
    CHECK_FALSE(ValuePattern::parse("1A5", Radix::Hex, SignalKind::Logic, 8).ok());
    CHECK(ValuePattern::parse("0A5", Radix::Hex, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("256", Radix::Unsigned, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("128", Radix::Signed, SignalKind::Logic, 8).ok());
    CHECK(ValuePattern::parse("-128", Radix::Signed, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("-129", Radix::Signed, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("-1", Radix::Unsigned, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("G", Radix::Hex, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("8", Radix::Octal, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("", Radix::Hex, SignalKind::Logic, 8).ok());
    CHECK_FALSE(ValuePattern::parse("4", Radix::Hex, SignalKind::Logic, 2).ok()); // partial top group

    // Wide values in decimal
    Signal& w = st.mutableSignal(st.addSignal(kRootScope, "w", SignalKind::Logic, 80));
    w.appendLogic(0, "1" + std::string(79, '0')); // 2^79
    CHECK(pat("604462909807314587353088", Radix::Unsigned, 80).matches({&w}, 0));
    CHECK(pat("-604462909807314587353088", Radix::Signed, 80).matches({&w}, 0));
    CHECK(pat("8" + std::string(19, '0'), Radix::Hex, 80).matches({&w}, 0));

    // Bits of a virtual bus
    CHECK(pat("A5", Radix::Hex, 8).matchesBits("10100101"));
    CHECK_FALSE(pat("A5", Radix::Hex, 8).matchesBits("0100101"));
}

TEST_CASE("value search: starts of matching intervals") {
    WaveStore st;
    Signal& s = st.mutableSignal(st.addSignal(kRootScope, "cnt", SignalKind::Logic, 4));
    // 0 1 2 3 0 1 2 3 ... at 0, 10, 20, ...
    const char* const kValues[] = {"0000", "0001", "0010", "0011"};
    for (Time i = 0; i < 16; ++i)
        s.appendLogic(i * 10, kValues[i % 4]);
    const SearchTarget t{&s};
    const ValueQuery two = eq("2", Radix::Hex, 4);
    REQUIRE(findValue(t, two, std::nullopt, Fwd) == Time{20});
    REQUIRE(findValue(t, two, Time{20}, Fwd) == Time{60});
    REQUIRE(findValue(t, two, Time{25}, Fwd) == Time{60});
    REQUIRE(findValue(t, two, Time{60}, Bwd) == Time{20});
    REQUIRE(findValue(t, two, Time{20}, Bwd) == std::nullopt);
    REQUIRE(findValue(t, two, std::nullopt, Bwd) == Time{140});
    REQUIRE(findValue(t, two, Time{140}, Fwd) == std::nullopt);
    // Not equals 0: becomes true at 10, 50, 90 ...
    const ValueQuery not0 = eq("0", Radix::Hex, 4, true);
    REQUIRE(findValue(t, not0, Time{0}, Fwd) == Time{10});
    REQUIRE(findValue(t, not0, Time{10}, Fwd) == Time{50});
    REQUIRE(findValue(t, not0, Time{45}, Bwd) == Time{10});
    // The first change counts when it matches.
    REQUIRE(findValue(t, eq("0", Radix::Hex, 4), std::nullopt, Fwd) == Time{0});
    REQUIRE(findValue(t, eq("0", Radix::Hex, 4), Time{40}, Bwd) == Time{0});
}

TEST_CASE("string and real equality") {
    WaveStore st;
    Signal& e = st.mutableSignal(st.addSignal(kRootScope, "state", SignalKind::String, 0));
    e.appendText(0, "idle");
    e.appendText(5, "busy");
    e.appendText(9, "idle");
    ValueQuery q;
    q.pattern = pat("IDLE", Radix::Default, 0, SignalKind::String);
    REQUIRE(findValue({&e}, q, Time{0}, Fwd) == Time{9});
    REQUIRE(findEdge({&e}, Time{5}, Fwd) == Time{9});

    Signal& r = st.mutableSignal(st.addSignal(kRootScope, "r", SignalKind::Real, 1));
    r.appendReal(0, 0.5);
    r.appendReal(3, 1.25);
    q.pattern = pat("1.25", Radix::Default, 1, SignalKind::Real);
    REQUIRE(findValue({&r}, q, std::nullopt, Fwd) == Time{3});
    REQUIRE_FALSE(ValuePattern::parse("abc", Radix::Default, SignalKind::Real, 1).ok());
}

TEST_CASE("threshold crossings with and without the index agree with brute force") {
    WaveStore st;
    Signal& s = st.mutableSignal(st.addSignal(kRootScope, "a", SignalKind::Real, 1));
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    // A slow sine with noise: long stretches on one side of the threshold.
    for (Time i = 0; i < 20000; ++i) {
        s.appendReal(i * 2, std::sin(static_cast<double>(i) / 700.0) + 0.05 * d(rng));
        if (i % 97 == 0)
            s.appendReal(i * 2, std::nan("")); // values without a number are skipped
        if (i % 89 == 0)
            s.appendReal(i * 2, 2.0); // ... and deltas: last one wins
    }
    const NumericIndex idx = NumericIndex::build(s);
    const auto ev = events(s);

    for (SearchCondition c :
         {SearchCondition::CrossRising, SearchCondition::CrossFalling, SearchCondition::CrossEither}) {
        ValueQuery q;
        q.condition = c;
        q.threshold = 0.3;
        // Brute force list of crossing times (state per event, NaN events skipped).
        std::vector<Time> cross;
        int prev = -1;
        for (const Event& e : ev) {
            double v = s.real(e.last);
            if (std::isnan(v)) {
                // last known value within the event
                std::size_t i = e.last;
                while (i > 0 && s.time(i - 1) == e.t && std::isnan(s.real(i)))
                    --i;
                v = s.real(i);
                if (std::isnan(v))
                    continue;
            }
            const int st2 = v >= 0.3 ? 1 : 0;
            if (prev >= 0 && st2 != prev &&
                (c == SearchCondition::CrossEither || (c == SearchCondition::CrossRising) == (st2 == 1)))
                cross.push_back(e.t);
            prev = st2;
        }
        REQUIRE(cross.size() > 4);

        std::uniform_int_distribution<Time> pick(0, 40010);
        for (int k = 0; k < 300; ++k) {
            const Time from = pick(rng);
            const auto after = std::upper_bound(cross.begin(), cross.end(), from);
            const std::optional<Time> wantF =
                after == cross.end() ? std::nullopt : std::optional<Time>(*after);
            const auto before = std::lower_bound(cross.begin(), cross.end(), from);
            const std::optional<Time> wantB =
                before == cross.begin() ? std::nullopt : std::optional<Time>(*(before - 1));
            REQUIRE(findValue({&s}, q, from, Fwd, &idx) == wantF);
            REQUIRE(findValue({&s}, q, from, Fwd, nullptr) == wantF);
            REQUIRE(findValue({&s}, q, from, Bwd, &idx) == wantB);
            REQUIRE(findValue({&s}, q, from, Bwd, nullptr) == wantB);
        }
        REQUIRE(findValue({&s}, q, std::nullopt, Fwd, &idx) == cross.front());
        REQUIRE(findValue({&s}, q, std::nullopt, Bwd, &idx) == cross.back());
    }
}

TEST_CASE("crossings on a logic vector, signed, and a partly covering index") {
    WaveStore st;
    Signal& s = st.mutableSignal(st.addSignal(kRootScope, "v", SignalKind::Logic, 8));
    s.appendLogic(0, "00000000");  // 0
    s.appendLogic(10, "11111100"); // -4 signed, 252 unsigned
    s.appendLogic(20, "XXXXXXXX"); // skipped
    s.appendLogic(30, "00000010"); // 2
    const NumericIndex idx = NumericIndex::build(s, true);
    s.appendLogic(40, "11111111"); // -1, not in the index yet
    ValueQuery q;
    q.condition = SearchCondition::CrossFalling;
    q.threshold = 1.0;
    q.isSigned = true;
    REQUIRE(findValue({&s}, q, std::nullopt, Fwd, &idx) == Time{40}); // 0 -> -4 stays below 1; 2 -> -1 falls
    q.condition = SearchCondition::CrossRising;
    REQUIRE(findValue({&s}, q, std::nullopt, Fwd, &idx) == Time{30});
    q.condition = SearchCondition::CrossFalling;
    REQUIRE(findValue({&s}, q, Time{30}, Fwd, &idx) == Time{40});
    REQUIRE(findValue({&s}, q, std::nullopt, Bwd, &idx) == Time{40});
    q.isSigned = false; // 252 >= 1: rising at 10, falling at 30 (2 >= 1? no: 2 >= 1 stays above)
    q.condition = SearchCondition::CrossRising;
    REQUIRE(findValue({&s}, q, std::nullopt, Fwd, &idx) == Time{10}); // signed index is ignored
    // Bit targets have no numeric view.
    REQUIRE(findValue({&s, 0}, q, std::nullopt, Fwd) == std::nullopt);
}

TEST_CASE("searching a bits sequence (virtual bus)") {
    struct Seq final : BitsSequence {
        std::vector<Time> t{0, 10, 20, 30};
        std::vector<std::string> v{"00", "01", "01", "11"};
        std::size_t count() const override { return t.size(); }
        Time time(std::size_t i) const override { return t[i]; }
        std::string bits(std::size_t i) const override { return v[i]; }
    } seq;
    REQUIRE(findEdge(seq, Time{10}, Fwd) == Time{30}); // 20 repeats 01
    REQUIRE(findEdge(seq, Time{30}, Bwd) == Time{10});
    ValueQuery q;
    q.pattern = pat("1", Radix::Hex, 2);
    REQUIRE(findValue(seq, q, std::nullopt, Fwd) == Time{10});
    REQUIRE(findValue(seq, q, Time{10}, Fwd) == std::nullopt);
    q.condition = SearchCondition::NotEquals;
    REQUIRE(findValue(seq, q, Time{10}, Fwd) == Time{30});
}

TEST_CASE("edge search agrees with brute force on random vectors") {
    WaveStore st;
    Signal& s = st.mutableSignal(st.addSignal(kRootScope, "r", SignalKind::Logic, 3));
    std::mt19937_64 rng(3);
    Time t = 0;
    for (int i = 0; i < 3000; ++i) {
        t += rng() % 3; // 0: another delta at the same time
        std::string v(3, '0');
        for (char& c : v)
            c = "01X"[rng() % 3];
        if (rng() % 4 == 0)
            v = "000";
        s.appendLogic(t, v);
    }
    const auto ev = events(s);
    std::vector<Time> edges;
    for (std::size_t k = 0; k < ev.size(); ++k)
        if (k == 0 || s.bits(ev[k].last) != s.bits(ev[k - 1].last))
            edges.push_back(ev[k].t);
    std::vector<Time> rises; // bit 2 becomes 1
    for (std::size_t k = 0; k < ev.size(); ++k) {
        const bool one = s.bit(ev[k].last, 2) == Logic::L1;
        const bool wasOne = k > 0 && s.bit(ev[k - 1].last, 2) == Logic::L1;
        if (one && !wasOne)
            rises.push_back(ev[k].t);
    }
    for (Time from = 0; from <= t + 1; from += 7) {
        auto a = std::upper_bound(edges.begin(), edges.end(), from);
        REQUIRE(findEdge({&s}, from, Fwd) == (a == edges.end() ? std::nullopt : std::optional<Time>(*a)));
        auto b = std::lower_bound(edges.begin(), edges.end(), from);
        REQUIRE(findEdge({&s}, from, Bwd) ==
                (b == edges.begin() ? std::nullopt : std::optional<Time>(*(b - 1))));
        auto r = std::upper_bound(rises.begin(), rises.end(), from);
        REQUIRE(findEdge({&s, 2}, from, Fwd, EdgeKind::Rising) ==
                (r == rises.end() ? std::nullopt : std::optional<Time>(*r)));
        auto rb = std::lower_bound(rises.begin(), rises.end(), from);
        REQUIRE(findEdge({&s, 2}, from, Bwd, EdgeKind::Rising) ==
                (rb == rises.begin() ? std::nullopt : std::optional<Time>(*(rb - 1))));
    }
}
