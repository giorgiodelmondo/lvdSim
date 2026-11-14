// SPDX-License-Identifier: GPL-3.0-or-later
// A virtual bus shown as an analog trace draws exactly like a vector signal with the same bits: at every zoom
// level, with automatic and manual range, unsigned and signed, while a run grows and after a restart, with
// the QPainter renderer and on the GPU.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/tile_geometry.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/wave_commands.hpp>

#include <QPainter>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

constexpr int kHeight = 4 * 22;

/// `top.cnt` (4 bits, U first, an X in the middle) and its bits as the scalars `top.b3` .. `top.b0`.
void appendStep(WaveStore& st, Time t, const std::string& bits) {
    auto id = [&](const char* name) { return st.findSignal(std::string("top.") + name); };
    st.mutableSignal(id("cnt")).appendLogic(t, bits);
    const char* names[] = {"b3", "b2", "b1", "b0"};
    for (int i = 0; i < 4; ++i)
        st.mutableSignal(id(names[i])).appendLogic(t, std::string(1, bits[static_cast<std::size_t>(i)]));
    st.extendTimeRange(t);
}

std::string bitsOf(unsigned v) {
    std::string s;
    for (int i = 3; i >= 0; --i)
        s += (v >> i) & 1u ? '1' : '0';
    return s;
}

std::shared_ptr<WaveStore> makeStore(Time until) {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    st->addSignal(top, "cnt", SignalKind::Logic, 4);
    for (const char* n : {"b3", "b2", "b1", "b0"})
        st->addSignal(top, n, SignalKind::Logic, 1);
    st->extendTimeRange(0);
    appendStep(*st, 0, "UUUU");
    unsigned v = 0;
    for (Time t = 10; t <= until; t += 10 + (t / 10) % 7) { // irregular steps
        appendStep(*st, t, t == 200 ? "01X0" : bitsOf(v));
        v = (v + 3) & 15u;
    }
    return st;
}

TileRequest request(const RowSource& src, double tpp, long long tile, bool linear) {
    TileRequest r;
    r.source = &src;
    r.timePerPixel = tpp;
    r.tileIndex = tile;
    r.widthPx = IRenderer::kTileWidth;
    r.heightPx = kHeight;
    r.style.linear = linear;
    return r;
}

QImage gpuTile(GpuOffscreen& gpu, const TileRequest& r, const Palette& pal) {
    gpu.renderer().clearCache();
    GpuFrame f;
    f.size = QSize(r.widthPx, r.heightPx);
    f.background = Qt::transparent;
    f.tileClip = QRect(QPoint(0, 0), f.size);
    GpuTileDraw t;
    t.key.row = reinterpret_cast<const void*>(std::uintptr_t{0x3000});
    t.key.revision = 1;
    t.key.scaleBits = scaleBitsOf(r.timePerPixel);
    t.key.tile = r.tileIndex;
    t.key.heightPx = r.heightPx;
    t.heightPx = r.heightPx;
    f.tiles.push_back(t);
    f.geometryFor = [&](const GpuTileDraw&) { return buildTileGeometry(r, pal); };
    return gpu.render(f).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

const std::vector<std::vector<BusMember>>& memberSets() {
    static const std::vector<std::vector<BusMember>> sets = {
        {"top.b3", "top.b2", "top.b1", "top.b0"}, // four scalars
        {BusMember("top.cnt", 3), BusMember("top.cnt", 2), "top.b1",
         "top.b0"},  // bits of the vector + scalars
        {"top.cnt"}, // the whole vector
    };
    return sets;
}

/// Wave widget with the vector and one virtual bus per member set, all analog.
struct Fixture {
    std::unique_ptr<WaveWidget> w = std::make_unique<WaveWidget>();
    std::shared_ptr<WaveStore> store;

    explicit Fixture(Radix radix = Radix::Unsigned, Time until = 600) {
        w->resize(1100, 640);
        DisplayModel m;
        DisplayItem vec = DisplayModel::makeSignal("top.cnt", radix);
        vec.analog.enabled = true;
        m.root().children.push_back(vec);
        int n = 0;
        for (const auto& members : memberSets()) {
            DisplayItem bus = DisplayModel::makeBus("vb" + std::to_string(n++), members, radix);
            bus.analog.enabled = true;
            m.root().children.push_back(bus);
        }
        w->setDisplayModel(m);
        store = makeStore(until);
        w->setStore(store);
    }
    WaveWidget* operator->() { return w.get(); }

    QImage rowImage(const QImage& all, int row) const {
        const int top = w->rowTop(row) - w->rowsTop();
        return all.copy(0, top, all.width(), kHeight);
    }
    QImage waveArea() const {
        const QImage c = w->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return c.copy(w->waveLeft(), w->rowsTop(), w->waveWidth(), w->rowsBottom() - w->rowsTop());
    }
    /// Every bus row is pixel-identical to the vector row (row 0); `what` names the situation.
    void compareRows(const QImage& all, const QString& what) {
        const QImage vec = rowImage(all, 0);
        bool drawn = false;
        for (int y = 0; y < vec.height() && !drawn; ++y)
            for (int x = 0; x < vec.width() && !drawn; ++x)
                drawn = vec.pixel(x, y) != vec.pixel(0, 0);
        QVERIFY2(drawn, qPrintable(what + ": the vector row is blank"));
        for (int row = 1; row < w->rowCount(); ++row) {
            QCOMPARE(w->rowClass(row), WaveClass::Analog);
            QVERIFY2(rowImage(all, row) == vec,
                     qPrintable(QString("%1: row %2 differs from the vector").arg(what).arg(row)));
        }
    }
};

} // namespace

