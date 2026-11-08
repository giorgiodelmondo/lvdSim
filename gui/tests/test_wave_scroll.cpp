// SPDX-License-Identifier: GPL-3.0-or-later
// Scroll bars (time and rows) and the Follow mode of the wave window.
#include "wave/icons.hpp"
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QtTest>


using namespace lvd::gui;
using namespace lvd::wave;

namespace {

constexpr Time kEnd = 1000;

std::shared_ptr<WaveStore> makeStore(Time end = kEnd) {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 64);
    for (Time t = 0; t <= end; t += 50) {
        st->mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
        st->mutableSignal(volt).appendReal(t, static_cast<double>(t) / 100.0);
    }
    st->extendTimeRange(0);
    st->extendTimeRange(end);
    return st;
}

/// More data, as a live run delivers it.
void grow(WaveStore& st, Time from, Time to) {
    const SignalId clk = st.findSignal("top.clk");
    const SignalId volt = st.findSignal("top.volt");
    for (Time t = from; t <= to; t += 50) {
        st.mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
        st.mutableSignal(volt).appendReal(t, static_cast<double>(t) / 100.0);
    }
    st.extendTimeRange(to);
}

DisplayModel makeModel(int count = 2) {
    DisplayModel m;
    for (int i = 0; i < count; ++i)
        m.root().children.push_back(DisplayModel::makeSignal(i % 2 == 0 ? "top.clk" : "top.volt"));
    return m;
}

struct Fixture {
    std::shared_ptr<WaveStore> store = makeStore();
    std::unique_ptr<WaveWidget> w = std::make_unique<WaveWidget>();
    explicit Fixture(int rows = 2, QSize size = QSize(900, 320)) {
        w->resize(size);
        w->setDisplayModel(makeModel(rows));
        w->setStore(store);
    }
    WaveWidget* operator->() { return w.get(); }
    WaveWidget& operator*() { return *w; }
    void liveGrow(Time to) {
        grow(*store, store->endTime() + 50, to);
        w->dataChanged();
    }
};

void wheel(WaveWidget& w, QPoint angle, Qt::KeyboardModifiers mods, QPoint pos) {
    QWheelEvent ev(QPointF(pos), QPointF(w.mapToGlobal(pos)), QPoint(), angle, Qt::NoButton, mods,
                   Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&w, &ev);
}

} // namespace

class TestWaveScroll : public QObject {
    Q_OBJECT

    static double hbarStart(WaveWidget& w) { // the time the bar says the left edge is at
        QScrollBar* b = w.horizontalScrollBar();
        return 1000.0 * b->value() / WaveWidget::kHBarSteps;
    }

private slots:
    // ---- horizontal ----
    void zoomedOutToFullTheThumbFillsTheBar() {
        Fixture f;
        QScrollBar* b = f->horizontalScrollBar();
        QCOMPARE(b->minimum(), 0);
        QCOMPARE(b->maximum(), 0);
        QCOMPARE(b->pageStep(), WaveWidget::kHBarSteps);
        QCOMPARE(b->value(), 0);
    }

    void thumbIsTheVisibleWindow() {
        Fixture f;
        f->setView(250.0, 1000.0 / f->waveWidth() / 4.0); // a quarter of the run, starting at 250
        QScrollBar* b = f->horizontalScrollBar();
        QVERIFY(qAbs(b->pageStep() - WaveWidget::kHBarSteps / 4) < 4);
        QCOMPARE(b->maximum(), WaveWidget::kHBarSteps - b->pageStep());
        QVERIFY(qAbs(hbarStart(*f) - 250.0) < 0.01);
        QVERIFY(b->singleStep() >= 1 && b->singleStep() < b->pageStep());
    }

