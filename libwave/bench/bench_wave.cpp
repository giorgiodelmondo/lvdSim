// SPDX-License-Identifier: GPL-3.0-or-later
// libwave benchmark: generates a synthetic FST trace, then measures load, index build and queries.
// Usage: bench_wave [signals] [changes_total] [file]
// Defaults are a scaled-down version of the reference trace (full size: 2000 signals, 100 M changes).
#include <lvdwave/fst_loader.hpp>
#include <lvdwave/lod.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <tuple>
#include <vector>

extern "C" {
#include <fstapi.h>
}

using namespace lvd::wave;
using Clock = std::chrono::steady_clock;

static double secondsSince(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

namespace {

struct Spec {
    fstHandle handle;
    int kind; // 0 clock, 1 bus32, 2 bus64, 3 real
    int width;
    int period;
};

void generate(const std::string& path, int nsig, std::uint64_t totalChanges) {
    auto* w = fstWriterCreate(path.c_str(), 1);
    fstWriterSetTimescale(w, -15);
    fstWriterSetScope(w, FST_ST_VHDL_ARCHITECTURE, "top", nullptr);

    std::vector<Spec> specs;
    for (int i = 0; i < nsig; ++i) {
        Spec s{};
        s.kind = (i % 100 < 50) ? 0 : (i % 100 < 80) ? 1 : (i % 100 < 95) ? 2 : 3;
        s.width = s.kind == 0 ? 1 : s.kind == 1 ? 32 : s.kind == 2 ? 64 : 8;
        s.period = 1 + (i * 7) % 97;
        const std::string name = "s" + std::to_string(i);
        s.handle = fstWriterCreateVar(w, s.kind == 3 ? FST_VT_VCD_REAL : FST_VT_VCD_WIRE, FST_VD_IMPLICIT,
                                      s.width, name.c_str(), 0);
        specs.push_back(s);
    }
    fstWriterSetUpscope(w);

    std::mt19937_64 rng(42);
    std::uint64_t emitted = 0;
    double rate = 0;  // expected changes per step
    for (const Spec& sp : specs) rate += 1.0 / sp.period;
    const std::uint64_t steps = static_cast<std::uint64_t>(static_cast<double>(totalChanges) / rate) + 1;
    std::vector<char> buf(65);

    fstWriterEmitTimeChange(w, 0);
    for (std::uint64_t step = 0; step < steps; ++step) {
        fstWriterEmitTimeChange(w, step * 10 + 10);
        for (const Spec& s : specs) {
            if (step % static_cast<std::uint64_t>(s.period) != 0)
                continue;
            if (s.kind == 0) {
                fstWriterEmitValueChange(w, s.handle, (step / s.period) & 1 ? "1" : "0");
            } else if (s.kind == 3) {
                const double v = static_cast<double>(rng() % 1000) / 10.0;
                fstWriterEmitValueChange(w, s.handle, &v);
            } else {
                const std::uint64_t v = rng();
                for (int b = 0; b < s.width; ++b)
                    buf[static_cast<std::size_t>(s.width - 1 - b)] = ((v >> (b % 64)) & 1) ? '1' : '0';
                buf[static_cast<std::size_t>(s.width)] = 0;
                fstWriterEmitValueChange(w, s.handle, buf.data());
            }
            ++emitted;
        }
    }
    fstWriterClose(w);
    std::printf("generated %llu value changes\n", static_cast<unsigned long long>(emitted));
}

} // namespace

int main(int argc, char** argv) {
    const int nsig = argc > 1 ? std::atoi(argv[1]) : 400;
    const std::uint64_t changes = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 4000000ull;
    const std::string path =
        argc > 3 ? argv[3] : (std::filesystem::temp_directory_path() / "lvdwave_bench.fst").string();

    auto t0 = Clock::now();
    generate(path, nsig, changes);
    std::printf("generate:        %8.2f s   file %.1f MB\n", secondsSince(t0),
                static_cast<double>(std::filesystem::file_size(path)) / 1e6);

    t0 = Clock::now();
    auto r = loadFst(path);
    const double loadS = secondsSince(t0);
    if (!r.ok()) {
        std::printf("load failed: %s\n", r.error().c_str());
        return 1;
    }
    const WaveStore& st = r.value();
    std::printf("load:            %8.2f s   %llu changes, %.1f MB in memory, %.1f M changes/s\n", loadS,
                static_cast<unsigned long long>(st.totalChanges()),
                static_cast<double>(st.memoryBytes()) / 1e6,
                static_cast<double>(st.totalChanges()) / loadS / 1e6);

    // Numeric index build over every numeric signal.
    t0 = Clock::now();
    std::vector<NumericIndex> indexes;
    std::vector<SignalId> numeric;
    for (SignalId id = 0; id < st.signalCount(); ++id) {
        if (st.entry(id).aliasOf() != kNoSignal)
            continue;
        const Signal& s = st.signal(id);
        if (s.kind() == SignalKind::Real ||
            (s.kind() == SignalKind::Logic && s.width() <= 64 && s.width() > 1)) {
            indexes.push_back(NumericIndex::build(s));
            numeric.push_back(id);
        }
    }
    std::printf("numeric index:   %8.2f s   %zu signals\n", secondsSince(t0), numeric.size());

    // Column query latency: one pixel column worth of changes plus the held value, at random positions.
    std::mt19937_64 rng(1);
    const Time end = st.endTime();
    const Time span = std::max<Time>(1, end / 2000); // a 2000 pixel wide window
    constexpr int kBatch = 100;
    const int batches = 2000;
    std::vector<double> lat;
    lat.reserve(batches);
    std::uint64_t sink = 0;
    std::vector<SignalId> ids(kBatch);
    std::vector<Time> starts(kBatch);
    for (int i = 0; i < batches; ++i) {
        for (int k = 0; k < kBatch; ++k) {
            ids[k] = static_cast<SignalId>(rng() % st.signalCount());
            starts[k] = rng() % std::max<Time>(1, end - span);
        }
        const auto q0 = Clock::now();
        for (int k = 0; k < kBatch; ++k) {
            const Signal& s = st.signal(ids[k]);
            const std::size_t hold = s.holdIndex(starts[k]);
            const auto [lo, hi] = s.changesIn(starts[k], starts[k] + span);
            sink += hold + lo + hi;
        }
        lat.push_back(std::chrono::duration<double, std::nano>(Clock::now() - q0).count() / kBatch);
    }
    std::sort(lat.begin(), lat.end());
    std::printf("column query:    median %6.0f ns   p99 %6.0f ns   (%d queries, sink %llu)\n",
                lat[lat.size() / 2], lat[lat.size() * 99 / 100], batches * kBatch,
                static_cast<unsigned long long>(sink));

    // Min/max range query latency on the analog index.
    if (!indexes.empty()) {
        std::vector<double> mm;
        mm.reserve(batches);
        double acc = 0;
        std::vector<std::size_t> los(kBatch), his(kBatch), ks(kBatch);
        for (int i = 0; i < batches; ++i) {
            for (int k = 0; k < kBatch; ++k) {
                ks[k] = rng() % indexes.size();
                const Signal& s = st.signal(numeric[ks[k]]);
                const Time a = rng() % std::max<Time>(1, end - span);
                std::tie(los[k], his[k]) = s.changesIn(a, a + span * 20);
            }
            const auto q0 = Clock::now();
            for (int k = 0; k < kBatch; ++k) {
                const MinMax m = indexes[ks[k]].range(los[k], his[k]);
                acc += m.empty() ? 0 : m.max;
            }
            mm.push_back(std::chrono::duration<double, std::nano>(Clock::now() - q0).count() / kBatch);
        }
        std::sort(mm.begin(), mm.end());
        std::printf("min/max range:   median %6.0f ns   p99 %6.0f ns   (acc %.1f)\n", mm[mm.size() / 2],
                    mm[mm.size() * 99 / 100], acc);
    }

    std::filesystem::remove(path);
    return 0;
}
