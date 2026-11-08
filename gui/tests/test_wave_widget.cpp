// SPDX-License-Identifier: GPL-3.0-or-later
#include "rhi/gpu_wave_renderer.hpp"
#include "rhi/rhi_wave_canvas.hpp"
#include "wave/wave_widget.hpp"

#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(top, "cnt", SignalKind::Logic, 8);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 64);
    const SignalId lo = st->addSignal(top, "lo", SignalKind::Logic, 4);
    for (Time t = 0; t <= 1000; t += 50) {
        st->mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
        st->mutableSignal(volt).appendReal(t, static_cast<double>(t) / 100.0);
    }
    st->mutableSignal(cnt).appendLogic(0, "00000000");
    st->mutableSignal(cnt).appendLogic(300, "10100101");
    st->mutableSignal(cnt).appendLogic(700, "1111XXXX");
    st->mutableSignal(lo).appendLogic(0, "0001");
    st->extendTimeRange(0);
    st->extendTimeRange(1000);
    return st;
}

DisplayModel makeModel() {
    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
    m.root().children.push_back(DisplayModel::makeSignal("top.cnt", Radix::Hex));
    m.root().children.push_back(DisplayModel::makeSignal("top.volt"));
    m.root().children.push_back(DisplayModel::makeSignal("top.missing"));
    return m;
}

}  // namespace

class TestWaveWidget : public QObject {
    Q_OBJECT

    std::unique_ptr<WaveWidget> make() {
        auto w = std::make_unique<WaveWidget>();
        w->resize(900, 320);
        w->setDisplayModel(makeModel());
        w->setStore(makeStore());
        return w;
    }

    static QPoint waveClickPoint(const WaveWidget& w, int dx, int row) {
        return QPoint(w.waveLeft() + dx, w.rowTop(row) + 5);
    }

private slots:
    void bareWidgetStartsOnQPainter() {
        auto w = make();
        QCOMPARE(w->renderBackend(), WaveWidget::Backend::Cpu);
        QCOMPARE(w->requestedBackend(), WaveWidget::Backend::Cpu);
        QVERIFY(w->gpuCanvas() == nullptr);
        QVERIFY(w->fallbackReason().isEmpty());
    }

    void gpuRequestFallsBackWhenWindowCannotUseIt() {
        auto w = make();
        QSignalSpy spy(w.get(), &WaveWidget::renderBackendChanged);
        w->setRenderBackend(WaveWidget::Backend::Gpu);
        QCOMPARE(w->requestedBackend(), WaveWidget::Backend::Gpu);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::qWait(100);
        if (QGuiApplication::platformName() == "offscreen") {
            // No RHI on this platform: the request is kept, the widget draws with QPainter.
            QCOMPARE(w->renderBackend(), WaveWidget::Backend::Cpu);
            QVERIFY(w->gpuCanvas() == nullptr);
            QCOMPARE(w->fallbackReason(), QString("platform offscreen"));
        } else if (w->renderBackend() == WaveWidget::Backend::Gpu) {
            QVERIFY(w->gpuCanvas()->isVisible());
        } else {
            QVERIFY(!w->gpuCanvas()->isVisible());
        }
        w->setRenderBackend(WaveWidget::Backend::Cpu);
        QCOMPARE(w->renderBackend(), WaveWidget::Backend::Cpu);
        QVERIFY(!w->snapshot().isNull());
    }

    void gpuFrameCoversVisibleRowsOnly() {
        auto w = make();
        w->setView(0.0, 1.0);
        const QSize size(w->waveWidth(), w->height() - 40);
        const GpuFrame f = w->buildGpuFrame(size);
        QCOMPARE(f.size, size);
        QVERIFY(!f.tiles.empty());
        QVERIFY(static_cast<bool>(f.geometryFor));
        // Three rows have data, the unknown path has none.
        QSet<int> heights;
        QSet<quintptr> rows;
        for (const GpuTileDraw& t : f.tiles) {
            QVERIFY(t.y >= 0 && t.y < size.height());
            heights.insert(t.heightPx);
            rows.insert(reinterpret_cast<quintptr>(t.key.row));
        }
        QCOMPARE(rows.size(), 3);
        const TileGeometry g = f.geometryFor(f.tiles.front());
        QCOMPARE(g.width, GpuWaveRenderer::kTileWidth);
        QVERIFY(!g.prims.empty());
    }