    void movingTheBarPansTheViewWithoutFeedback() {
        Fixture f;
        const double tpp = 1000.0 / f->waveWidth() / 4.0;
        f->setView(0.0, tpp);
        QSignalSpy viewSpy(f.w.get(), &WaveWidget::viewChanged);
        QScrollBar* b = f->horizontalScrollBar();
        QSignalSpy barSpy(b, &QScrollBar::valueChanged);
        const int target = b->maximum() / 2;
        b->setValue(target);
        QCOMPARE(viewSpy.count(), 1);
        QCOMPARE(barSpy.count(), 1); // no echo: the view change did not set the bar again
        QCOMPARE(b->value(), target);
        QVERIFY(qAbs(f->view().startTime - 1000.0 * target / WaveWidget::kHBarSteps) < 1e-6);
        QCOMPARE(f->view().timePerPixel, tpp); // panning never changes the zoom
        QVERIFY(!f->autoFit());

        b->setValue(b->maximum()); // the far end: the right edge is the end of the run
        QVERIFY(qAbs(f->view().endTime() - 1000.0) < 1e-6);
        b->setValue(0);
        QCOMPARE(f->view().startTime, 0.0);
    }

    void pageAndArrowStepsPan() {
        Fixture f;
        f->setView(0.0, 1000.0 / f->waveWidth() / 4.0);
        QScrollBar* b = f->horizontalScrollBar();
        b->triggerAction(QAbstractSlider::SliderPageStepAdd);
        QVERIFY(qAbs(f->view().startTime - 250.0) < 0.01); // one window
        b->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        QVERIFY(qAbs(f->view().startTime - 275.0) < 0.1); // a tenth of a window
        b->triggerAction(QAbstractSlider::SliderPageStepSub);
        QVERIFY(qAbs(f->view().startTime - 25.0) < 0.1);
    }

    void otherViewChangesMoveTheThumbAndLeaveTheViewAlone() {
        Fixture f;
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        f->setView(0.0, 1000.0 / f->waveWidth() / 4.0);
        QScrollBar* b = f->horizontalScrollBar();
        QSignalSpy barSpy(b, &QScrollBar::valueChanged);

        wheel(*f.w, QPoint(0, -120), Qt::ShiftModifier,
              QPoint(f->waveLeft() + 100, f->rowsTop() + 10)); // pan by wheel
        const double afterWheel = f->view().startTime;
        QVERIFY(afterWheel > 0.0);
        QVERIFY(qAbs(hbarStart(*f) - afterWheel) < 0.01);

        QTest::keyClick(f.w.get(), Qt::Key_Right, Qt::ControlModifier); // pan by key
        QVERIFY(f->view().startTime > afterWheel);
        QVERIFY(qAbs(hbarStart(*f) - f->view().startTime) < 0.01);

        const double tppBefore = f->view().timePerPixel;
        wheel(*f.w, QPoint(0, 120), Qt::ControlModifier,
              QPoint(f->waveLeft() + 300, f->rowsTop() + 10)); // zoom in
        QVERIFY(f->view().timePerPixel < tppBefore);
        QVERIFY(qAbs(hbarStart(*f) - f->view().startTime) < 0.01);
        QVERIFY(qAbs(b->pageStep() -
                     static_cast<double>(WaveWidget::kHBarSteps) * f->view().widthPx * f->view().timePerPixel / 1000.0) < 8);

        f->zoomFull();
        QCOMPARE(b->maximum(), 0);
        QCOMPARE(b->pageStep(), WaveWidget::kHBarSteps);
        QVERIFY(barSpy.count() >= 3);
    }

    void draggingTheThumbDoesNotFightTheMouse() {
        Fixture f;
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        f->setView(0.0, 1000.0 / f->waveWidth() / 4.0);
        QScrollBar* b = f->horizontalScrollBar();
        b->setSliderDown(true);
        b->setSliderPosition(b->maximum() / 3); // a drag step: tracking applies it
        const int dragged = b->value();
        QVERIFY(dragged > 0);
        QCOMPARE(b->value(), dragged); // updateHBar must not rewrite the value while the thumb is held
        f->dataChanged(); // even a data change in the middle of a drag keeps the thumb
        QCOMPARE(b->value(), dragged);
        b->setSliderDown(false);
    }

