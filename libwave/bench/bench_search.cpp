// SPDX-License-Identifier: GPL-3.0-or-later
// libwave benchmark: searching change lists (edges, values, threshold crossings) and list window rows.
// Usage: bench_search [changes per signal]   (default 1000000)
#include <lvdwave/list_index.hpp>
#include <lvdwave/lod.hpp>
#include <lvdwave/search.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace lvd::wave;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

template <class F>
void timeQueries(const char* name, int n, F&& f) {
    std::vector<double> us;
    us.reserve(static_cast<std::size_t>(n));
    std::uint64_t sink = 0;
    for (int i = 0; i < n; ++i) {
        const auto t0 = Clock::now();
        sink += f(i);
        us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
    }
    std::sort(us.begin(), us.end());
    std::printf("%-44s median %9.2f us   p99 %9.2f us   max %9.2f us  (sink %llu)\n", name, us[us.size() / 2],
                us[us.size() * 99 / 100], us.back(), static_cast<unsigned long long>(sink % 1000));
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000ull;
    WaveStore st;
    const SignalId clkId = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
    const SignalId cntId = st.addSignal(kRootScope, "cnt", SignalKind::Logic, 32);
    const SignalId sinId = st.addSignal(kRootScope, "sine", SignalKind::Real, 1);
    Signal& clk = st.mutableSignal(clkId);
    Signal& cnt = st.mutableSignal(cntId);
    Signal& sine = st.mutableSignal(sinId);

    auto t0 = Clock::now();
    std::string bits(32, '0');
    for (std::size_t i = 0; i < n; ++i) {
        const Time t = static_cast<Time>(i) * 10;
        clk.appendLogic(t, (i & 1) ? "1" : "0");
        const std::uint32_t v = static_cast<std::uint32_t>(i);
        for (int b = 0; b < 32; ++b)
            bits[31 - static_cast<std::size_t>(b)] = ((v >> b) & 1u) ? '1' : '0';
        cnt.appendLogic(t, bits);
        // ~20 crossings of 0.999 over the whole run: long stretches without a crossing
        sine.appendReal(t, std::sin(static_cast<double>(i) * 6.283185307 * 10.0 / static_cast<double>(n)));
        st.extendTimeRange(t);
    }
    std::printf("store: 3 signals x %zu changes built in %.0f ms\n", n, msSince(t0));

    t0 = Clock::now();
    const NumericIndex sinIdx = NumericIndex::build(sine);
    std::printf("numeric index of the real signal: %.1f ms\n", msSince(t0));

    std::mt19937_64 rng(5);
    const Time end = st.endTime();
    std::vector<Time> from(2000);
    for (Time& f : from)
        f = rng() % end;
    const int q = static_cast<int>(from.size());

    timeQueries("next edge, clk (any)", q, [&](int i) {
        return findEdge({&clk}, from[static_cast<std::size_t>(i)], SearchDirection::Forward).value_or(0);
    });
    timeQueries("previous rising edge, clk", q, [&](int i) {
        return findEdge({&clk}, from[static_cast<std::size_t>(i)], SearchDirection::Backward,
                        EdgeKind::Rising)
            .value_or(0);
    });
    timeQueries("next edge, cnt bit 19 (changes every 2^19)", 50, [&](int i) {
        return findEdge({&cnt, 19}, from[static_cast<std::size_t>(i)], SearchDirection::Forward).value_or(0);
    });

    ValueQuery eq;
    eq.pattern = ValuePattern::parse(std::to_string(n - 3), Radix::Unsigned, SignalKind::Logic, 32).value();
    timeQueries("cnt == value near the end, from 0 (full scan)", 20,
                [&](int) { return findValue({&cnt}, eq, Time{0}, SearchDirection::Forward).value_or(0); });
    ValueQuery hexq;
    hexq.pattern = ValuePattern::parse("???FF", Radix::Hex, SignalKind::Logic, 32).value();
    timeQueries("cnt == hex ???FF (every 256th), random start", q, [&](int i) {
        return findValue({&cnt}, hexq, from[static_cast<std::size_t>(i)], SearchDirection::Forward)
            .value_or(0);
    });
    ValueQuery none;
    none.pattern = ValuePattern::parse("FFFFFFFF", Radix::Hex, SignalKind::Logic, 32).value();
    timeQueries("cnt == FFFFFFFF (never), backward from end", 20, [&](int) {
        return findValue({&cnt}, none, std::nullopt, SearchDirection::Backward).value_or(0);
    });

    ValueQuery cross;
    cross.condition = SearchCondition::CrossRising;
    cross.threshold = 0.999;
    timeQueries("sine crosses 0.999 rising, with index", q, [&](int i) {
        return findValue({&sine}, cross, from[static_cast<std::size_t>(i)], SearchDirection::Forward, &sinIdx)
            .value_or(0);
    });
    timeQueries("sine crosses 0.999 rising, backward, index", q, [&](int i) {
        return findValue({&sine}, cross, from[static_cast<std::size_t>(i)], SearchDirection::Backward,
                         &sinIdx)
            .value_or(0);
    });
    timeQueries("sine crosses 0.999 rising, no index", 50, [&](int i) {
        return findValue({&sine}, cross, from[static_cast<std::size_t>(i)], SearchDirection::Forward, nullptr)
            .value_or(0);
    });

    // List window rows over the three signals (they change together: n rows).
    t0 = Clock::now();
    const ListIndex list(st, {clkId, cntId, sinId});
    std::printf("list index: %zu rows indexed in %.1f ms\n", list.rowCount(), msSince(t0));
    timeQueries("list row time, random row", q, [&](int) { return list.rowTime(rng() % list.rowCount()); });
    std::size_t r0 = rng() % (list.rowCount() - 100);
    timeQueries("list row time, next row (scrolling)", q,
                [&](int i) { return list.rowTime(r0 + static_cast<std::size_t>(i) % 100); });
    timeQueries("list row at time (cursor sync)", q,
                [&](int i) { return list.rowAtOrBefore(from[static_cast<std::size_t>(i)]); });
    return 0;
}
