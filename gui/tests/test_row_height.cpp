// SPDX-License-Identifier: GPL-3.0-or-later
// Row heights in pixels: the "Height..." dialog and menu entries, resizing by the bottom border, Alt+Up/Down,
// undo, saving and loading, drawing at other heights (QPainter and GPU alike) and GPU atlas memory.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/row_height_dialog.hpp"
#include "wave/tile_geometry.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId bus = st->addSignal(top, "bus", SignalKind::Logic, 8);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 64);
    for (Time t = 0; t <= 1000; t += 50) {
        st->mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
        st->mutableSignal(bus).appendLogic(t, (t / 50) % 2 ? "10100101" : "01011010");
        st->mutableSignal(volt).appendReal(t, static_cast<double>((t / 50) % 7));
    }
    st->extendTimeRange(0);
    st->extendTimeRange(1000);
    return st;
}

// rows: clk, bus, volt (analog), vb (virtual bus), divider
struct Fixture {
    std::unique_ptr<WaveWidget> w = std::make_unique<WaveWidget>();
    Fixture() {
        w->resize(900, 600);
        DisplayModel m;
        m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
        m.root().children.push_back(DisplayModel::makeSignal("top.bus", Radix::Hex));
        DisplayItem volt = DisplayModel::makeSignal("top.volt");
        volt.analog.enabled = true;
        m.root().children.push_back(volt);
        m.root().children.push_back(DisplayModel::makeBus("vb", {"top.clk", BusMember("top.bus", 0)}));
        m.root().children.push_back(DisplayModel::makeDivider("div"));
        w->setDisplayModel(m);
        w->setStore(makeStore());
        w->setView(0.0, 1.5);
    }
    WaveWidget* operator->() { return w.get(); }
    QPoint borderOf(int row) const { return QPoint(40, w->rowTop(row) + w->rowHeightAt(row) - 1); }
};

TileRequest request(const RowSource& src, int height, double tpp = 1.0) {
    TileRequest r;
    r.source = &src;
    r.timePerPixel = tpp;
    r.widthPx = IRenderer::kTileWidth;
    r.heightPx = height;
    return r;
}

QImage gpuTile(GpuOffscreen& gpu, const TileRequest& r, const Palette& pal) {
    gpu.renderer().clearCache();
    GpuFrame f;
    f.size = QSize(r.widthPx, r.heightPx);
    f.background = Qt::transparent;
    f.tileClip = QRect(QPoint(0, 0), f.size);
    GpuTileDraw t;
    t.key.row = reinterpret_cast<const void*>(std::uintptr_t{0x4000});
    t.key.revision = 1;
    t.key.scaleBits = scaleBitsOf(r.timePerPixel);
    t.key.tile = r.tileIndex;
    t.key.heightPx = r.heightPx;
    t.heightPx = r.heightPx;
    f.tiles.push_back(t);
    f.geometryFor = [&](const GpuTileDraw&) { return buildTileGeometry(r, pal); };
    return gpu.render(f).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

/// First and last row of pixels with anything drawn (alpha != 0).
std::pair<int, int> drawnRows(const QImage& img) {
    int lo = -1, hi = -1;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) != 0) {
                if (lo < 0)
                    lo = y;
                hi = y;
                break;
            }
    return {lo, hi};
}

} // namespace

class TestRowHeight : public QObject {
    Q_OBJECT
private slots:
    void defaultsAndSetForEveryKind() {
        Fixture f;
        QCOMPARE(f->rowHeightAt(0), 22);
        QCOMPARE(f->rowHeightAt(2), 4 * 22); // analog: its number of lines
        for (int row = 0; row < f->rowCount(); ++row) {
            f->selectRow(row);
            QVERIFY(f->heightSelectionAvailable());
            QVERIFY(f->setRowHeightForSelection(30 + row * 10));
            QCOMPARE(f->rowHeightAt(row), 30 + row * 10);
        }
        // rows are stacked by their heights
        for (int row = 1; row < f->rowCount(); ++row)
            QCOMPARE(f->rowTop(row), f->rowTop(row - 1) + f->rowHeightAt(row - 1));
        // limits
        f->selectRow(0);
        QVERIFY(f->setRowHeightForSelection(3));
        QCOMPARE(f->rowHeightAt(0), kMinRowHeightPx);
        QVERIFY(f->setRowHeightForSelection(100000));
        QCOMPARE(f->rowHeightAt(0), kMaxRowHeightPx);
        QVERIFY(!f->setRowHeightForSelection(100000)); // unchanged
        QVERIFY(!f->lastEditError().isEmpty());
        // several rows at once, then back to the defaults
        f->selectRange(0, f->rowCount() - 1);
        QVERIFY(f->setRowHeightForSelection(40));
        for (int row = 0; row < f->rowCount(); ++row)
            QCOMPARE(f->rowHeightAt(row), 40);
        QVERIFY(f->setRowHeightForSelection(0));
        QCOMPARE(f->rowHeightAt(0), 22);
        QCOMPARE(f->rowHeightAt(2), 88);
    }