    void liveGrowthRescalesTheThumb() {
        Fixture f;
        f->setView(0.0, 1000.0 / f->waveWidth() / 2.0); // the first half
        QScrollBar* b = f->horizontalScrollBar();
        const int pageBefore = b->pageStep();
        f.liveGrow(2000);
        QVERIFY(b->pageStep() < pageBefore); // the run doubled: the window is a smaller part of it
        QVERIFY(qAbs(b->pageStep() - WaveWidget::kHBarSteps / 4) < 8);
        QCOMPARE(f->view().startTime, 0.0); // not following, not at the end: the view stays
    }

    void resizeKeepsTheBarInSync() {
        Fixture f;
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        f->setView(100.0, 0.5);
        QScrollBar* b = f->horizontalScrollBar();
        const int before = b->pageStep();
        f->resize(1400, 320);
        QTest::qWait(20);
        QVERIFY(b->pageStep() > before); // wider window shows more time
        QVERIFY(qAbs(hbarStart(*f) - f->view().startTime) < 0.01);
        // geometry: the bar sits under the wave area, left of the vertical bar
        QCOMPARE(b->geometry().left(), f->waveLeft());
        QCOMPARE(b->geometry().bottom() + 1, f->height());
        QCOMPARE(b->geometry().right() + 1, f->verticalScrollBar()->geometry().left());
        QCOMPARE(f->verticalScrollBar()->geometry().bottom() + 1, b->geometry().top());
        QCOMPARE(f->rowsBottom(), b->geometry().top());
    }

    void noStoreIsHarmless() {
        WaveWidget w;
        w.resize(600, 200);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCOMPARE(w.horizontalScrollBar()->maximum(), 0);
        w.zoomBy(2.0);
        w.setFollow(true);
        QVERIFY(w.follow());
    }

    // ---- vertical ----
    void verticalBarDrivesNameValueAndWaveRows() {
        Fixture f(60, QSize(900, 300));
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        QScrollBar* v = f->verticalScrollBar();
        const int page = f->rowsBottom() - f->rowsTop();
        QCOMPARE(v->pageStep(), page);
        QCOMPARE(v->maximum(), f->contentHeight() - page);
        QCOMPARE(v->singleStep(), f->rowHeight());
        QCOMPARE(f->rowTop(0), f->rowsTop());
        v->setValue(5 * f->rowHeight());
        QCOMPARE(f->scrollY(), 5 * f->rowHeight());
        QCOMPARE(f->rowTop(5), f->rowsTop());
        // one widget, one offset: a hit in the name column and one in the wave area give the same row
        const int y = f->rowTop(7) + 3;
        QTest::mouseClick(f.w.get(), Qt::LeftButton, {}, QPoint(20, y));
        QVERIFY(f->isRowSelected(7));
        // the GPU frame uses the same offset
        const GpuFrame frame = f->buildGpuFrame(QSize(f->waveWidth(), page));
        QVERIFY(!frame.tiles.empty());
        float minY = 1e9f;
        for (const GpuTileDraw& t : frame.tiles)
            if (t.y + t.heightPx > 0) minY = qMin(minY, t.y); // tiles that are visible at all
        QCOMPARE(minY, 0.0f); // row 5 is the first visible one and sits at the top of the canvas
    }

    void verticalWheelScrollsAndStaysInRange() {
        Fixture f(60, QSize(900, 300));
        QScrollBar* v = f->verticalScrollBar();
        wheel(*f.w, QPoint(0, -120), {}, QPoint(30, f->rowsTop() + 30));
        QCOMPARE(v->value(), 3 * f->rowHeight());
        wheel(*f.w, QPoint(0, 120 * 40), {}, QPoint(30, f->rowsTop() + 30));
        QCOMPARE(v->value(), 0);
        wheel(*f.w, QPoint(0, -120 * 400), {}, QPoint(30, f->rowsTop() + 30));
        QCOMPARE(v->value(), v->maximum());
    }

