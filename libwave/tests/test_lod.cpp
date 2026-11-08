// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <lvdwave/lod.hpp>
#include <random>

using namespace lvd::wave;

namespace {

MinMax brute(const NumericIndex& idx, std::size_t first, std::size_t last) {
    MinMax m;
    last = std::min(last, idx.size());
    for (std::size_t i = first; i < last; ++i) m.add(idx.value(i));
    return m;
}

bool same(const MinMax& a, const MinMax& b) {
    if (a.empty() || b.empty()) return a.empty() && b.empty();
    return a.min == b.min && a.max == b.max;
}

}  // namespace

TEST_CASE("numeric value of logic signals") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Logic, 4);
    Signal& s = st.mutableSignal(id);
    s.appendLogic(0, "0101");
    s.appendLogic(1, "1111");
    s.appendLogic(2, "01X1");
    REQUIRE(numericValue(s, 0, false) == 5.0);
    REQUIRE(numericValue(s, 1, false) == 15.0);
    REQUIRE(numericValue(s, 1, true) == -1.0);
    REQUIRE(std::isnan(numericValue(s, 2, false)));
}

TEST_CASE("wide logic and strings have no numeric view") {
    WaveStore st;
    const SignalId w = st.addSignal(kRootScope, "w", SignalKind::Logic, 65);
    const SignalId t = st.addSignal(kRootScope, "t", SignalKind::String, 0);
    REQUIRE_FALSE(NumericIndex::build(st.signal(w)).valid());
    REQUIRE_FALSE(NumericIndex::build(st.signal(t)).valid());
}

TEST_CASE("range queries match brute force on random data") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "r", SignalKind::Real, 64);
    Signal& s = st.mutableSignal(id);
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<double> dist(-100.0, 100.0);
    const std::size_t n = 5000;
    for (std::size_t i = 0; i < n; ++i) s.appendReal(i, dist(rng));

    NumericIndex idx = NumericIndex::build(s);
    REQUIRE(idx.valid());
    REQUIRE(idx.size() == n);

    std::uniform_int_distribution<std::size_t> pick(0, n + 20);
    for (int k = 0; k < 2000; ++k) {
        std::size_t a = pick(rng), b = pick(rng);
        if (a > b) std::swap(a, b);
        REQUIRE(same(idx.range(a, b), brute(idx, a, b)));
    }
    REQUIRE(idx.range(5, 5).empty());
    REQUIRE(idx.range(10, 3).empty());
}

TEST_CASE("unknown values are skipped in ranges") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Logic, 8);
    Signal& s = st.mutableSignal(id);
    for (int i = 0; i < 100; ++i) s.appendLogic(i, i % 3 == 0 ? "XXXXXXXX" : std::string(8, '0'));
    s.appendLogic(200, "00001010");
    NumericIndex idx = NumericIndex::build(s);
    const MinMax m = idx.range(0, idx.size());
    REQUIRE(m.min == 0.0);
    REQUIRE(m.max == 10.0);
    REQUIRE(idx.range(0, 1).empty());  // only an X at change 0
}

TEST_CASE("incremental update equals full rebuild") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "r", SignalKind::Real, 64);
    Signal& s = st.mutableSignal(id);
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    NumericIndex inc = NumericIndex::build(s);  // empty
    std::size_t t = 0;
    for (int round = 0; round < 40; ++round) {
        const int add = 1 + static_cast<int>(rng() % 300);
        for (int i = 0; i < add; ++i) s.appendReal(t++, dist(rng));
        inc.update(s);
        NumericIndex full = NumericIndex::build(s);
        REQUIRE(inc.size() == full.size());
        REQUIRE(same(inc.range(0, inc.size()), full.range(0, full.size())));
        const std::size_t mid = inc.size() / 3;
        REQUIRE(same(inc.range(mid, inc.size()), full.range(mid, full.size())));
    }
}