    void bitRowsKeepTheDefault() {
        Fixture f;
        f->selectRow(1);
        QVERIFY(f->setRowHeightForSelection(50));
        f->toggleBits(1);
        QCOMPARE(f->rowHeightAt(1), 50);
        QCOMPARE(f->rowBit(2), 7);
        QCOMPARE(f->rowHeightAt(2), 22);
        f->selectRow(2); // a bit row only: nothing to resize
        QVERIFY(!f->heightSelectionAvailable());
        QVERIFY(!f->setRowHeightForSelection(60));
        QCOMPARE(f->rowBorderAt(QPoint(40, f->rowTop(2) + 21)), -1);
    }

    void undoRedo() {
        Fixture f;
        f->selectRow(1);
        QVERIFY(f->setRowHeightForSelection(64));
        QVERIFY(f->canUndo());
        QVERIFY(f->undo());
        QCOMPARE(f->rowHeightAt(1), 22);
        QVERIFY(f->redo());
        QCOMPARE(f->rowHeightAt(1), 64);
    }

    void dialogFromTheMenus() {
        Fixture f;
        f->selectRow(0);
        int offered = -1;
        std::pair<int, int> limits;
        f->setRowHeightDialogRunner([&](QDialog& d) {
            auto& dlg = static_cast<RowHeightDialog&>(d);
            offered = dlg.heightPx();
            limits = {dlg.spinBox()->minimum(), dlg.spinBox()->maximum()};
            dlg.open();
            dlg.setHeightPx(45);
            dlg.okButton()->click();
            return dlg.result() == QDialog::Accepted;
        });
        std::unique_ptr<QMenu> menu(f->createContextMenu(2)); // the clicked row is selected
        QAction* a = menu->findChild<QAction*>("rowHeight");
        QVERIFY(a != nullptr && a->isEnabled());
        a->trigger();
        QCOMPARE(offered, 88); // the clicked row's height
        QCOMPARE(limits.first, kMinRowHeightPx);
        QCOMPARE(limits.second, kMaxRowHeightPx);
        QCOMPARE(f->rowHeightAt(2), 45);
        // the Wave menu (arrange actions) has it too; "Default" resets
        f->setRowHeightDialogRunner([&](QDialog& d) {
            auto& dlg = static_cast<RowHeightDialog&>(d);
            dlg.open();
            dlg.defaultButton()->click();
            return dlg.result() == QDialog::Accepted;
        });
        QMenu waveMenu;
        f->populateArrangeMenu(&waveMenu);
        QAction* b = waveMenu.findChild<QAction*>("rowHeight");
        QVERIFY(b != nullptr && b->isEnabled());
        b->trigger();
        QCOMPARE(f->rowHeightAt(2), 88);
        // Cancel changes nothing
        f->setRowHeightDialogRunner([](QDialog& d) {
            static_cast<RowHeightDialog&>(d).setHeightPx(300);
            d.reject();
            return false;
        });
        QVERIFY(!f->showRowHeightDialog());
        QCOMPARE(f->rowHeightAt(2), 88);
    }

