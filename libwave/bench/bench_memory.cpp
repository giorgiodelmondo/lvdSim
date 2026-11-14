// SPDX-License-Identifier: GPL-3.0-or-later
// libwave benchmark: the memory image (address -> word at any time) of a large memory with many writes.
// Usage: bench_memory [words] [writes]   (default 65536 words of 32 bits, 2000000 writes)
#include <lvdwave/memory.hpp>

#include <algorithm>
#include <chrono>
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
    std::printf("%-46s median %9.2f us   p99 %9.2f us   max %9.2f us  (sink %llu)\n", name, us[us.size() / 2],
                us[us.size() * 99 / 100], us.back(), static_cast<unsigned long long>(sink % 1000));
}

std::string bits32(std::uint32_t v) {
    std::string s(32, '0');
    for (int b = 0; b < 32; ++b)
        s[31 - static_cast<std::size_t>(b)] = ((v >> b) & 1u) ? '1' : '0';
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t words = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 65536ull;
    const std::size_t writes = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 2000000ull;

    // A memory as a producer that writes only the changed element would record it: every word at 0, then one
    // write per 10 time units at a random address (a quarter of them to a small hot region).
    WaveStore st;
    const ScopeId sc = st.addScope(kRootScope, "tb");
    std::vector<SignalId> ids;
    for (std::size_t w = 0; w < words; ++w)
        ids.push_back(st.addSignal(sc, "mem[" + std::to_string(w) + "][31:0]", SignalKind::Logic, 32));
    auto t0 = Clock::now();
    for (SignalId id : ids)
        st.mutableSignal(id).appendLogic(0, bits32(0));
    std::mt19937_64 rng(11);
    const std::size_t firstHalf = writes / 2;
    auto writeRange = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            const std::size_t addr = (i % 4 == 0) ? rng() % 64 : rng() % words;
            const Time t = static_cast<Time>(i + 1) * 10;
            st.mutableSignal(ids[addr]).appendLogic(t, bits32(static_cast<std::uint32_t>(rng())));
            st.extendTimeRange(t);
        }
    };
    writeRange(0, firstHalf);
    std::printf("store: %zu words x 32 bits, %zu writes so far, built in %.0f ms (%.0f MB)\n", words,
                firstHalf, msSince(t0), static_cast<double>(st.memoryBytes()) / 1e6);

    t0 = Clock::now();
    const auto mems = findMemories(st);
    std::printf("findMemories: %zu memory, %.1f ms\n", mems.size(), msSince(t0));

    t0 = Clock::now();
    MemoryImage img(st, mems.front());
    std::printf("image index of %zu writes: %.1f ms\n", img.writes().size(), msSince(t0));

    // Live: the rest arrives in 200 polls.
    std::vector<double> pollMs;
    const std::size_t step = std::max<std::size_t>(1, (writes - firstHalf) / 200);
    for (std::size_t from = firstHalf; from < writes; from += step) {
        writeRange(from, std::min(writes, from + step));
        const auto p0 = Clock::now();
        img.update();
        pollMs.push_back(msSince(p0));
    }
    std::sort(pollMs.begin(), pollMs.end());
    std::printf("update per poll (%zu polls, ~%zu writes each): median %.2f ms, max %.2f ms; %zu writes\n",
                pollMs.size(), step, pollMs[pollMs.size() / 2], pollMs.back(), img.writes().size());
    t0 = Clock::now();
    img.update();
    std::printf("update with nothing new: %.2f ms\n", msSince(t0));

    const Time end = st.endTime();
    std::vector<Time> at(500);
    for (Time& t : at)
        t = rng() % end;
    const int q = static_cast<int>(at.size());

    timeQueries("one screen: 64 lines x 16 words, hex", q, [&](int i) {
        const Time t = at[static_cast<std::size_t>(i)];
        const std::size_t first = (static_cast<std::size_t>(i) * 977) % (words - 1024);
        std::size_t n = 0;
        for (std::size_t w = first; w < first + 1024; ++w)
            n += img.format(w, t, Radix::Hex).size();
        return n;
    });
    timeQueries("one screen: last-write time of 1024 words", q, [&](int i) {
        const Time t = at[static_cast<std::size_t>(i)];
        std::size_t n = 0;
        for (std::size_t w = 0; w < 1024; ++w)
            n += img.lastWrite(w, t).value_or(0) == t ? 1 : 0;
        return n;
    });
    timeQueries("last write time and words written there", q, [&](int i) {
        const auto lw = img.lastWriteTime(at[static_cast<std::size_t>(i)]);
        return lw ? img.wordsWrittenAt(*lw).size() : 0;
    });
    timeQueries("next write time", q,
                [&](int i) { return img.nextWriteTime(at[static_cast<std::size_t>(i)]).value_or(0); });

    const auto pat = ValuePattern::parse("DEADBEEF", Radix::Hex, SignalKind::Logic, 32);
    timeQueries("find value, no match (all words)", 20, [&](int i) {
        return img
            .findWord(pat.value(), at[static_cast<std::size_t>(i)], std::nullopt, SearchDirection::Forward)
            .value_or(0);
    });
    t0 = Clock::now();
    const std::string text = img.memoryFileText(end, Radix::Hex);
    std::printf("$readmemh text of all words: %.1f ms (%zu bytes)\n", msSince(t0), text.size());

    // The kernel today repeats every element when one changes: 4096 words, 500 writes = 2M changes, 500
    // writes.
    {
        WaveStore rs;
        const ScopeId s2 = rs.addScope(kRootScope, "tb");
        const std::size_t n2 = 4096, w2 = 500;
        std::vector<SignalId> e;
        for (std::size_t w = 0; w < n2; ++w)
            e.push_back(rs.addSignal(s2, "ram[" + std::to_string(w) + "][31:0]", SignalKind::Logic, 32));
        std::vector<std::string> vals(n2, bits32(0));
        for (std::size_t i = 0; i <= w2; ++i) {
            if (i > 0)
                vals[rng() % n2] = bits32(static_cast<std::uint32_t>(rng()));
            for (std::size_t w = 0; w < n2; ++w)
                rs.mutableSignal(e[w]).appendLogic(static_cast<Time>(i) * 10, vals[w]);
        }
        t0 = Clock::now();
        MemoryImage ri(rs, findMemories(rs).front());
        std::printf(
            "repeated-element dump (%zu words x %zu dumps = %zu changes): index %.1f ms, %zu writes\n", n2,
            w2 + 1, n2 * (w2 + 1), msSince(t0), ri.writes().size());
    }
    return 0;
}
