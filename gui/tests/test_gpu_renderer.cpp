// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer against the QPainter renderer, headless. Skipped when no GPU context can be created.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/tile_geometry.hpp"

#include <lvdwave/lod.hpp>

#include <QPainter>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

struct Compare {
    int drawn = 0;       // pixels drawn by either picture
    int different = 0;   // pixels whose colour differs noticeably
    int onlyOne = 0;     // pixels drawn by one picture and empty in the other
};

Compare compare(const QImage& a, const QImage& b, int tolerance = 24) {
    Compare c;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
            const bool da = p.alpha() > 8, db = q.alpha() > 8;
            if (!da && !db) continue;
            ++c.drawn;
            if (da != db) ++c.onlyOne;
            const int d = std::max({std::abs(p.red() - q.red()), std::abs(p.green() - q.green()), std::abs(p.blue() - q.blue()),
                                    std::abs(p.alpha() - q.alpha())});
            if (d > tolerance) ++c.different;
        }
    return c;
}

TileKey keyFor(int id, int tile = 0, int height = 22, std::uint64_t revision = 1) {
    TileKey k;
    k.row = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x1000 + id));
    k.revision = revision;
    k.scaleBits = scaleBitsOf(1.0);
    k.tile = tile;
    k.heightPx = height;
    return k;
}

TileRequest request(const RowSource& src, double tpp, int height = 22, long long tile = 0) {
    TileRequest r;
    r.source = &src;
    r.timePerPixel = tpp;
    r.tileIndex = tile;
    r.widthPx = GpuWaveRenderer::kTileWidth;
    r.heightPx = height;
    return r;
}

// Draws one tile on the GPU, alone on a transparent target.
QImage gpuTile(GpuOffscreen& gpu, const TileRequest& r, const Palette& pal) {
    gpu.renderer().clearCache();  // the key is the same in every test: never reuse an earlier picture
    GpuFrame f;
    f.size = QSize(r.widthPx, r.heightPx);
    f.background = Qt::transparent;
    f.tileClip = QRect(QPoint(0, 0), f.size);
    GpuTileDraw t;
    t.key = keyFor(1, static_cast<int>(r.tileIndex), r.heightPx);
    t.heightPx = r.heightPx;
    f.tiles.push_back(t);
    f.geometryFor = [&](const GpuTileDraw&) { return buildTileGeometry(r, pal); };
    return gpu.render(f);
}

}  // namespace

class TestGpuRenderer : public QObject {
    Q_OBJECT

    std::unique_ptr<GpuOffscreen> gpu_;
    Palette palette_;

private slots:
    void initTestCase() {
        QString err;
        gpu_ = GpuOffscreen::create(&err);
        if (!gpu_) QSKIP(qPrintable("no GPU context: " + err));
        qInfo() << "GPU backend:" << gpu_->backendName();
    }

    // Tile keys are made up per test and repeat between tests: every test starts with an empty GPU cache.
    void init() {
        if (gpu_) gpu_->renderer().clearCache();
    }

    void anEmptyFrameIsTheBackground() {
        GpuFrame f;
        f.size = QSize(64, 32);
        f.background = QColor(10, 20, 30);
        f.tileClip = QRect(0, 0, 64, 32);
        f.geometryFor = [](const GpuTileDraw&) { return TileGeometry{}; };
        const QImage img = gpu_->render(f);
        QCOMPARE(img.size(), QSize(64, 32));
        QCOMPARE(img.pixelColor(0, 0), QColor(10, 20, 30));
        QCOMPARE(img.pixelColor(63, 31), QColor(10, 20, 30));
    }