    void verticalRangeFollowsExpandCollapseResizeAndEdits() {
        Fixture f(40, QSize(900, 300));
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        QScrollBar* v = f->verticalScrollBar();
        const int max40 = v->maximum();
        QVERIFY(max40 > 0);
        f->resize(900, 500);
        QVERIFY(v->maximum() < max40);
        QCOMPARE(v->pageStep(), f->rowsBottom() - f->rowsTop());
        QCOMPARE(v->maximum(), f->contentHeight() - v->pageStep());

        // removing rows shrinks the range and clamps the offset
        v->setValue(v->maximum());
        f->selectRange(0, 29);
        f->removeSelection();
        QCOMPARE(f->rowCount(), 10);
        QCOMPARE(v->maximum(), qMax(0, f->contentHeight() - v->pageStep()));
        QVERIFY(f->scrollY() <= v->maximum());

        // grouping and collapsing: a collapsed group hides its rows
        f->selectRange(0, 9);
        QVERIFY(f->groupSelected("G"));
        const int expanded = f->contentHeight();
        f->toggleGroup(0);
        QVERIFY(f->contentHeight() < expanded);
        QCOMPARE(f->verticalScrollBar()->maximum(), qMax(0, f->contentHeight() - v->pageStep()));
        f->toggleGroup(0);
        QCOMPARE(f->contentHeight(), expanded);
    }

    void analogRowsAreTallerAndCounted() {
        Fixture f(3, QSize(900, 300));
        const int plain = f->contentHeight();
        f->selectRow(1);
        f->toggleAnalogForSelection();
        QVERIFY(f->contentHeight() > plain);
        QCOMPARE(f->contentHeight(), plain + 3 * f->rowHeight()); // 4 rows high instead of 1
        // the row below the tall one is found at its real position
        const int y = f->rowTop(2) + 2;
        QTest::mouseClick(f.w.get(), Qt::LeftButton, {}, QPoint(20, y));
        QVERIFY(f->isRowSelected(2));
    }

    void tenThousandRowsStayResponsive() {
        auto st = makeStore();
        auto w = std::make_unique<WaveWidget>();
        w->resize(900, 400);
        DisplayModel m;
        for (int i = 0; i < 10000; ++i)
            m.root().children.push_back(DisplayModel::makeSignal(i % 2 ? "top.clk" : "top.volt"));
        w->setDisplayModel(std::move(m));
        w->setStore(st);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QCOMPARE(w->rowCount(), 10000);
        QScrollBar* v = w->verticalScrollBar();
        QCOMPARE(v->maximum(), 10000 * w->rowHeight() - v->pageStep());
        v->setValue(v->maximum());
        QCOMPARE(w->rowTop(9999) + w->rowHeight(), w->rowsBottom());
        v->setValue(4321 * w->rowHeight());
        QCOMPARE(w->rowTop(4321), w->rowsTop());
        // hit testing is a search, not a scan: many clicks at the far end stay fast
        v->setValue(v->maximum());
        QElapsedTimer clicks;
        clicks.start();
        for (int i = 0; i < 100; ++i) QTest::mouseMove(w.get(), QPoint(20, w->rowsTop() + 3 + (i % 300)));
        QVERIFY2(clicks.elapsed() < 2000, qPrintable(QString("100 moves took %1 ms").arg(clicks.elapsed())));
        // selecting by click at the end still lands on the right row
        v->setValue(v->maximum());
        QTest::mouseClick(w.get(), Qt::LeftButton, {}, QPoint(20, w->rowTop(9990) + 3));
        QVERIFY(w->isRowSelected(9990));
        QElapsedTimer timer;
        timer.start();
        w->grab(); // paint with 10k rows: only the visible ones are drawn
        QVERIFY2(timer.elapsed() < 1500, qPrintable(QString("paint took %1 ms").arg(timer.elapsed())));
    }

