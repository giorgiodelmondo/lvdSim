// SPDX-License-Identifier: GPL-3.0-or-later
// Analog traces must look the same at every zoom level and tile alignment, and nothing may be drawn
// after the end of the recorded data.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/tile_geometry.hpp"
#include "wave/wave_widget.hpp"

#include <QPainter>
#include <QtTest>

#include <cmath>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

constexpr int kHeight = 4 * 22;

TileRequest request(const RowSource& src, double tpp, long long tile, bool linear = false) {
    TileRequest r;
    r.source = &src;
    r.timePerPixel = tpp;
    r.tileIndex = tile;
    r.widthPx = IRenderer::kTileWidth;
    r.heightPx = kHeight;
    r.style.linear = linear;
    return r;
}

struct Span {
    int lo = -1, hi = -1; ///< first and last drawn pixel of a column, -1 if none
    bool empty() const { return lo < 0; }
};

Span column(const QImage& img, int x) {
    Span s;
    for (int y = 0; y < img.height(); ++y) {
        if (qAlpha(img.pixel(x, y)) == 0)
            continue;
        if (s.lo < 0)
            s.lo = y;
        s.hi = y;
    }
    return s;
}

/// First column of the picture, from x on, that has any pixel drawn; -1 if none.
int firstDrawnFrom(const QImage& img, int x) {
    for (; x < img.width(); ++x)
        if (!column(img, x).empty())
            return x;
    return -1;
}

/// A sine sampled at irregular intervals, as a simulator process with a variable step would produce.
struct SineStore {
    WaveStore store;
    SignalId id;
    std::unique_ptr<NumericIndex> index;
    std::unique_ptr<SignalSource> source;

    SineStore(Time step, Time period, Time length) {
        id = store.addSignal(kRootScope, "sine", SignalKind::Real, 64);
        Signal& s = store.mutableSignal(id);
        Time t = 0;
        for (unsigned n = 0; t <= length; ++n) {
            s.appendReal(
                t, std::sin(2.0 * 3.14159265358979 * static_cast<double>(t) / static_cast<double>(period)));
            t += step + (n % 3); // irregular
        }
        store.extendTimeRange(0);
        store.extendTimeRange(length);
        index = std::make_unique<NumericIndex>(NumericIndex::build(store.signal(id)));
        source = std::make_unique<SignalSource>(store.signal(id), index.get(), true);
    }
};

/// One signal of every kind of row, all with their last change well before the end of the data.
struct MixedStore {
    std::shared_ptr<WaveStore> store = std::make_shared<WaveStore>();
    std::unique_ptr<NumericIndex> index;
    std::vector<std::unique_ptr<RowSource>> sources;
    std::vector<QString> names;

    explicit MixedStore(Time end) {
        const SignalId bit = store->addSignal(kRootScope, "bit", SignalKind::Logic, 1);
        const SignalId bus = store->addSignal(kRootScope, "bus", SignalKind::Logic, 8);
        const SignalId real = store->addSignal(kRootScope, "real", SignalKind::Real, 64);
        const SignalId text = store->addSignal(kRootScope, "text", SignalKind::String, 0);
        for (Time t = 0; t <= 300; t += 25) {
            store->mutableSignal(bit).appendLogic(t, (t / 25) % 2 ? "1" : "0");
            store->mutableSignal(bus).appendLogic(t, (t / 25) % 2 ? "10101010" : "01010101");
            store->mutableSignal(real).appendReal(t, static_cast<double>((t / 25) % 5));
        }
        store->mutableSignal(text).appendText(0, "idle");
        store->mutableSignal(text).appendText(200, "run");
        store->extendTimeRange(0);
        store->extendTimeRange(end);
        index = std::make_unique<NumericIndex>(NumericIndex::build(store->signal(real)));
        sources.push_back(std::make_unique<SignalSource>(store->signal(bit), nullptr, false));
        sources.push_back(std::make_unique<SignalSource>(store->signal(bus), nullptr, false));
        sources.push_back(std::make_unique<SignalSource>(store->signal(real), index.get(), true));
        sources.push_back(std::make_unique<SignalSource>(store->signal(text), nullptr, false));
        names = {"bit", "bus", "analog", "string"};
    }
};