    void rectanglesAreExactAndBlendWithPremultipliedAlpha() {
        GpuFrame f;
        f.size = QSize(32, 32);
        f.background = QColor(0, 0, 0);
        f.tileClip = QRect(0, 0, 32, 32);
        f.rects.push_back({4, 6, 10, 3, Rgba{255, 0, 0, 255}});
        f.rects.push_back({20, 10, 4, 4, Rgba{0, 255, 0, 128}});  // half transparent over black
        f.geometryFor = [](const GpuTileDraw&) { return TileGeometry{}; };
        const QImage img = gpu_->render(f);

        QCOMPARE(img.pixelColor(4, 6), QColor(255, 0, 0));
        QCOMPARE(img.pixelColor(13, 8), QColor(255, 0, 0));
        QCOMPARE(img.pixelColor(14, 8), QColor(0, 0, 0));  // one past the right edge
        QCOMPARE(img.pixelColor(3, 6), QColor(0, 0, 0));
        QCOMPARE(img.pixelColor(4, 9), QColor(0, 0, 0));   // one below
        const QColor half = img.pixelColor(21, 11);
        QVERIFY(std::abs(half.green() - 128) <= 2);
        QCOMPARE(half.red(), 0);
    }

    // ---- the same picture as QPainter, tile by tile --------------------------------------------------
    void scalarTilesMatch() {
        WaveStore st;
        const SignalId clk = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
        Signal& s = st.mutableSignal(clk);
        s.appendLogic(0, "0");
        s.appendLogic(40, "1");
        s.appendLogic(90, "0");
        s.appendLogic(120, "X");
        s.appendLogic(160, "Z");
        s.appendLogic(200, "1");
        s.appendLogic(205, "0");
        SignalSource src(st.signal(clk), nullptr, false);

        PainterRenderer cpu(palette_);
        const TileRequest r = request(src, 1.0);
        const Compare c = compare(cpu.renderTile(r), gpuTile(*gpu_, r, palette_));
        QVERIFY(c.drawn > 300);
        QCOMPARE(c.different, 0);  // axis aligned shapes are exact
        QCOMPARE(c.onlyOne, 0);
    }

