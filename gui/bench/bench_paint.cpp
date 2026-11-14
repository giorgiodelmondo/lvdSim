// SPDX-License-Identifier: GPL-3.0-or-later
// Paint benchmark for the wave widget on a large synthetic in-memory trace.
// Usage: bench_paint [signals] [changes_total] [width] [height] [varied]
// "varied" gives the rows many different heights (14 .. 103 px, analog rows too) to compare with the uniform default.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/wave_widget.hpp"

#include <QApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

double msSince(const QElapsedTimer& t) { return static_cast<double>(t.nsecsElapsed()) / 1e6; }

}  // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const int nsig = argc > 1 ? std::atoi(argv[1]) : 300;
    const std::uint64_t total = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10000000ull;
    const int width = argc > 3 ? std::atoi(argv[3]) : 1920;
    const int height = argc > 4 ? std::atoi(argv[4]) : 1080;
    const bool varied = argc > 5 && std::string(argv[5]) == "varied";

    QElapsedTimer t;
    t.start();
    auto store = std::make_shared<WaveStore>();
    const ScopeId top = store->addScope(kRootScope, "top");
    std::vector<SignalId> ids;
    std::vector<int> kind, period;
    for (int i = 0; i < nsig; ++i) {
        const int k = i % 10 < 5 ? 0 : (i % 10 < 8 ? 1 : 2);  // clock, 32-bit bus, real
        ids.push_back(store->addSignal(top, "s" + std::to_string(i), k == 2 ? SignalKind::Real : SignalKind::Logic,
                                       k == 0 ? 1 : 32));
        kind.push_back(k);
        period.push_back(1 + (i * 7) % 61);
    }
    double rate = 0;
    for (int p : period) rate += 1.0 / p;
    const std::uint64_t steps = static_cast<std::uint64_t>(static_cast<double>(total) / rate) + 1;
    std::mt19937_64 rng(5);
    char buf[33];
    for (std::uint64_t step = 0; step < steps; ++step) {
        for (int i = 0; i < nsig; ++i) {
            if (step % static_cast<std::uint64_t>(period[static_cast<std::size_t>(i)]) != 0) continue;
            Signal& s = store->mutableSignal(ids[static_cast<std::size_t>(i)]);
            const Time time = step * 10;
            if (kind[static_cast<std::size_t>(i)] == 0) {
                s.appendLogic(time, (step / static_cast<std::uint64_t>(period[static_cast<std::size_t>(i)])) & 1 ? "1" : "0");
            } else if (kind[static_cast<std::size_t>(i)] == 1) {
                const std::uint64_t v = rng();
                for (int b = 0; b < 32; ++b) buf[31 - b] = ((v >> b) & 1) ? '1' : '0';
                buf[32] = 0;
                s.appendLogic(time, buf);
            } else {
                s.appendReal(time, static_cast<double>(rng() % 1000) / 10.0);
            }
        }
    }
    store->extendTimeRange(0);
    store->extendTimeRange(steps * 10);
    std::printf("built store: %.0f ms, %llu changes, %.0f MB\n", msSince(t),
                static_cast<unsigned long long>(store->totalChanges()), static_cast<double>(store->memoryBytes()) / 1e6);

    DisplayModel model;
    for (int i = 0; i < nsig; ++i) {
        DisplayItem it = DisplayModel::makeSignal("top.s" + std::to_string(i), Radix::Hex);
        if (kind[static_cast<std::size_t>(i)] == 2) it.analog.enabled = true;
        if (varied) it.heightPx = 14 + (i * 13) % 90;
        model.root().children.push_back(std::move(it));
    }

    WaveWidget w;
    w.resize(width, height);
    w.setDisplayModel(std::move(model));
    t.restart();
    w.setStore(store);
    std::printf("set store + rows: %.0f ms (%d rows%s)\n", msSince(t), w.rowCount(), varied ? ", varied heights" : "");

    auto frame = [&] {
        QElapsedTimer ft;
        ft.start();
        const QImage img = w.grab().toImage();
        return img.isNull() ? -1.0 : msSince(ft);
    };

    const double full = frame();
    const double warm = frame();
    std::printf("full zoom, cold: %7.1f ms   warm: %5.1f ms   tiles cached: %zu\n", full, warm, w.tileCache().size());

    // Zoom in on the middle by powers of 4: every step is a cold frame at a new scale.
    double worst = 0, sum = 0;
    const int zoomSteps = 8;
    for (int i = 0; i < zoomSteps; ++i) {
        w.zoomBy(4.0);
        const double ms = frame();
        worst = std::max(worst, ms);
        sum += ms;
    }
    std::printf("zoom steps (cold at each scale): avg %6.1f ms   worst %6.1f ms\n", sum / zoomSteps, worst);

    // Pan at the current scale: new tiles only at the edges.
    w.zoomFull();
    w.zoomBy(8.0);
    frame();
    double pan = 0;
    const int panSteps = 30;
    for (int i = 0; i < panSteps; ++i) {
        w.setView(w.view().startTime + 40.0 * w.view().timePerPixel, w.view().timePerPixel);
        pan += frame();
    }
    std::printf("pan 40 px per frame:  avg %6.1f ms\n", pan / panSteps);

    // ---- virtual bus as an analog trace: the extra cost of composing values into a min/max index -----------
    {
        WaveSpace space;
        space.setCurrent(store);
        std::vector<BusMember> members; // low bytes of two busy 32-bit buses and two clocks: 18 bits
        for (int i = 0; i < nsig && members.size() < 18; ++i) {
            const std::string path = "top.s" + std::to_string(i);
            if (kind[static_cast<std::size_t>(i)] == 1)
                for (int b = 7; b >= 0 && members.size() < 16; --b)
                    members.emplace_back(path, b);
        }
        members.emplace_back("top.s0");
        members.emplace_back("top.s1");
        QElapsedTimer bt;
        bt.start();
        const BusSource plain(space, members);
        const double plainMs = msSince(bt);
        bt.restart();
        const BusSource analog(space, members, true);
        const double analogMs = msSince(bt);
        PainterRenderer pr;
        TileRequest q;
        q.source = &analog;
        q.widthPx = IRenderer::kTileWidth;
        q.heightPx = 88;
        q.timePerPixel = static_cast<double>(store->endTime()) / width;
        bt.restart();
        const int tiles = (width + IRenderer::kTileWidth - 1) / IRenderer::kTileWidth;
        for (int i = 0; i < tiles; ++i) {
            q.tileIndex = i;
            pr.renderTile(q);
        }
        std::printf("virtual bus (%zu members, %zu changes): build %.1f ms, as analog %.1f ms; full-zoom analog row "
                    "%.1f ms\n",
                    members.size(), analog.count(), plainMs, analogMs, msSince(bt));
    }
    // ---- GPU renderer (headless device), same store, same view sequence -----------------------
    QString err;
    auto gpu = GpuOffscreen::create(&err);
    if (!gpu) {
        std::printf("GPU: unavailable (%s)\n", qPrintable(err));
        return 0;
    }
    std::printf("GPU backend: %s\n", qPrintable(gpu->backendName()));
    const QSize waveSize(w.width() - w.waveLeft(), w.rowsBottom() - 40);
    auto gframe = [&] {
        QElapsedTimer ft;
        ft.start();
        const GpuFrame f = w.buildGpuFrame(waveSize);
        const double build = msSince(ft);
        gpu->renderAndWait(f);
        const bool ok = true;
        return std::pair<double, double>{ok ? msSince(ft) : -1.0, build};
    };
    w.zoomFull();
    gpu->renderer().clearCache();
    const auto gfull = gframe();
    const auto gwarm = gframe();
    std::printf("GPU full zoom, cold: %7.1f ms (frame build %.1f)   warm: %5.1f ms   tiles on GPU: %zu\n", gfull.first,
                gfull.second, gwarm.first, gpu->renderer().cachedTiles());
    double gworst = 0, gsum = 0;
    for (int i = 0; i < zoomSteps; ++i) {
        w.zoomBy(4.0);
        const double ms = gframe().first;
        gworst = std::max(gworst, ms);
        gsum += ms;
    }
    std::printf("GPU zoom steps (cold at each scale): avg %6.1f ms   worst %6.1f ms\n", gsum / zoomSteps, gworst);
    w.zoomFull();
    w.zoomBy(8.0);
    gframe();
    double gpan = 0;
    for (int i = 0; i < panSteps; ++i) {
        w.setView(w.view().startTime + 40.0 * w.view().timePerPixel, w.view().timePerPixel);
        gpan += gframe().first;
    }
    std::printf("GPU pan 40 px per frame:  avg %6.1f ms   (CPU %6.1f ms, speed-up %.1fx)\n", gpan / panSteps,
                pan / panSteps, pan / gpan);
    std::printf("speed-up: cold full %.1fx, zoom steps %.1fx\n", full / gfull.first, sum / gsum);
    std::printf("GPU atlases: %zu for %zu row heights, %.0f MB\n", gpu->renderer().atlasCount(),
                gpu->renderer().atlasHeights(), static_cast<double>(gpu->renderer().atlasBytes()) / 1048576.0);
    return 0;
}