QImage gpuTile(GpuOffscreen& gpu, const TileRequest& r, const Palette& pal) {
    gpu.renderer().clearCache();
    GpuFrame f;
    f.size = QSize(r.widthPx, r.heightPx);
    f.background = Qt::transparent;
    f.tileClip = QRect(QPoint(0, 0), f.size);
    GpuTileDraw t;
    t.key.row = reinterpret_cast<const void*>(std::uintptr_t{0x2000});
    t.key.revision = 1;
    t.key.scaleBits = scaleBitsOf(r.timePerPixel);
    t.key.tile = r.tileIndex;
    t.key.heightPx = r.heightPx;
    t.heightPx = r.heightPx;
    f.tiles.push_back(t);
    f.geometryFor = [&](const GpuTileDraw&) { return buildTileGeometry(r, pal); };
    return gpu.render(f).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

class TestWaveFidelity : public QObject {
    Q_OBJECT

    // Every column of a coarse tile covers `ratio` columns of the finer tiles at the same time span. The
    // picture zoomed out is the envelope of the picture zoomed in: same vertical extent per column.
    void zoomLevelsAgree(Time step, bool linear) {
        SineStore sine(step, 60000, 200000);
        PainterRenderer r;
        for (int ratio : {2, 4, 16}) {
            const double fine = 1.0, coarse = fine * ratio;
            for (long long tile : {0LL, 1LL, 2LL}) {
                const QImage c = r.renderTile(request(*sine.source, coarse, tile, linear));
                QList<QImage> f;
                for (int i = 0; i < ratio; ++i)
                    f.push_back(r.renderTile(request(*sine.source, fine, tile * ratio + i, linear)));
                for (int x = 0; x < c.width(); ++x) {
                    Span u; // union of the fine columns
                    for (int k = 0; k < ratio; ++k) {
                        const int gx = x * ratio + k;
                        const Span s = column(f[gx / c.width()], gx % c.width());
                        if (s.empty())
                            continue;
                        u.lo = u.lo < 0 ? s.lo : std::min(u.lo, s.lo);
                        u.hi = std::max(u.hi, s.hi);
                    }
                    const Span s = column(c, x);
                    const QString where = QString("ratio %1 tile %2 column %3").arg(ratio).arg(tile).arg(x);
                    QVERIFY2(
                        s.lo == u.lo && s.hi == u.hi,
                        qPrintable(
                            where +
                            QString(": coarse %1..%2, fine %3..%4").arg(s.lo).arg(s.hi).arg(u.lo).arg(u.hi)));
                }
            }
        }
    }

private slots:
    void zoomLevelsAgreeStepDense() { zoomLevelsAgree(3, false); }
    void zoomLevelsAgreeStepSparse() { zoomLevelsAgree(40, false); }
    void zoomLevelsAgreeLinearDense() { zoomLevelsAgree(3, true); }
    void zoomLevelsAgreeLinearSparse() { zoomLevelsAgree(40, true); }
    void zoomLevelsAgreeLinearVerySparse() { zoomLevelsAgree(700, true); }

    // A linear trace passes through the interpolated value in every column, also where the nearest changes
    // are far outside the tile (the old code held the last value to the tile edge: a step at every tile
    // edge).
    void linearTraceFollowsTheInterpolation() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        st.mutableSignal(id).appendReal(0, 0.0);
        st.mutableSignal(id).appendReal(2000, 10.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        PainterRenderer r;
        const double top = 3, bottom = kHeight - 4;
        for (long long tile : {0LL, 1LL}) {
            const QImage img = r.renderTile(request(src, 4.0, tile, true)); // 1024 time units per tile
            for (int x : {10, 100, 200, 250}) {
                const double t = (static_cast<double>(tile) * 256 + x + 0.5) * 4.0;
                if (t > 2000)
                    continue;
                const double y = bottom - (t / 2000.0) * (bottom - top);
                const Span s = column(img, x);
                QVERIFY(!s.empty());
                QVERIFY2(y >= s.lo - 2.5 && y <= s.hi + 2.5,
                         qPrintable(QString("tile %1 column %2: expected y %3, drawn %4..%5")
                                        .arg(tile)
                                        .arg(x)
                                        .arg(y)
                                        .arg(s.lo)
                                        .arg(s.hi)));
            }
        }
    }

    // A step trace holds its value and rises vertically in the column of the change.
    void stepTraceHoldsAndRises() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        st.mutableSignal(id).appendReal(0, 0.0);
        st.mutableSignal(id).appendReal(100, 10.0);
        st.mutableSignal(id).appendReal(180, 5.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        PainterRenderer r;
        const int top = 3, bottom = kHeight - 4;
        const QImage img = r.renderTile(request(src, 1.0, 0));
        QCOMPARE(column(img, 50).lo, bottom); // held low: one pixel
        QCOMPARE(column(img, 50).hi, bottom);
        QCOMPARE(column(img, 100).lo, top); // the riser spans both levels
        QCOMPARE(column(img, 100).hi, bottom);
        QCOMPARE(column(img, 140).lo, top);
        QCOMPARE(column(img, 140).hi, top);
        QCOMPARE(column(img, 180).lo, top); // falls to the middle
        QVERIFY(column(img, 180).hi > top + 20);
        QCOMPARE(column(img, 220).lo, column(img, 220).hi);
        QCOMPARE(column(img, 255).lo, column(img, 220).lo); // held to the right edge (no end given)
    }

    // ---- nothing after the end of the data
    // ---------------------------------------------------------------------

    void endClipEveryRowKind_data() {
        QTest::addColumn<double>("tpp");
        QTest::addColumn<long long>("tile");
        QTest::addColumn<unsigned long long>("end");
        QTest::newRow("zoomed in, end inside tile 0") << 1.0 << 0LL << 200ULL;
        QTest::newRow("end in the middle of a pixel") << 8.0 << 0LL << 1000ULL;
        QTest::newRow("end in tile 1") << 2.0 << 1LL << 700ULL;
        QTest::newRow("end on a pixel boundary") << 4.0 << 0LL << 400ULL;
    }
    void endClipEveryRowKind() {
        QFETCH(double, tpp);
        QFETCH(long long, tile);
        QFETCH(unsigned long long, end);
        MixedStore m(end);
        PainterRenderer r;
        const int endColumn =
            static_cast<int>(std::floor(static_cast<double>(end) / tpp)) - static_cast<int>(tile * 256);
        QVERIFY(endColumn > 0 && endColumn < 250);
        for (std::size_t i = 0; i < m.sources.size(); ++i) {
            TileRequest q = request(*m.sources[i], tpp, tile);
            q.validEnd = end;
            const QImage img = r.renderTile(q);
            // The last value is held up to the end of the data: its column is drawn, the next ones are not.
            QVERIFY2(!column(img, endColumn).empty(), qPrintable(m.names[i] + ": the end column is empty"));
            QCOMPARE(firstDrawnFrom(img, endColumn + 1), -1);
            // Without an end the value is held to the right edge, so the clip is what removes it.
            q.validEnd = std::numeric_limits<Time>::max();
            QVERIFY2(firstDrawnFrom(r.renderTile(q), endColumn + 2) >= 0,
                     qPrintable(m.names[i] + ": no held value without an end"));
        }
    }

    void tilesBeyondTheEndAreBlank() {
        MixedStore m(500);
        PainterRenderer r;
        for (const auto& src : m.sources) {
            TileRequest q = request(*src, 1.0, 3); // times 768 ..
            q.validEnd = 500;
            QCOMPARE(firstDrawnFrom(r.renderTile(q), 0), -1);
        }
    }

    void endClipOnTheGpu() {
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu)
            QSKIP(qPrintable("no headless GPU: " + err));
        MixedStore m(500);
        Palette pal;
        PainterRenderer r;
        for (const auto& src : m.sources) {
            TileRequest q = request(*src, 2.0, 0); // the end is at column 250
            q.validEnd = 500;
            const QImage g = gpuTile(*gpu, q, pal);
            QCOMPARE(firstDrawnFrom(g, 251), -1);
            QVERIFY(!column(g, 250).empty());
            QCOMPARE(firstDrawnFrom(r.renderTile(q), 251), -1);
        }
    }

    // ---- the widget
    // --------------------------------------------------------------------------------------------

    void widgetDrawsNothingAfterTheEnd() {
        auto w = std::make_unique<WaveWidget>();
        w->resize(1500, 320);
        DisplayModel model;
        model.root().children.push_back(DisplayModel::makeSignal("bit"));
        model.root().children.push_back(DisplayModel::makeSignal("bus", Radix::Hex));
        DisplayItem real = DisplayModel::makeSignal("real");
        real.analog.enabled = true;
        model.root().children.push_back(real);
        model.root().children.push_back(DisplayModel::makeSignal("text"));
        w->setDisplayModel(model);
        MixedStore m(400);
        w->setStore(m.store);
        w->setView(0.0, 1.0); // 1 time unit per pixel: the data ends 400 pixels in, the wave area is wider
        QVERIFY(w->waveWidth() > 500);

        const int left = w->waveLeft(), right = left + w->waveWidth();
        const int endX = left + 400;
        auto checkImage = [&](const QImage& img, const QString& what) {
            bool anyBefore = false;
            for (int y = w->rowsTop(); y < w->rowsTop() + 7 * 22; ++y) {
                for (int x = left + 2; x < right - 2; ++x) {
                    const QColor c = img.pixelColor(x, y);
                    const int bright = std::max({c.red(), c.green(), c.blue()});
                    if (x > endX + 3) {
                        QVERIFY2(bright < 0x60, qPrintable(QString("%1: drawn pixel at %2,%3: %4")
                                                               .arg(what)
                                                               .arg(x)
                                                               .arg(y)
                                                               .arg(c.name())));
                    } else if (bright >= 0xc0) {
                        anyBefore = true;
                    }
                }
            }
            QVERIFY2(anyBefore, qPrintable(what + ": nothing drawn before the end"));
            // The tint marks the area without data and is faint.
            const QColor bg = img.pixelColor(left + 3, w->rowsTop() + 2),
                         tinted = img.pixelColor(right - 5, w->rowsTop() + 2);
            QVERIFY2(tinted.red() > bg.red() && tinted.red() < bg.red() + 24, qPrintable(what + ": no tint"));
        };

        w->setCursorTime(0, 100.0);
        QCOMPARE(QString::fromStdString(w->rowValueText(0)), QString("0"));
        w->setCursorTime(0, 500.0); // after the end
        for (int row = 0; row < 4; ++row)
            QCOMPARE(QString::fromStdString(w->rowValueText(row)), QString("-"));
        w->setCursorTime(0, 400.0); // on the end: still valid
        QVERIFY(QString::fromStdString(w->rowValueText(1)) != QString("-"));
        w->setCursorTime(0, std::nullopt);

        checkImage(w->grab().toImage(), "QPainter");

        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu) {
            qInfo() << "no headless GPU:" << err;
            return;
        }
        const QSize size(w->waveWidth(), w->height() - w->rowsTop());
        gpu->renderer().clearCache();
        const QImage g =
            gpu->render(w->buildGpuFrame(size)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage placed(w->width(), w->height(),
                      QImage::Format_ARGB32_Premultiplied); // at the wave area's position
        placed.fill(Qt::black);
        QPainter p(&placed);
        p.drawImage(left, w->rowsTop(), g);
        p.end();
        checkImage(placed, "GPU");
    }
};

QTEST_MAIN(TestWaveFidelity)
#include "test_wave_fidelity.moc"