    // ---- follow ----
    void followToolbarButton() {
        Fixture f;
        WaveToolBar bar(f.w.get());
        QAction* a = bar.followAction();
        QVERIFY(a != nullptr);
        QCOMPARE(a->objectName(), QString("follow"));
        QVERIFY(a->isCheckable());
        QVERIFY(!a->isChecked());
        QCOMPARE(a->toolTip(), QString("Follow simulation (stay at end)"));
        QVERIFY(!a->icon().isNull());
        QVERIFY(!a->shortcut().isEmpty());
        a->setChecked(true);
        QVERIFY(f->follow());
        f->setFollow(false); // the widget turns it off: the button follows
        QVERIFY(!a->isChecked());
        f->setFollow(true);
        QVERIFY(a->isChecked());
        // a second toolbar on the same widget stays in step too
        WaveToolBar other(f.w.get());
        QVERIFY(other.followAction()->isChecked());
    }

    void followKeepsTheRightEdgeAtTheEndWithTheSameWidth() {
        Fixture f;
        f->zoomBy(4.0);
        const double tpp = f->view().timePerPixel;
        QSignalSpy spy(f.w.get(), &WaveWidget::followChanged);
        f->setFollow(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(qAbs(f->view().endTime() - 1000.0) < tpp * 2); // jumped to the end at once
        for (Time to : {Time(1500), Time(2500), Time(2600)}) {
            f.liveGrow(to);
            QVERIFY2(qAbs(f->view().endTime() - static_cast<double>(to)) <= tpp * 2,
                     qPrintable(QString::number(to)));
            QCOMPARE(f->view().timePerPixel, tpp); // same width
            QVERIFY(f->follow());
            QVERIFY(f->horizontalScrollBar()->maximum() - f->horizontalScrollBar()->value() <= 4); // thumb at the right end
        }
        QCOMPARE(spy.count(), 1);
    }

    void followOffChangesNothing() {
        Fixture f;
        f->setView(100.0, 1.0);
        const ViewState before = f->view();
        f.liveGrow(3000);
        QCOMPARE(f->view().startTime, before.startTime);
        QCOMPARE(f->view().timePerPixel, before.timePerPixel);
        QVERIFY(!f->follow());
    }

    void followWithoutZoomKeepsFittingTheRun() {
        // Follow set before any data exists (the saved setting) and nothing zoomed: the view keeps fitting the run
        WaveWidget w;
        w.resize(900, 320);
        w.setDisplayModel(makeModel());
        w.setFollow(true);
        auto store = makeStore();
        w.setStore(store);
        QVERIFY(w.follow() && w.autoFit());
        grow(*store, 1050, 2000);
        w.dataChanged();
        QVERIFY(w.follow());
        QVERIFY(qAbs(w.view().startTime) < 1e-9);
        QVERIFY(qAbs(w.view().endTime() - 2000.0) < 2.0);
        w.zoomBy(2.0);  // the first zoom fixes a width: from now on it tails
        const double tpp = w.view().timePerPixel;
        grow(*store, 2050, 3000);
        w.dataChanged();
        QCOMPARE(w.view().timePerPixel, tpp);
        QVERIFY(qAbs(w.view().endTime() - 3000.0) <= tpp * 2);
    }

    void everyManualPanTurnsFollowOff() {
        struct Case {
            const char* name;
            std::function<void(Fixture&)> act;
        };
        const std::vector<Case> cases = {
            {"horizontal bar",
             [](Fixture& f) { f->horizontalScrollBar()->setValue(f->horizontalScrollBar()->maximum() / 2); }},
            {"bar page step",
             [](Fixture& f) { f->horizontalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub); }},
            {"shift wheel",
             [](Fixture& f) {
                 wheel(*f.w, QPoint(0, 120), Qt::ShiftModifier, QPoint(f->waveLeft() + 50, f->rowsTop() + 5));
             }},
            {"horizontal wheel",
             [](Fixture& f) {
                 wheel(*f.w, QPoint(120, 0), {}, QPoint(f->waveLeft() + 50, f->rowsTop() + 5));
             }},
            {"ctrl left", [](Fixture& f) { QTest::keyClick(f.w.get(), Qt::Key_Left, Qt::ControlModifier); }},
            {"middle drag",
             [](Fixture& f) {
                 const QPoint p(f->waveLeft() + 200, f->rowsTop() + 10);
                 QTest::mousePress(f.w.get(), Qt::MiddleButton, {}, p);
                 QTest::mouseMove(f.w.get(), p + QPoint(-60, 0));
                 QTest::mouseRelease(f.w.get(), Qt::MiddleButton, {}, p + QPoint(-60, 0));
             }},
            {"home", [](Fixture& f) { QTest::keyClick(f.w.get(), Qt::Key_Home); }},
            {"end", [](Fixture& f) { QTest::keyClick(f.w.get(), Qt::Key_End); }},
            {"go to time", [](Fixture& f) { f->goToTime(300.0); }},
            {"zoom to fit", [](Fixture& f) { f->zoomFull(); }},
            {"set view", [](Fixture& f) { f->setView(0.0, 1.0); }},
            {"jump to edge off screen", [](Fixture& f) {
                 f->setCursorTime(0, 100.0);
                 f->jumpToEdge(-1); // the previous transition is far left of the followed window
             }},
        };
        for (const Case& c : cases) {
            Fixture f;
            f->show();
            QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
            f->zoomBy(4.0);
            f->setFollow(true);
            QVERIFY2(f->follow(), c.name);
            QSignalSpy spy(f.w.get(), &WaveWidget::followChanged);
            c.act(f);
            QVERIFY2(!f->follow(), c.name);
            QVERIFY2(spy.count() >= 1 && spy.last().first().toBool() == false, c.name);
        }
    }

