// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/painter_renderer.hpp"
#include "wave/tile_cache.hpp"

#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

constexpr int kH = 22;
constexpr int kHi = 3;        // top level row of pixels
constexpr int kLo = kH - 4;   // bottom level row

TileRequest request(const RowSource& src, double tpp, long long tile = 0, int h = kH) {
    TileRequest r;
    r.source = &src;
    r.timePerPixel = tpp;
    r.tileIndex = tile;
    r.widthPx = IRenderer::kTileWidth;
    r.heightPx = h;
    return r;
}

bool drawn(const QImage& img, int x, int y) { return qAlpha(img.pixel(x, y)) > 0; }

}  // namespace

class TestPainterRenderer : public QObject {
    Q_OBJECT
private slots:
    void scalarLevelsAndEdges() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
        Signal& s = st.mutableSignal(id);
        s.appendLogic(0, "0");
        s.appendLogic(100, "1");
        s.appendLogic(200, "0");
        SignalSource src(st.signal(id), nullptr, false);
        QCOMPARE(src.waveClass(), WaveClass::Scalar);

        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0));
        QCOMPARE(img.width(), 256);
        QCOMPARE(img.height(), kH);

        QVERIFY(drawn(img, 50, kLo));     // low level before the edge
        QVERIFY(!drawn(img, 50, kHi));
        QVERIFY(drawn(img, 150, kHi));    // high level
        QVERIFY(!drawn(img, 150, kLo));
        QVERIFY(drawn(img, 100, 10));     // rising edge is a vertical line
        QVERIFY(drawn(img, 200, 10));     // falling edge
        QVERIFY(drawn(img, 240, kLo));    // low again
        QVERIFY(!drawn(img, 50, 10));     // nothing between the levels away from edges
    }

    void nothingBeforeFirstChange() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
        st.mutableSignal(id).appendLogic(100, "1");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0));
        QVERIFY(!drawn(img, 50, kHi));
        QVERIFY(!drawn(img, 50, kLo));
        QVERIFY(drawn(img, 150, kHi));
    }

    void valueHeldPastTheLastChange() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
        st.mutableSignal(id).appendLogic(0, "1");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0, /*tile*/ 40));  // far beyond the data
        QVERIFY(drawn(img, 10, kHi));
        QVERIFY(drawn(img, 250, kHi));
    }

    void unknownAndHighZ() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
        Signal& s = st.mutableSignal(id);
        s.appendLogic(0, "X");
        s.appendLogic(100, "Z");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0));
        QVERIFY(drawn(img, 50, 10));  // unknown is a filled band
        QVERIFY(qRed(img.pixel(50, kHi)) > qBlue(img.pixel(50, kHi)));
        const int mid = kH / 2;
        QVERIFY(drawn(img, 150, mid));  // high impedance is a mid level line
        QVERIFY(qBlue(img.pixel(150, mid)) > qRed(img.pixel(150, mid)));
    }

    void denseScalarBecomesSolid() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
        Signal& s = st.mutableSignal(id);
        for (Time t = 0; t < 5000; ++t) s.appendLogic(t, (t & 1) ? "1" : "0");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 16.0));  // 16 changes per pixel
        for (int x : {5, 100, 200}) {
            QVERIFY(drawn(img, x, kHi));
            QVERIFY(drawn(img, x, 10));
            QVERIFY(drawn(img, x, kLo));
        }
    }

    void busSegments() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "b", SignalKind::Logic, 8);
        Signal& s = st.mutableSignal(id);
        s.appendLogic(0, "00000000");
        s.appendLogic(100, "1111XXXX");
        SignalSource src(st.signal(id), nullptr, false);
        QCOMPARE(src.waveClass(), WaveClass::Bus);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0));

        QVERIFY(drawn(img, 50, kHi));     // outline top
        QVERIFY(drawn(img, 50, kLo));     // outline bottom
        QVERIFY(!drawn(img, 50, 10));     // known value: hollow
        QVERIFY(drawn(img, 150, 10));     // value with X is filled
        QVERIFY(qRed(img.pixel(150, 10)) > qBlue(img.pixel(150, 10)));
    }

    void denseBusUsesPerColumnMode() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "b", SignalKind::Logic, 8);
        Signal& s = st.mutableSignal(id);
        for (Time t = 0; t < 20000; ++t) s.appendLogic(t, (t & 1) ? "10101010" : "01010101");
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 64.0));
        QVERIFY(drawn(img, 100, kHi));
        QVERIFY(drawn(img, 100, 10));
    }

    void analogStepAndEnvelope() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        Signal& s = st.mutableSignal(id);
        for (Time t = 0; t < 300; t += 10) s.appendReal(t, (t / 10) % 2 == 0 ? 0.0 : 10.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        QCOMPARE(src.waveClass(), WaveClass::Analog);

        const int h = 4 * kH;
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0, 0, h));
        QVERIFY(drawn(img, 5, h - 4));    // low value near the bottom
        QVERIFY(drawn(img, 15, 3));       // high value near the top
        QVERIFY(drawn(img, 10, 40));      // step edge between them

        // Zoomed far out the envelope must still reach both extremes.
        WaveStore big;
        const SignalId bid = big.addSignal(kRootScope, "v", SignalKind::Real, 64);
        for (Time t = 0; t < 100000; ++t) big.mutableSignal(bid).appendReal(t, (t % 2) ? 10.0 : 0.0);
        const NumericIndex bidx = NumericIndex::build(big.signal(bid));
        SignalSource bsrc(big.signal(bid), &bidx, true);
        const QImage env = r.renderTile(request(bsrc, 400.0, 0, h));
        QVERIFY(drawn(env, 100, 3));
        QVERIFY(drawn(env, 100, h - 4));
    }

    void flatAnalogDoesNotDivideByZero() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        st.mutableSignal(id).appendReal(0, 5.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0, 0, 4 * kH));
        bool any = false;
        for (int y = 0; y < img.height(); ++y) any = any || drawn(img, 20, y);
        QVERIFY(any);
    }

    void emptySourceRendersBlank() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
        SignalSource src(st.signal(id), nullptr, false);
        PainterRenderer r;
        const QImage img = r.renderTile(request(src, 1.0));
        for (int y = 0; y < img.height(); ++y) QVERIFY(!drawn(img, 10, y));
    }

    void virtualBusSource() {
        WaveStore st;
        const ScopeId top = st.addScope(kRootScope, "top");
        const SignalId a = st.addSignal(top, "a", SignalKind::Logic, 4);
        const SignalId b = st.addSignal(top, "b", SignalKind::Logic, 4);
        st.mutableSignal(a).appendLogic(0, "0000");
        st.mutableSignal(a).appendLogic(100, "1111");
        st.mutableSignal(b).appendLogic(50, "1010");
        BusSource bus(st, {"top.a", "top.b"});
        QCOMPARE(bus.count(), std::size_t{3});  // union of times 0, 50, 100
        QCOMPARE(bus.time(1), Time{50});
        QCOMPARE(QString::fromStdString(bus.text(1, Radix::Hex)), QString("0A"));
        QCOMPARE(QString::fromStdString(bus.text(2, Radix::Hex)), QString("FA"));
        QVERIFY(bus.isUnknown(0));   // b has no value at time 0
        QVERIFY(!bus.isUnknown(1));
        QCOMPARE(bus.holdIndex(75), std::size_t{1});
        QCOMPARE(bus.holdIndex(0), std::size_t{0});

        PainterRenderer r;
        const QImage img = r.renderTile(request(bus, 1.0));
        QVERIFY(drawn(img, 70, kHi));
    }

    void tileCacheLru() {
        TileCache cache(2);
        auto key = [](long long tile) {
            TileKey k;
            k.row = &k;
            k.revision = 1;
            k.scaleBits = scaleBitsOf(1.0);
            k.tile = tile;
            k.heightPx = 22;
            return k;
        };
        QImage img(4, 4, QImage::Format_ARGB32);
        TileKey k1 = key(1), k2 = key(2), k3 = key(3);
        k1.row = k2.row = k3.row = nullptr;
        cache.put(k1, img);
        cache.put(k2, img);
        QVERIFY(cache.find(k1) != nullptr);  // k1 becomes most recent
        cache.put(k3, img);                  // evicts k2
        QCOMPARE(cache.size(), std::size_t{2});
        QVERIFY(cache.find(k2) == nullptr);
        QVERIFY(cache.find(k1) != nullptr);
        QVERIFY(cache.find(k3) != nullptr);
        QCOMPARE(cache.hits(), std::size_t{3});
        QCOMPARE(cache.misses(), std::size_t{1});

        TileKey other = k1;
        other.revision = 2;  // a changed row never matches old tiles
        QVERIFY(cache.find(other) == nullptr);
        cache.clear();
        QCOMPARE(cache.size(), std::size_t{0});
    }
};

QTEST_MAIN(TestPainterRenderer)
#include "test_painter_renderer.moc"