class TestVbusAnalog : public QObject {
    Q_OBJECT
private slots:
    void numericValuesMatchTheVector() {
        auto st = makeStore(600);
        WaveSpace space;
        space.setCurrent(st);
        const Signal& cnt = *space.findPath("top.cnt");
        for (bool isSigned : {false, true}) {
            const NumericIndex vec = NumericIndex::build(cnt, isSigned);
            for (const auto& members : memberSets()) {
                BusSource bus(space, members, true, isSigned);
                QCOMPARE(bus.waveClass(), WaveClass::Analog);
                QCOMPARE(bus.width(), 4u);
                QVERIFY(bus.numeric() != nullptr && bus.numeric()->valid());
                QCOMPARE(bus.count(), cnt.changeCount());
                for (std::size_t i = 0; i < bus.count(); ++i) {
                    QCOMPARE(bus.time(i), cnt.time(i));
                    const double a = bus.numeric()->value(i), b = vec.value(i);
                    QVERIFY2((std::isnan(a) && std::isnan(b)) || a == b,
                             qPrintable(QString("change %1: bus %2, vector %3").arg(i).arg(a).arg(b)));
                    // the same number as the bus's own text
                    const double fromBits = numericValueOfBits(bus.bits(i), isSigned);
                    QVERIFY((std::isnan(a) && std::isnan(fromBits)) || a == fromBits);
                }
                const MinMax r = bus.numeric()->range(0, bus.count());
                QCOMPARE(r.min, isSigned ? -8.0 : 0.0);
                QCOMPARE(r.max, isSigned ? 7.0 : 15.0);
            }
        }
    }

    void withoutAnalogItStaysABus() {
        auto st = makeStore(100);
        WaveSpace space;
        space.setCurrent(st);
        BusSource bus(space, memberSets()[0]);
        QCOMPARE(bus.waveClass(), WaveClass::Bus);
        QVERIFY(bus.numeric() == nullptr);
        // more than 64 bits: no numeric view, as for a vector
        std::vector<BusMember> wide;
        for (int i = 0; i < 17; ++i)
            wide.emplace_back("top.cnt");
        BusSource w(space, wide, true);
        QCOMPARE(w.width(), 68u);
        QCOMPARE(w.waveClass(), WaveClass::Bus);
        // a missing member reads as U: no value anywhere, but still analog
        BusSource missing(space, {"top.nope", "top.b0"}, true);
        QCOMPARE(missing.waveClass(), WaveClass::Analog);
        QVERIFY(missing.numeric()->range(0, missing.count()).empty());
    }