    void zoomKeepsFollowAndPinsTheEnd() {
        Fixture f;
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        f->zoomBy(2.0);
        f->setFollow(true);
        const double tpp = f->view().timePerPixel;

        f->zoomBy(2.0);
        QVERIFY(f->follow());
        QVERIFY(f->view().timePerPixel < tpp);
        QVERIFY(qAbs(f->view().endTime() - 1000.0) < f->view().timePerPixel * 2);

        // wheel zoom around a pointer in the middle of the area: the end stays at the right edge
        wheel(*f.w, QPoint(0, 120), Qt::ControlModifier,
              QPoint(f->waveLeft() + f->waveWidth() / 2, f->rowsTop() + 10));
        QVERIFY(f->follow());
        QVERIFY(qAbs(f->view().endTime() - 1000.0) < f->view().timePerPixel * 2);
        wheel(*f.w, QPoint(0, -120 * 3), Qt::ControlModifier, QPoint(f->waveLeft() + 10, f->rowsTop() + 10));
        QVERIFY(f->follow());
        QVERIFY(qAbs(f->view().endTime() - 1000.0) < f->view().timePerPixel * 2);
    }

    void resizeWhileFollowingKeepsTheEnd() {
        Fixture f;
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        f->zoomBy(4.0);
        f->setFollow(true);
        f->resize(1300, 320);
        QTest::qWait(20);
        QVERIFY(f->follow());
        QVERIFY(qAbs(f->view().endTime() - 1000.0) < f->view().timePerPixel * 2);
    }

    void followSurvivesANewStoreAndRowEdits() {
        Fixture f;
        f->zoomBy(4.0);
        f->setFollow(true);
        f->selectRow(0);
        f->toggleAnalogForSelection(); // edits do not turn it off
        QVERIFY(f->follow());
        f->setStore(makeStore(2000));
        QVERIFY(f->follow());
    }
};

QTEST_MAIN(TestWaveScroll)
#include "test_wave_scroll.moc"
