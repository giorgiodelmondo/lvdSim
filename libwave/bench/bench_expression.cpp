// SPDX-License-Identifier: GPL-3.0-or-later
// libwave benchmark: expression signals and waveform comparison.
// Usage: bench_expression [changes per input signal]   (default 1000000)
//
// Every input signal gets N changes at its own times (inputs do not change together), so an expression with k
// inputs is evaluated at about k x N time steps. Reported: building the expression row in one call, building
// it in 1000 chunks as a live run delivers its data (incremental update), comparing two signals, and
// comparing two stores.
#include <lvdwave/compare.hpp>
#include <lvdwave/expression.hpp>
#include <lvdwave/wave_space.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>

using namespace lvd::wave;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

std::string bitsOf(std::uint64_t v, std::uint32_t w) {
    std::string s(w, '0');
    for (std::uint32_t b = 0; b < w; ++b)
        if ((v >> b) & 1u)
            s[w - 1 - b] = '1';
    return s;
}

/// tb.a, tb.b (1 bit), tb.bus[15:0], tb.cnt[31:0], each with n changes; offsets keep their times apart.
std::shared_ptr<WaveStore> makeStore(std::size_t n, std::uint32_t seed) {
    auto st = std::make_shared<WaveStore>();
    const ScopeId tb = st->addScope(kRootScope, "tb");
    Signal& a = st->mutableSignal(st->addSignal(tb, "a", SignalKind::Logic, 1));
    Signal& b = st->mutableSignal(st->addSignal(tb, "b", SignalKind::Logic, 1));
    Signal& bus = st->mutableSignal(st->addSignal(tb, "bus[15:0]", SignalKind::Logic, 16));
    Signal& cnt = st->mutableSignal(st->addSignal(tb, "cnt[31:0]", SignalKind::Logic, 32));
    std::mt19937 rng(seed);
    for (std::size_t i = 0; i < n; ++i) {
        const Time t = static_cast<Time>(i) * 40;
        a.appendLogic(t, (i & 1) ? "1" : "0");
        b.appendLogic(t + 10, (rng() & 3) == 0 ? "1" : "0");
        bus.appendLogic(t + 20, bitsOf(rng() & 0x7F, 16));
        cnt.appendLogic(t + 30, bitsOf(i, 32));
    }
    st->extendTimeRange(0);
    st->extendTimeRange(static_cast<Time>(n) * 40);
    return st;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000ull;
    auto t0 = Clock::now();
    auto st = makeStore(n, 1);
    std::printf("store: %zu changes per input, built in %.0f ms\n\n", n, msSince(t0));
    WaveSpace space;
    space.setCurrent(st);

    std::printf("%-44s %10s %10s %12s %10s\n", "expression", "one call", "1000 chunks", "out changes",
                "ns/step");
    const char* exprs[] = {
        "a and not b",
        "bus = 16#3F",
        "cnt > 100000",
        "bus(7 downto 4) & a",
        "(bus(3 downto 0) = 5) or (a and b)",
        "cnt(7 downto 0) + bus(7 downto 0)",
    };
    for (const char* text : exprs) {
        t0 = Clock::now();
        auto one = ExpressionSignal::create(text, space.resolver());
        const double full = msSince(t0);
        if (!one.ok()) {
            std::printf("%-44s error: %s\n", text, one.error().c_str());
            return 1;
        }
        const std::size_t inputs = one.value()->expression().inputs().size();

        // the same over a store that grows in 1000 chunks
        auto grow = std::make_shared<WaveStore>();
        const ScopeId tb = grow->addScope(kRootScope, "tb");
        Signal* dst[4];
        const char* names[4] = {"a", "b", "bus[15:0]", "cnt[31:0]"};
        const std::uint32_t widths[4] = {1, 1, 16, 32};
        for (int k = 0; k < 4; ++k)
            dst[k] = &grow->mutableSignal(grow->addSignal(tb, names[k], SignalKind::Logic, widths[k]));
        WaveSpace gs;
        gs.setCurrent(grow);
        auto inc = ExpressionSignal::create(text, gs.resolver());
        double incMs = 0.0;
        const std::size_t chunk = std::max<std::size_t>(1, n / 1000);
        for (std::size_t from = 0; from < n; from += chunk) {
            const std::size_t to = std::min(n, from + chunk);
            for (int k = 0; k < 4; ++k) {
                const Signal& src = st->signal(st->findSignal(std::string("tb.") + names[k]));
                for (std::size_t i = from; i < to; ++i)
                    dst[k]->appendLogic(src.time(i), src.bits(i));
            }
            t0 = Clock::now();
            inc.value()->update();
            incMs += msSince(t0);
        }
        if (inc.value()->signal().changeCount() != one.value()->signal().changeCount()) {
            std::printf("%-44s incremental result differs!\n", text);
            return 1;
        }
        std::printf("%-44s %8.1f ms %8.1f ms %12zu %10.1f\n", text, full, incMs,
                    one.value()->signal().changeCount(), full * 1e6 / static_cast<double>(n * inputs));
    }

    std::printf("\n%-44s %10s %12s\n", "compare", "time", "differences");
    auto other = makeStore(n, 2); // same a and cnt, different b and bus
    for (const char* path : {"tb.a", "tb.bus[15:0]", "tb.cnt[31:0]"}) {
        const Signal& x = st->signal(st->findSignal(path));
        const Signal& y = other->signal(other->findSignal(path));
        t0 = Clock::now();
        SignalCompare c(x, y);
        c.update(st->endTime());
        std::printf("%-44s %8.1f ms %12zu\n", (std::string("signal ") + path).c_str(), msSince(t0),
                    c.count());
        CompareOptions o;
        o.tolerance = 15;
        t0 = Clock::now();
        SignalCompare ct(x, y, o);
        ct.update(st->endTime());
        std::printf("%-44s %8.1f ms %12zu\n", (std::string("signal ") + path + ", tolerance 15").c_str(),
                    msSince(t0), ct.count());
    }
    t0 = Clock::now();
    const StoreDiff d = compareStores(*st, *other);
    std::printf("%-44s %8.1f ms %12zu  (%zu signals compared, %zu differ)\n", "whole store", msSince(t0),
                d.total, d.compared, d.differing.size());
    return 0;
}