    void tilesMatchTheVectorAtEveryZoom() {
        auto st = makeStore(4000);
        WaveSpace space;
        space.setCurrent(st);
        PainterRenderer r;
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu)
            qInfo() << "no headless GPU, QPainter only:" << err;
        Palette pal;
        for (bool isSigned : {false, true}) {
            const NumericIndex idx = NumericIndex::build(*space.findPath("top.cnt"), isSigned);
            SignalSource vec(*space.findPath("top.cnt"), &idx, true);
            for (const auto& members : memberSets()) {
                BusSource bus(space, members, true, isSigned);
                for (bool linear : {false, true}) {
                    for (double tpp : {0.25, 1.0, 3.0, 16.0, 64.0}) {
                        for (long long tile : {0LL, 1LL, 3LL}) {
                            const QString where = QString("signed %1 linear %2 tpp %3 tile %4")
                                                      .arg(isSigned)
                                                      .arg(linear)
                                                      .arg(tpp)
                                                      .arg(tile);
                            TileRequest qv = request(vec, tpp, tile, linear),
                                        qb = request(bus, tpp, tile, linear);
                            qv.validEnd = qb.validEnd = st->endTime();
                            const QImage a = r.renderTile(qv), b = r.renderTile(qb);
                            QVERIFY2(a == b, qPrintable("QPainter " + where));
                            if (gpu) {
                                const QImage g = gpuTile(*gpu, qb, pal);
                                QVERIFY2(g == a, qPrintable("GPU " + where));
                            }
                        }
                    }
                }
            }
        }
    }

    void widgetDrawsBusLikeVector_data() {
        QTest::addColumn<int>("radix");
        QTest::newRow("unsigned") << static_cast<int>(Radix::Unsigned);
        QTest::newRow("signed") << static_cast<int>(Radix::Signed);
        QTest::newRow("hex") << static_cast<int>(Radix::Hex);
    }
    void widgetDrawsBusLikeVector() {
        QFETCH(int, radix);
        Fixture f(static_cast<Radix>(radix));
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        auto check = [&](const QString& what) {
            f.compareRows(f.waveArea(), "QPainter " + what);
            if (!gpu)
                return;
            const QSize size(f->waveWidth(), f->rowsBottom() - f->rowsTop());
            gpu->renderer().clearCache();
            f.compareRows(
                gpu->render(f->buildGpuFrame(size)).convertToFormat(QImage::Format_ARGB32_Premultiplied),
                "GPU " + what);
        };
        // full view, then deeper and deeper
        for (double tpp : {0.0, 2.0, 0.5, 0.1}) {
            if (tpp > 0)
                f->setView(150.0, tpp);
            check(QString("auto, tpp %1").arg(tpp));
        }
        // the automatic range of every row is the vector's
        const auto vr = f->effectiveAnalogRange(0);
        QVERIFY(vr.has_value());
        for (int row = 1; row < f->rowCount(); ++row) {
            const auto br = f->effectiveAnalogRange(row);
            QVERIFY(br.has_value());
            QCOMPARE(br->min, vr->min);
            QCOMPARE(br->max, vr->max);
        }
        QCOMPARE(vr->min, static_cast<Radix>(radix) == Radix::Signed ? -8.0 : 0.0);
        // a manual range on all rows
        f->selectRange(0, f->rowCount() - 1);
        QVERIFY(f->setAnalogRangeForSelection(false, 2.0, 9.0));
        f->setView(0.0, 1.0);
        check("manual 2..9");
    }

    void liveGrowthAndRestart() {
        Fixture f(Radix::Unsigned, 300);
        f->setView(0.0, 1.0);
        f.compareRows(f.waveArea(), "before growth");
        // the run goes on: the bus follows (range and new changes)
        unsigned v = 5;
        for (Time t = 310; t <= 700; t += 13) {
            appendStep(*f.store, t, bitsOf(v));
            v = (v + 7) & 15u;
        }
        f->dataChanged();
        f.compareRows(f.waveArea(), "after growth");
        QCOMPARE(f->effectiveAnalogRange(1)->max, f->effectiveAnalogRange(0)->max);
        // restart: the signals are cleared and refilled with other values
        for (const char* n : {"cnt", "b3", "b2", "b1", "b0"})
            f.store->mutableSignal(f.store->findSignal(std::string("top.") + n)).clearChanges();
        appendStep(*f.store, 0, "0000");
        for (Time t = 20; t <= 400; t += 20)
            appendStep(*f.store, t, bitsOf(static_cast<unsigned>(t / 20) % 6));
        f->dataChanged();
        f.compareRows(f.waveArea(), "after restart");
        QCOMPARE(f->effectiveAnalogRange(1)->max, 5.0);
    }

    void analogToggleAndExportCoverBuses() {
        Fixture f;
        // turning the trace off and on again from the menu action works on bus rows
        f->selectRow(1);
        f->toggleAnalogForSelection();
        QCOMPARE(f->rowClass(1), WaveClass::Bus);
        QVERIFY(!f->analogSelectionAvailable());
        f->toggleAnalogForSelection();
        QCOMPARE(f->rowClass(1), WaveClass::Analog);
        QVERIFY(f->analogSelectionAvailable());
        QVERIFY(f->setAnalogRangeForSelection(false, -1.0, 20.0));
        // the .do script keeps the bus analog with its range, and reading it back gives the same rows
        const std::string script = f->doScript();
        QVERIFY2(QString::fromStdString(script).contains(QRegularExpression(
                     "add wave -vbus vb0 -radix unsigned -format analog-step -min -1 -max 20")),
                 script.c_str());
        Fixture g;
        g->setDisplayModel(DisplayModel{});
        g->loadDoScript(script);
        QCOMPARE(g->rowCount(), f->rowCount());
        for (int row = 0; row < g->rowCount(); ++row)
            QCOMPARE(g->rowClass(row), WaveClass::Analog);
        QCOMPARE(g->effectiveAnalogRange(1)->min, -1.0);
        // and so does the layout file
        const DisplayModel back = DisplayModel::parse(f->displayModel().serialize()).value();
        QVERIFY(back.root().children[1].analog.enabled);
        QVERIFY(!back.root().children[1].analog.autoRange);
    }
};

QTEST_MAIN(TestVbusAnalog)
#include "test_vbus_analog.moc"