    void gpuFrameMatchesQPainterWaveArea() {
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu) QSKIP(qPrintable("no headless GPU: " + err));
        WaveWidget w;
        w.resize(900, 200);
        DisplayModel m;
        m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
        DisplayItem volt = DisplayModel::makeSignal("top.volt");
        volt.analog.enabled = true;  // labels belong to the overlay, so compare rows without any
        m.root().children.push_back(volt);
        w.setDisplayModel(m);
        w.setStore(makeStore());
        w.setView(0.0, 1.5);
        const QSize size(w.waveWidth(), w.rowsBottom() - w.rowsTop());
        gpu->renderer().clearCache();
        const QImage g = gpu->render(w.buildGpuFrame(size)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage c = w.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QRect area(w.waveLeft(), w.rowsTop(), size.width(), size.height());
        long differing = 0, total = 0;
        for (int y = 0; y < area.height(); ++y) {
            for (int x = 1; x < area.width(); ++x) {  // x = 0 is the border line, painted by the parent
                const QRgb a = g.pixel(x, y), b = c.pixel(area.left() + x, area.top() + y);
                ++total;
                if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 || qAbs(qBlue(a) - qBlue(b)) > 2) ++differing;
            }
        }
        qInfo() << "differing pixels:" << differing << "of" << total;
        if (qEnvironmentVariableIsSet("LVD_DUMP_DIFF")) {
            QImage d(area.size(), QImage::Format_RGB32);
            d.fill(Qt::black);
            for (int y = 0; y < area.height(); ++y)
                for (int x = 0; x < area.width(); ++x)
                    if (g.pixel(x, y) != c.pixel(area.left() + x, area.top() + y)) d.setPixel(x, y, qRgb(255, 0, 0));
            d.save("diff.png");
            g.save("gpu.png");
            c.copy(area).save("cpu.png");
        }
        QVERIFY2(differing * 1000 < total, qPrintable(QString("%1 of %2 pixels differ").arg(differing).arg(total)));
    }

    void rowsAndNames() {
        auto w = make();
        QCOMPARE(w->rowCount(), 4);
        QCOMPARE(QString::fromStdString(w->rowName(0)), QString("top.clk"));
        QVERIFY(w->rowHasSource(0));
        QVERIFY(!w->rowHasSource(3));  // unknown path has no data
        QCOMPARE(w->rowClass(0), WaveClass::Scalar);
        QCOMPARE(w->rowClass(1), WaveClass::Bus);
    }

    void zoomFitsWholeTrace() {
        auto w = make();
        QCOMPARE(w->view().startTime, 0.0);
        QVERIFY(qAbs(w->view().endTime() - 1000.0) < 1.0);
    }

    void clickPlacesCursorAndShowsValue() {
        auto w = make();
        w->resize(900, 320);
        w->setView(0.0, 1.0);  // one time unit per pixel
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::NoModifier, waveClickPoint(*w, 400, 1));
        QVERIFY(w->cursorTime(0).has_value());
        QVERIFY(qAbs(*w->cursorTime(0) - 400.0) <= 1.0);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("A5"));
        QCOMPARE(QString::fromStdString(w->rowValueText(0)), QString("0"));  // clk at 400: (400/50)%2 == 0
        QCOMPARE(w->selectedCount(), 1);                                      // the click selected the row
        QCOMPARE(QString::fromStdString(w->rowValueText(3)), QString(""));   // no data
    }

    void secondCursorWithShift() {
        auto w = make();
        w->setView(0.0, 1.0);
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::NoModifier, waveClickPoint(*w, 100, 0));
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::ShiftModifier, waveClickPoint(*w, 350, 0));
        QVERIFY(w->cursorTime(0) && w->cursorTime(1));
        QVERIFY(qAbs(*w->cursorTime(1) - *w->cursorTime(0) - 250.0) <= 2.0);
    }

    void radixChangesValueText() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->setCursorTime(0, 400.0);
        w->selectRow(1);
        w->setRadixForSelection(Radix::Unsigned);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("165"));
        w->setRadixForSelection(Radix::Signed);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("-91"));
        w->setRadixForSelection(Radix::Binary);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("10100101"));
        w->setCursorTime(0, 800.0);
        w->setRadixForSelection(Radix::Hex);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("FX"));
    }

    void valueBeforeFirstChangeIsDash() {
        auto w = std::make_unique<WaveWidget>();
        w->resize(900, 320);
        auto st = makeStore();
        const SignalId late = st->addSignal(ScopeId{1}, "late", SignalKind::Logic, 4);
        st->mutableSignal(late).appendLogic(500, "1111");
        DisplayModel m;
        m.root().children.push_back(DisplayModel::makeSignal("top.late"));
        w->setDisplayModel(m);
        w->setStore(st);
        w->setCursorTime(0, 100.0);
        QCOMPARE(QString::fromStdString(w->rowValueText(0)), QString("-"));  // nothing has happened yet
        w->setCursorTime(0, 500.0);
        QCOMPARE(QString::fromStdString(w->rowValueText(0)), QString("F"));
    }

    void edgeNavigation() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->selectRow(0);
        w->setCursorTime(0, 0.0);
        QVERIFY(w->jumpToEdge(+1));
        QCOMPARE(*w->cursorTime(0), 50.0);
        QVERIFY(w->jumpToEdge(+1));
        QCOMPARE(*w->cursorTime(0), 100.0);
        QVERIFY(w->jumpToEdge(-1));
        QCOMPARE(*w->cursorTime(0), 50.0);
        w->setCursorTime(0, 1000.0);
        QVERIFY(!w->jumpToEdge(+1));  // nothing after the last change
        w->setCursorTime(0, 0.0);
        QVERIFY(!w->jumpToEdge(-1));  // nothing before the first
        w->clearSelection();
        QVERIFY(w->jumpToEdge(+1));  // without a selection every row is considered
        QCOMPARE(*w->cursorTime(0), 50.0);  // clk and volt change at 50
    }

    void wheelZoomKeepsTimeUnderPointer() {
        auto w = make();
        w->setView(0.0, 1.0);
        const QPointF p(w->waveLeft() + 300.0, w->rowTop(0) + 5.0);
        const double before = w->view().timeAt(300);
        QWheelEvent ev(p, w->mapToGlobal(p), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(w.get(), &ev);
        QVERIFY(w->view().timePerPixel < 1.0);
        QVERIFY(qAbs(w->view().timeAt(300) - before) < 1e-6);
    }

    void keyboardZoomAndFit() {
        auto w = make();
        const double tpp = w->view().timePerPixel;
        QTest::keyClick(w.get(), Qt::Key_Plus);
        QVERIFY(w->view().timePerPixel < tpp);
        QTest::keyClick(w.get(), Qt::Key_F);
        QVERIFY(qAbs(w->view().timePerPixel - tpp) < 1e-9);
    }

    void groupBusAndRemove() {
        auto w = make();
        w->selectRow(0);
        w->selectRow(1, true);
        QCOMPARE(w->selectedCount(), 2);
        QVERIFY(w->groupSelection("Pair"));
        QCOMPARE(w->rowCount(), 2 + 1 + 2);  // group row, its two children, volt, missing
        QCOMPARE(QString::fromStdString(w->rowName(0)), QString("Pair"));

        w->toggleGroup(0);  // collapse
        QCOMPARE(w->rowCount(), 3);
        w->toggleGroup(0);
        QCOMPARE(w->rowCount(), 5);

        w->selectRow(1);
        w->selectRow(2, true);
        QVERIFY(w->busFromSelection("clkcnt"));
        QCOMPARE(w->rowCount(), 6);
        QCOMPARE(QString::fromStdString(w->rowName(3)), QString("clkcnt"));
        QVERIFY(w->rowHasSource(3));
        QCOMPARE(w->rowClass(3), WaveClass::Bus);

        w->setCursorTime(0, 400.0);
        w->selectRow(3);
        w->setRadixForSelection(Radix::Binary);
        QCOMPARE(QString::fromStdString(w->rowValueText(3)), QString("010100101"));  // clk(1 bit) + cnt(8 bits)

        w->selectRow(3);
        w->removeSelection();
        QCOMPARE(w->rowCount(), 5);
    }

    void analogToggleChangesRowHeight() {
        auto w = make();
        w->selectRow(2);
        w->toggleAnalogForSelection();
        QCOMPARE(w->rowClass(2), WaveClass::Analog);
        QCOMPARE(w->rowTop(3) - w->rowTop(2), 4 * w->rowHeight());
        w->toggleAnalogForSelection();
        QCOMPARE(w->rowClass(2), WaveClass::Bus);  // a real signal without analog view is a value row
        QCOMPARE(w->rowTop(3) - w->rowTop(2), w->rowHeight());
    }

    void paintingFillsTheCache() {
        auto w = make();
        const QImage first = w->grab().toImage();
        QVERIFY(!first.isNull());
        QVERIFY(w->tileCache().size() > 0);
        const std::size_t misses = w->tileCache().misses();
        const QImage second = w->grab().toImage();
        QCOMPARE(w->tileCache().misses(), misses);  // second paint is served from the cache
        QVERIFY(w->tileCache().hits() > 0);
        QCOMPARE(first, second);

        // The drawing contains something other than background.
        bool nonBackground = false;
        const QRgb bg = first.pixel(w->waveLeft() + 3, w->rowTop(0) + 10);
        for (int x = w->waveLeft() + 3; x < first.width() - 30 && !nonBackground; ++x)
            for (int y = w->rowTop(0); y < w->rowTop(0) + 22; ++y)
                if (first.pixel(x, y) != bg) { nonBackground = true; break; }
        QVERIFY(nonBackground);
    }

    void traceUsesPaletteColours() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->resize(900, 320);
        const QImage img = w->grab().toImage();
        const Palette defaults;
        // clk is low at t = 0..49: the low level line must be the palette colour, not black.
        const int y = w->rowTop(0) + 18;  // low level line of a 22 pixel row
        const QColor c = img.pixelColor(w->waveLeft() + 20, y);
        QCOMPARE(c.red(), defaults.low.red());
        QCOMPARE(c.green(), defaults.low.green());
        QCOMPARE(c.blue(), defaults.low.blue());
    }

    void emptyWidgetPaints() {
        WaveWidget w;
        w.resize(500, 200);
        QVERIFY(!w.grab().toImage().isNull());
        QCOMPARE(w.rowCount(), 0);
    }

    void splitterDragResizesColumns() {
        auto w = make();
        const int before = w->waveLeft();
        QTest::mousePress(w.get(), Qt::LeftButton, Qt::NoModifier, QPoint(220, 100));
        QTest::mouseMove(w.get(), QPoint(300, 100));
        QTest::mouseRelease(w.get(), Qt::LeftButton, Qt::NoModifier, QPoint(300, 100));
        QVERIFY(w->waveLeft() > before);
    }

    void middleDragPans() {
        auto w = make();
        w->setView(100.0, 1.0);
        QTest::mousePress(w.get(), Qt::MiddleButton, Qt::NoModifier, QPoint(w->waveLeft() + 200, 100));
        QTest::mouseMove(w.get(), QPoint(w->waveLeft() + 150, 100));
        QTest::mouseRelease(w.get(), Qt::MiddleButton, Qt::NoModifier, QPoint(w->waveLeft() + 150, 100));
        QVERIFY(w->view().startTime > 100.0);  // dragging left moves the view to later times
    }

    // ---- single bits of a vector ----------------------------------------------------------------
    void vectorExpandsIntoBitRows() {
        auto w = make();
        QVERIFY(w->rowExpandable(1));   // cnt: 8 bit vector
        QVERIFY(!w->rowExpandable(0));  // clk: one bit
        QVERIFY(!w->rowExpandable(2));  // volt: real
        QCOMPARE(w->rowBit(1), -1);

        w->setCursorTime(0, 400.0);  // cnt = 10100101
        w->toggleBits(1);
        QCOMPARE(w->rowCount(), 4 + 8);
        QCOMPARE(QString::fromStdString(w->rowName(2)), QString("top.cnt[7]"));
        QCOMPARE(QString::fromStdString(w->rowName(9)), QString("top.cnt[0]"));
        QCOMPARE(w->rowBit(2), 7);
        QCOMPARE(w->rowBit(9), 0);
        QCOMPARE(w->rowClass(2), WaveClass::Scalar);
        QVERIFY(!w->rowExpandable(2));
        QVERIFY(w->displayModel().root().children[1].showBits);

        const char* expected = "10100101";  // bit 7 first
        for (int i = 0; i < 8; ++i)
            QCOMPARE(QString::fromStdString(w->rowValueText(2 + i)), QString(expected[i]));

        w->setCursorTime(0, 800.0);  // cnt = 1111XXXX
        QCOMPARE(QString::fromStdString(w->rowValueText(2)), QString("1"));
        QCOMPARE(QString::fromStdString(w->rowValueText(9)), QString("X"));

        w->toggleBits(1);  // collapse again
        QCOMPARE(w->rowCount(), 4);
        QVERIFY(!w->displayModel().root().children[1].showBits);
        w->toggleBits(0);  // not expandable: ignored
        QCOMPARE(w->rowCount(), 4);
    }

    void clickingTheTriangleExpandsBits() {
        auto w = make();
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::NoModifier, QPoint(6 + 6, w->rowTop(1) + 8));
        QCOMPARE(w->rowCount(), 12);
        QTest::mouseDClick(w.get(), Qt::LeftButton, Qt::NoModifier, QPoint(60, w->rowTop(1) + 8));
        QCOMPARE(w->rowCount(), 4);  // double click on the name toggles too
    }

    void bitRowsPaintFlatForConstantBits() {
        auto w = make();
        w->toggleBits(1);
        w->setView(0.0, 1.0);
        const QImage img = w->grab().toImage();
        // cnt[7] is 0 until t=300 and 1 afterwards (then 1 at 700): constant between, so one edge only.
        const int top = w->rowTop(2);
        const int x = w->waveLeft() + 100;
        bool lowLine = false;
        for (int y = top; y < top + 22; ++y)
            if (img.pixelColor(x, y).green() > 150) lowLine = true;
        QVERIFY(lowLine);
    }

    // ---- sparse virtual bus ---------------------------------------------------------------------
    void sparseBitsMakeAVirtualBus() {
        auto w = make();
        w->toggleBits(1);                       // rows: clk, cnt, cnt[7..0], volt, missing
        w->selectRow(0);                        // clk
        w->selectRow(2, true);                  // cnt[7]
        w->selectRow(4, true);                  // cnt[5]
        w->selectRow(9, true);                  // cnt[0]
        QCOMPARE(w->selectedCount(), 4);
        QVERIFY(w->busFromSelection("sparse"));

        QCOMPARE(w->rowCount(), 12 + 1);
        // inserted right after the vector that owns the last selected bit
        QCOMPARE(QString::fromStdString(w->rowName(10)), QString("sparse"));
        QVERIFY(w->rowHasSource(10));
        QCOMPARE(w->rowClass(10), WaveClass::Bus);

        w->selectRow(10);
        w->setRadixForSelection(Radix::Binary);
        w->setCursorTime(0, 400.0);  // clk = 0, cnt = 10100101: bits 7, 5, 0 are all 1
        QCOMPARE(QString::fromStdString(w->rowValueText(10)), QString("0111"));
        w->setCursorTime(0, 50.0);   // clk = 1, cnt = 00000000
        QCOMPARE(QString::fromStdString(w->rowValueText(10)), QString("1000"));

        const std::string text = w->displayModel().serialize();
        QVERIFY(text.find("member \"top.cnt\" bit=7") != std::string::npos);
        QVERIFY(text.find("member \"top.cnt\" bit=0") != std::string::npos);
        QVERIFY(text.find("member \"top.clk\"\n") != std::string::npos);
    }

    void aSingleBitIsNotABusButTwoAre() {
        auto w = make();
        w->toggleBits(1);
        w->selectRow(2);
        QVERIFY(!w->busFromSelection("x"));  // one bit only
        w->selectRow(2);
        w->selectRow(3, true);
        QVERIFY(w->busFromSelection("pair"));
        QVERIFY(!w->busFromSelection("again"));  // selection was cleared
    }

    void shiftClickSelectsARange() {
        auto w = make();
        w->toggleBits(1);
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::NoModifier, QPoint(100, w->rowTop(2) + 8));
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::ShiftModifier, QPoint(100, w->rowTop(5) + 8));
        QCOMPARE(w->selectedCount(), 4);  // rows 2..5
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::ControlModifier, QPoint(100, w->rowTop(3) + 8));
        QCOMPARE(w->selectedCount(), 3);  // ctrl-click removes one
    }

    // ---- cursors --------------------------------------------------------------------------------
    void severalCursorsAndTheActiveOne() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->setCursorTime(0, 100.0);
        QCOMPARE(w->cursorCount(), 1);
        const int c = w->addCursor(400.0);
        QCOMPARE(c, 1);
        QCOMPARE(w->cursorCount(), 2);
        QCOMPARE(w->activeCursor(), 1);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("A5"));  // value column follows the active cursor
        w->setActiveCursor(0);
        QCOMPARE(QString::fromStdString(w->rowValueText(1)), QString("00"));

        const int mid = w->addCursor();  // default: middle of the view
        QVERIFY(qAbs(*w->cursorTime(mid) - w->view().timeAt(w->view().widthPx / 2.0)) < 1.0);

        w->removeCursor(1);
        QCOMPARE(w->cursorCount(), 2);
        w->removeCursor(0);
        w->removeCursor(0);
        QCOMPARE(w->cursorCount(), 0);
        w->removeCursor(0);  // harmless
        QVERIFY(!w->cursorTime(0).has_value());
    }

    void cursorLimit() {
        auto w = make();
        for (int i = 0; i < WaveWidget::kMaxCursors; ++i) QVERIFY(w->addCursor(i * 10.0) >= 0);
        QCOMPARE(w->addCursor(1.0), -1);
        QCOMPARE(w->cursorCount(), WaveWidget::kMaxCursors);
    }

    void shiftClickAddsAndDragMovesTheNearestCursor() {
        auto w = make();
        w->setView(0.0, 1.0);
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::NoModifier, waveClickPoint(*w, 100, 0));
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::ShiftModifier, waveClickPoint(*w, 300, 0));
        QTest::mouseClick(w.get(), Qt::LeftButton, Qt::ShiftModifier, waveClickPoint(*w, 600, 0));
        QCOMPARE(w->cursorCount(), 3);

        // Press right next to the first cursor and drag: that cursor moves, the others stay.
        QTest::mousePress(w.get(), Qt::LeftButton, Qt::NoModifier, waveClickPoint(*w, 102, 0));
        QTest::mouseMove(w.get(), waveClickPoint(*w, 160, 0));
        QTest::mouseRelease(w.get(), Qt::LeftButton, Qt::NoModifier, waveClickPoint(*w, 160, 0));
        QCOMPARE(w->cursorCount(), 3);
        QVERIFY(qAbs(*w->cursorTime(0) - 160.0) <= 2.0);
        QVERIFY(qAbs(*w->cursorTime(1) - 300.0) <= 2.0);
        QVERIFY(qAbs(*w->cursorTime(2) - 600.0) <= 2.0);
        QCOMPARE(w->activeCursor(), 0);
    }

    void timingDifferenceIsDrawn() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->grab();
        const QImage before = w->grab().toImage();
        w->addCursor(200.0);
        w->addCursor(500.0);
        const QImage after = w->grab().toImage();
        QVERIFY(before != after);
        // the arrow line between the two cursors, just under the ruler
        const int y = w->rulerHeight() + 12;
        bool arrow = false;
        for (int x = w->waveLeft() + 250; x < w->waveLeft() + 450; ++x)
            if (after.pixelColor(x, y).red() > 180) arrow = true;
        QVERIFY(arrow);
    }

    // ---- navigation -----------------------------------------------------------------------------
    void startEndAndGoTo() {
        auto w = make();
        w->setView(300.0, 0.5);
        w->goToEnd();
        QCOMPARE(*w->cursorTime(0), 1000.0);
        QVERIFY(w->view().startTime <= 1000.0 && w->view().endTime() >= 1000.0);

        w->goToStart();
        QCOMPARE(*w->cursorTime(0), 0.0);
        QCOMPARE(w->view().startTime, 0.0);

        w->setView(0.0, 1.0);
        w->goToTime(700.0);
        QCOMPARE(w->cursorCount(), 1);  // moves the active cursor, does not add one
        QCOMPARE(*w->cursorTime(0), 700.0);
        QVERIFY(qAbs(w->view().timeAt(w->view().widthPx / 2.0) - 700.0) < 1.0);  // centred
        w->goToTime(10.0);
        QCOMPARE(w->view().startTime, 0.0);  // clamped, never before time zero
    }

    void edgeNavigationUsesSelectedRowsOnly() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->selectRow(1);  // cnt changes at 0, 300, 700 only
        w->setCursorTime(0, 0.0);
        QVERIFY(w->jumpToEdge(+1));
        QCOMPARE(*w->cursorTime(0), 300.0);
        QVERIFY(w->jumpToEdge(+1));
        QCOMPARE(*w->cursorTime(0), 700.0);
        QVERIFY(!w->jumpToEdge(+1));
        QVERIFY(w->jumpToEdge(-1));
        QCOMPARE(*w->cursorTime(0), 300.0);
    }

    void jumpScrollsTheViewToTheTarget() {
        auto w = make();
        w->setView(0.0, 0.1);  // sees only the first ~100 time units
        w->selectRow(1);
        w->setCursorTime(0, 0.0);
        QVERIFY(w->jumpToEdge(+1));  // 300 is outside the view
        QVERIFY(w->view().startTime <= 300.0 && w->view().endTime() >= 300.0);
    }

    // ---- colours --------------------------------------------------------------------------------
    void colourOverridesTheTrace() {
        auto w = make();
        w->setView(0.0, 1.0);
        QVERIFY(!w->rowColor(0).has_value());
        w->selectRow(0);
        w->setColorForSelection(QColor(0xff, 0x40, 0xc0));
        QVERIFY(w->rowColor(0).has_value());
        QCOMPARE(*w->rowColor(0), QColor(0xff, 0x40, 0xc0));

        const QImage img = w->grab().toImage();
        const QColor c = img.pixelColor(w->waveLeft() + 20, w->rowTop(0) + 18);  // clk low level line
        QCOMPARE(c.red(), 0xff);
        QCOMPARE(c.green(), 0x40);
        QCOMPARE(c.blue(), 0xc0);
        // other rows keep the default palette
        const QColor other = img.pixelColor(w->waveLeft() + 20, w->rowTop(1) + 3);
        QVERIFY(other.red() != 0xff);

        QVERIFY(w->displayModel().serialize().find("color=#ff40c0") != std::string::npos);

        w->setColorForSelection(std::nullopt);
        QVERIFY(!w->rowColor(0).has_value());
        const QImage again = w->grab().toImage();
        QCOMPARE(again.pixelColor(w->waveLeft() + 20, w->rowTop(0) + 18).red(), Palette{}.low.red());
    }

    void colourAppliesToBusesAndBitRows() {
        auto w = make();
        w->toggleBits(1);
        w->selectRow(1);
        w->setColorForSelection(QColor(0x20, 0x80, 0xff));
        QVERIFY(w->rowColor(2).has_value());  // a bit row shows its vector's colour
        QCOMPARE(*w->rowColor(2), QColor(0x20, 0x80, 0xff));
    }

    // ---- dividers -------------------------------------------------------------------------------
    void dividerIsInsertedAfterTheSelection() {
        auto w = make();
        w->selectRow(1);
        w->insertDivider("alu");
        QCOMPARE(w->rowCount(), 5);
        QCOMPARE(QString::fromStdString(w->rowName(2)), QString("alu"));
        QVERIFY(!w->rowHasSource(2));
        QCOMPARE(w->displayModel().root().children[2].kind, ItemKind::Divider);

        w->clearSelection();
        w->insertDivider("end");  // nothing selected: appended
        QCOMPARE(w->rowCount(), 6);
        QCOMPARE(QString::fromStdString(w->rowName(5)), QString("end"));

        w->insertDivider("");  // a blank separator is allowed
        QCOMPARE(w->rowCount(), 7);
        const QImage img = w->grab().toImage();
        QVERIFY(!img.isNull());
    }

};

QTEST_MAIN(TestWaveWidget)
#include "test_wave_widget.moc"