    void denseScalarMatches() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
        for (Time t = 0; t < 6000; ++t) st.mutableSignal(id).appendLogic(t, (t & 1) ? "1" : "0");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer cpu(palette_);
        const TileRequest r = request(src, 16.0);
        const Compare c = compare(cpu.renderTile(r), gpuTile(*gpu_, r, palette_));
        QVERIFY(c.drawn > 1000);
        QCOMPARE(c.different, 0);
    }

    void busTilesMatchClosely() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "b", SignalKind::Logic, 8);
        Signal& s = st.mutableSignal(id);
        s.appendLogic(0, "00000000");
        s.appendLogic(60, "1111XXXX");
        s.appendLogic(110, "10101010");
        s.appendLogic(115, "01010101");  // a segment too narrow for an outline
        s.appendLogic(180, "ZZZZZZZZ");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer cpu(palette_);
        const TileRequest r = request(src, 1.0);
        const Compare c = compare(cpu.renderTile(r), gpuTile(*gpu_, r, palette_));
        QVERIFY(c.drawn > 300);
        // the slanted ends of the hexagons are rasterised slightly differently
        QVERIFY2(c.onlyOne + c.different < c.drawn / 12, qPrintable(QString("%1 of %2 differ").arg(c.onlyOne + c.different).arg(c.drawn)));
    }

    void denseBusMatches() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "b", SignalKind::Logic, 8);
        for (Time t = 0; t < 20000; ++t) st.mutableSignal(id).appendLogic(t, (t & 1) ? "10101010" : "01010101");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer cpu(palette_);
        const TileRequest r = request(src, 64.0);
        const Compare c = compare(cpu.renderTile(r), gpuTile(*gpu_, r, palette_));
        QVERIFY(c.drawn > 500);
        QCOMPARE(c.different, 0);
    }

    void analogTilesMatch() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        for (Time t = 0; t < 400; t += 10) st.mutableSignal(id).appendReal(t, std::sin(static_cast<double>(t) / 40.0) * 3.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        PainterRenderer cpu(palette_);

        for (bool linear : {false, true}) {
            TileRequest r = request(src, 1.0, 88);
            r.style.linear = linear;
            const Compare c = compare(cpu.renderTile(r), gpuTile(*gpu_, r, palette_));
            QVERIFY(c.drawn > 200);
            QVERIFY2(c.different == 0 && c.onlyOne == 0, qPrintable(QString("linear=%1: %2 of %3 differ").arg(linear).arg(c.onlyOne + c.different).arg(c.drawn)));
        }
    }

    void analogEnvelopeMatches() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        for (Time t = 0; t < 100000; ++t) st.mutableSignal(id).appendReal(t, (t % 2) ? 10.0 : 0.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        PainterRenderer cpu(palette_);
        const TileRequest r = request(src, 400.0, 88);
        const Compare c = compare(cpu.renderTile(r), gpuTile(*gpu_, r, palette_));
        QVERIFY(c.drawn > 400);
        QCOMPARE(c.different, 0);
    }

    void aColourOverrideReachesTheGpu() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
        st.mutableSignal(id).appendLogic(0, "1");
        SignalSource src(st.signal(id), nullptr, false);
        TileRequest r = request(src, 1.0);
        r.style.hasColor = true;
        r.style.color = QColor(0x20, 0x80, 0xff);
        const QImage img = gpuTile(*gpu_, r, palette_);
        QCOMPARE(img.pixelColor(10, 3), QColor(0x20, 0x80, 0xff));  // the high level line
    }

    // ---- a whole frame ---------------------------------------------------------------------------------
    void aComposedFrameMatchesTheCpuComposition() {
        WaveStore st;
        const SignalId a = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
        const SignalId b = st.addSignal(kRootScope, "bus", SignalKind::Logic, 8);
        const SignalId v = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        for (Time t = 0; t < 2000; t += 20) st.mutableSignal(a).appendLogic(t, (t / 20) % 2 ? "1" : "0");
        for (Time t = 0; t < 2000; t += 130) st.mutableSignal(b).appendLogic(t, (t / 130) % 3 == 0 ? "1010X010" : "00001111");
        for (Time t = 0; t < 2000; t += 7) st.mutableSignal(v).appendReal(t, std::cos(static_cast<double>(t) / 90.0));
        const NumericIndex idx = NumericIndex::build(st.signal(v));
        SignalSource sa(st.signal(a), nullptr, false), sb(st.signal(b), nullptr, false), sv(st.signal(v), &idx, true);

        struct Row {
            const RowSource* src;
            int y, h;
        };
        const std::vector<Row> rows = {{&sa, 10, 22}, {&sb, 32, 22}, {&sv, 54, 88}, {&sa, 142, 22}};
        const double tpp = 2.0;
        const int width = 900, height = 200;
        const QRect clip(120, 0, width - 120, height);

        GpuFrame f;
        f.size = QSize(width, height);
        f.background = palette_.background;
        f.tileClip = clip;
        f.rects.push_back({0, 32, static_cast<float>(width), 22, Rgba::from(palette_.selection)});  // a selected row
        f.rects.push_back({300, 0, 1, static_cast<float>(height), Rgba::from(palette_.grid)});      // a grid line

        std::vector<std::pair<GpuTileDraw, TileRequest>> draws;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            for (long long tile = 0; tile < 4; ++tile) {
                TileRequest r = request(*rows[i].src, tpp, rows[i].h, tile);
                GpuTileDraw d;
                d.key = keyFor(static_cast<int>(i), static_cast<int>(tile), rows[i].h);
                d.heightPx = rows[i].h;
                d.x = static_cast<float>(60 + tile * 256);  // starts left of the clip, so it is cut there
                d.y = static_cast<float>(rows[i].y);
                f.tiles.push_back(d);
                draws.emplace_back(d, r);
            }
        }
        f.geometryFor = [&](const GpuTileDraw& d) {
            for (const auto& [dd, r] : draws)
                if (dd.key == d.key) return buildTileGeometry(r, palette_);
            return TileGeometry{};
        };
        const QImage gpuImg = gpu_->render(f);

        QImage cpuImg(width, height, QImage::Format_ARGB32_Premultiplied);
        cpuImg.fill(palette_.background);
        {
            QPainter p(&cpuImg);
            p.fillRect(QRectF(0, 32, width, 22), palette_.selection);
            p.fillRect(QRectF(300, 0, 1, height), palette_.grid);
            p.setClipRect(clip);
            PainterRenderer cpu(palette_);
            for (const auto& [d, r] : draws) p.drawImage(QPointF(d.x, d.y), cpu.renderTile(r));
        }

        QCOMPARE(gpuImg.size(), cpuImg.size());
        const Compare c = compare(cpuImg, gpuImg);
        QVERIFY(c.drawn > 100000);
        // Where do they differ? Summarise by row so that a failure points at the shape that is wrong.
        QMap<int, int> perRow;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const QColor p = cpuImg.pixelColor(x, y), q = gpuImg.pixelColor(x, y);
                const int d = std::max({std::abs(p.red() - q.red()), std::abs(p.green() - q.green()), std::abs(p.blue() - q.blue())});
                if (d > 24) ++perRow[y];
            }
        QString where;
        for (auto it = perRow.begin(); it != perRow.end(); ++it) where += QString(" y%1:%2").arg(it.key()).arg(it.value());
        QVERIFY2(c.different + c.onlyOne < c.drawn / 200,
                 qPrintable(QString("%1 of %2 pixels differ, by row:%3").arg(c.different + c.onlyOne).arg(c.drawn).arg(where)));

        // nothing leaks outside the clip rectangle
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < clip.left(); ++x)
                QVERIFY2(gpuImg.pixelColor(x, y) == palette_.background || (y >= 32 && y < 54) || x == 300,
                         qPrintable(QString("pixel %1,%2 is not background").arg(x).arg(y)));
    }

    // ---- the cache on the GPU ----------------------------------------------------------------------------
    void tilesStayOnTheGpuBetweenFrames() {
        gpu_->renderer().clearCache();
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
        for (Time t = 0; t < 1000; t += 10) st.mutableSignal(id).appendLogic(t, (t / 10) % 2 ? "1" : "0");
        SignalSource src(st.signal(id), nullptr, false);

        int built = 0;
        GpuFrame f;
        f.size = QSize(600, 100);
        f.background = Qt::black;
        f.tileClip = QRect(0, 0, 600, 100);
        for (int tile = 0; tile < 2; ++tile) {
            GpuTileDraw d;
            d.key = keyFor(5, tile);
            d.heightPx = 22;
            d.x = static_cast<float>(tile * 256);
            d.y = 10;
            f.tiles.push_back(d);
        }
        f.geometryFor = [&](const GpuTileDraw& d) {
            ++built;
            return buildTileGeometry(request(src, 4.0, 22, d.key.tile), palette_);
        };

        const QImage first = gpu_->render(f);
        QCOMPARE(built, 2);
        QCOMPARE(gpu_->renderer().lastFrame().tilesRasterised, std::size_t{2});
        QCOMPARE(gpu_->renderer().cachedTiles(), std::size_t{2});

        const QImage second = gpu_->render(f);  // nothing to build: only quads are drawn
        QCOMPARE(built, 2);
        QCOMPARE(gpu_->renderer().lastFrame().tilesRasterised, std::size_t{0});
        QCOMPARE(gpu_->renderer().lastFrame().tilesReused, std::size_t{2});
        QCOMPARE(gpu_->renderer().lastFrame().quads, std::size_t{2});
        QCOMPARE(first, second);

        // a new revision is a different picture
        for (GpuTileDraw& d : f.tiles) d.key.revision = 2;
        gpu_->render(f);
        QCOMPARE(built, 4);

        gpu_->renderer().clearCache();
        QCOMPARE(gpu_->renderer().cachedTiles(), std::size_t{0});
        gpu_->render(f);
        QCOMPARE(built, 6);
    }

    void reusedSlotsAreCleanedBeforeNewTiles() {
        gpu_->renderer().clearCache();
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
        st.mutableSignal(id).appendLogic(0, "1");
        SignalSource high(st.signal(id), nullptr, false);
        const SignalId id2 = st.addSignal(kRootScope, "b", SignalKind::Logic, 1);
        st.mutableSignal(id2).appendLogic(0, "0");
        SignalSource low(st.signal(id2), nullptr, false);

        auto frameFor = [&](const RowSource& s, int rev) {
            GpuFrame f;
            f.size = QSize(256, 22);
            f.background = Qt::transparent;
            f.tileClip = QRect(0, 0, 256, 22);
            GpuTileDraw d;
            d.key = keyFor(9, 0, 22, static_cast<std::uint64_t>(rev));
            d.heightPx = 22;
            f.tiles.push_back(d);
            f.geometryFor = [&s](const GpuTileDraw&) { return buildTileGeometry(request(s, 1.0), Palette{}); };
            return f;
        };
        const QImage a = gpu_->render(frameFor(high, 1));  // high level: top line
        QVERIFY(a.pixelColor(10, 3).alpha() > 0);
        gpu_->renderer().clearCache();                     // the slot is free again but still holds the old picture
        const QImage b = gpu_->render(frameFor(low, 2));   // low level: bottom line only
        QVERIFY(b.pixelColor(10, 18).alpha() > 0);
        QCOMPARE(b.pixelColor(10, 3).alpha(), 0);          // no remains of the previous tile
    }

    void memoryRunsOutGracefully() {
        gpu_->renderer().clearCache();
        // the atlases of one height grow (4 lines of tiles, then 4 times more each, up to the full size)
        int capacity = 0;
        for (std::size_t k = 0; k < GpuWaveRenderer::kMaxAtlasesPerHeight; ++k)
            capacity += (GpuWaveRenderer::kAtlasWidth / GpuWaveRenderer::kTileWidth) * GpuWaveRenderer::atlasRows(22, k);
        const int total = capacity + 300;  // more than all atlases of this height hold

        GpuFrame f;
        f.size = QSize(256, 22);
        f.background = Qt::black;
        f.tileClip = QRect(0, 0, 256, 22);
        for (int i = 0; i < total; ++i) {
            GpuTileDraw d;
            d.key = keyFor(i);
            d.heightPx = 22;
            f.tiles.push_back(d);
        }
        f.geometryFor = [](const GpuTileDraw&) { return TileGeometry{}; };
        gpu_->render(f);
        QCOMPARE(gpu_->renderer().lastFrame().tilesSkipped, std::size_t{300});  // everything is needed at once: nothing to evict
        QCOMPARE(gpu_->renderer().cachedTiles(), static_cast<std::size_t>(capacity));

        // the next frame needs different tiles: the oldest ones are evicted
        f.tiles.clear();
        for (int i = 0; i < 100; ++i) {
            GpuTileDraw d;
            d.key = keyFor(total + i);
            d.heightPx = 22;
            f.tiles.push_back(d);
        }
        gpu_->render(f);
        QCOMPARE(gpu_->renderer().lastFrame().tilesSkipped, std::size_t{0});
        QCOMPARE(gpu_->renderer().lastFrame().tilesRasterised, std::size_t{100});
        QCOMPARE(gpu_->renderer().cachedTiles(), static_cast<std::size_t>(capacity));
        gpu_->renderer().clearCache();
    }
};

QTEST_MAIN(TestGpuRenderer)
#include "test_gpu_renderer.moc"