    void dragTheBottomBorder() {
        Fixture f;
        const QPoint p = f.borderOf(1);
        QCOMPARE(f->rowBorderAt(p), 1);
        QCOMPARE(f->rowBorderAt(p + QPoint(0, -3)), 1);
        QCOMPARE(f->rowBorderAt(p + QPoint(0, 1)), 1);
        QCOMPARE(f->rowBorderAt(p + QPoint(0, 2)), -1); // inside the next row: a click selects it
        QCOMPARE(f->rowBorderAt(p + QPoint(0, -5)), -1);
        QCOMPARE(f->rowBorderAt(QPoint(f->waveLeft() + 20, p.y())), -1); // not in the wave area
        QTest::mousePress(f.w.get(), Qt::LeftButton, Qt::NoModifier, p);
        QVERIFY(f->rowResizeActive());
        QMouseEvent move(QEvent::MouseMove, QPointF(p + QPoint(0, 30)),
                         QPointF(f->mapToGlobal(p + QPoint(0, 30))), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(f.w.get(), &move);
        QCOMPARE(f->rowHeightAt(1), 52); // live, before the release
        QCOMPARE(f->rowTop(2), f->rowTop(1) + 52);
        QTest::mouseRelease(f.w.get(), Qt::LeftButton, Qt::NoModifier, p + QPoint(0, 40));
        QVERIFY(!f->rowResizeActive());
        QCOMPARE(f->rowHeightAt(1), 62);
        QVERIFY(f->undo()); // one step for the whole drag
        QCOMPARE(f->rowHeightAt(1), 22);
        // shrinking stops at the minimum; Esc restores
        QVERIFY(f->beginRowResize(2, 100));
        f->updateRowResize(-500);
        QCOMPARE(f->rowHeightAt(2), kMinRowHeightPx);
        QTest::keyClick(f.w.get(), Qt::Key_Escape);
        QVERIFY(!f->rowResizeActive());
        QCOMPARE(f->rowHeightAt(2), 88);
        QVERIFY(!f->canUndo());
    }

    void altArrowKeys() {
        Fixture f;
        f->selectRange(0, 1);
        QTest::keyClick(f.w.get(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(f->rowHeightAt(0), 24);
        QCOMPARE(f->rowHeightAt(1), 24);
        QCOMPARE(f->selectedCount(), 2); // the selection did not move
        QTest::keyClick(f.w.get(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(f.w.get(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(f->rowHeightAt(0), 20);
    }

    void savedAndRestored() {
        Fixture f;
        f->selectRange(0, f->rowCount() - 1);
        QVERIFY(f->setRowHeightForSelection(37));
        // layout file
        auto back = DisplayModel::parse(f->displayModel().serialize());
        QVERIFY2(back.ok(), back.ok() ? "" : back.error().c_str());
        Fixture g;
        g->setDisplayModel(back.value());
        QCOMPARE(g->rowCount(), f->rowCount());
        for (int row = 0; row < g->rowCount(); ++row)
            QCOMPARE(g->rowHeightAt(row), 37);
        // add wave script
        const std::string script = f->doScript();
        QVERIFY2(QString::fromStdString(script).count("-height 37") == f->rowCount(), script.c_str());
        Fixture h;
        h->setDisplayModel(DisplayModel{});
        h->loadDoScript(script);
        QCOMPARE(h->rowCount(), f->rowCount());
        for (int row = 0; row < h->rowCount(); ++row)
            QCOMPARE(h->rowHeightAt(row), 37);
    }

    void tracesFollowTheHeight() {
        auto st = makeStore();
        const NumericIndex idx = NumericIndex::build(st->signal(st->findSignal("top.volt")));
        SignalSource analog(st->signal(st->findSignal("top.volt")), &idx, true);
        SignalSource clk(st->signal(st->findSignal("top.clk")), nullptr, false);
        SignalSource bus(st->signal(st->findSignal("top.bus")), nullptr, false);
        PainterRenderer r;
        for (int h : {12, 22, 37, 150}) {
            // the analog trace spans the row (3 px margins), the scalar and the bus are centred in it
            const auto [alo, ahi] =
                drawnRows(r.renderTile(request(analog, h, 4.0))); // the whole run: values 0 .. 6
            QCOMPARE(alo, 3);
            QCOMPARE(ahi, h - 4);
            for (const RowSource* s :
                 {static_cast<const RowSource*>(&clk), static_cast<const RowSource*>(&bus)}) {
                const auto [lo, hi] = drawnRows(r.renderTile(request(*s, h)));
                QVERIFY2(lo >= 0, qPrintable(QString("height %1: nothing drawn").arg(h)));
                QVERIFY2(std::abs(lo - (h - 1 - hi)) <= 1,
                         qPrintable(QString("height %1: drawn %2..%3, not centred").arg(h).arg(lo).arg(hi)));
            }
        }
    }

    void gpuTilesMatchAtOtherHeights() {
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu)
            QSKIP(qPrintable("no headless GPU: " + err));
        auto st = makeStore();
        const NumericIndex idx = NumericIndex::build(st->signal(st->findSignal("top.volt")));
        SignalSource analog(st->signal(st->findSignal("top.volt")), &idx, true);
        SignalSource clk(st->signal(st->findSignal("top.clk")), nullptr, false);
        SignalSource bus(st->signal(st->findSignal("top.bus")), nullptr, false);
        PainterRenderer r;
        Palette pal;
        for (int h : {12, 17, 37, 61, 150, 400}) {
            for (const RowSource* s :
                 {static_cast<const RowSource*>(&analog), static_cast<const RowSource*>(&clk),
                  static_cast<const RowSource*>(&bus)}) {
                const TileRequest q = request(*s, h, 2.0);
                QVERIFY2(gpuTile(*gpu, q, pal) == r.renderTile(q), qPrintable(QString("height %1").arg(h)));
            }
        }
    }

    void gpuFrameMatchesQPainterWithResizedRows() {
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu)
            QSKIP(qPrintable("no headless GPU: " + err));
        WaveWidget w;
        w.resize(900, 400);
        DisplayModel m;
        DisplayItem clk = DisplayModel::makeSignal("top.clk");
        clk.heightPx = 47;
        m.root().children.push_back(clk);
        DisplayItem volt = DisplayModel::makeSignal("top.volt"); // labels belong to the overlay: rows without
        volt.analog.enabled = true;
        volt.heightPx = 133;
        m.root().children.push_back(volt);
        DisplayItem small = DisplayModel::makeSignal("top.clk");
        small.heightPx = 13;
        m.root().children.push_back(small);
        w.setDisplayModel(m);
        w.setStore(makeStore());
        w.setView(0.0, 1.5);
        QCOMPARE(w.rowHeightAt(1), 133);
        const QSize size(w.waveWidth(), w.rowsBottom() - w.rowsTop());
        gpu->renderer().clearCache();
        const QImage g =
            gpu->render(w.buildGpuFrame(size)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage c = w.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QRect area(w.waveLeft(), w.rowsTop(), size.width(), size.height());
        long differing = 0, total = 0;
        for (int y = 0; y < area.height(); ++y)
            for (int x = 1; x < area.width(); ++x) { // x = 0 is the border line, painted by the parent
                const QRgb a = g.pixel(x, y), b = c.pixel(area.left() + x, area.top() + y);
                ++total;
                if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
                    qAbs(qBlue(a) - qBlue(b)) > 2)
                    ++differing;
            }
        QVERIFY2(differing * 1000 < total,
                 qPrintable(QString("%1 of %2 pixels differ").arg(differing).arg(total)));
    }

    void gpuAtlasMemoryStaysBounded() {
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu)
            QSKIP(qPrintable("no headless GPU: " + err));
        auto st = makeStore();
        SignalSource clk(st->signal(st->findSignal("top.clk")), nullptr, false);
        Palette pal;
        // a border dragged through many heights: every height gets an atlas, old ones are freed
        for (int h = 22; h < 22 + 120 * 3; h += 3) {
            const TileRequest q = request(clk, h);
            GpuFrame f;
            f.size = QSize(q.widthPx, q.heightPx);
            f.background = Qt::transparent;
            f.tileClip = QRect(QPoint(0, 0), f.size);
            GpuTileDraw t;
            t.key.row = &clk;
            t.key.revision = 1;
            t.key.scaleBits = scaleBitsOf(q.timePerPixel);
            t.key.tile = 0;
            t.key.heightPx = h;
            t.heightPx = h;
            f.tiles.push_back(t);
            f.geometryFor = [&](const GpuTileDraw&) { return buildTileGeometry(q, pal); };
            const QImage img = gpu->render(f);
            QVERIFY(!img.isNull());
            QVERIFY2(
                gpu->renderer().atlasBytes() <= GpuWaveRenderer::kAtlasBudgetBytes,
                qPrintable(QString("%1 MB at height %2").arg(gpu->renderer().atlasBytes() >> 20).arg(h)));
            QCOMPARE(gpu->renderer().lastFrame().tilesSkipped, std::size_t{0});
        }
        QVERIFY(gpu->renderer().atlasHeights() < 120);
    }
};

QTEST_MAIN(TestRowHeight)
#include "test_row_height.moc"
