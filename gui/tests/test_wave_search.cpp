// SPDX-License-Identifier: GPL-3.0-or-later
// Wave window navigation: next/previous edge (any, rising, falling) by keys and toolbar, and the Find Value
// dialog.
#include "wave/find_value_dialog.hpp"
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

// clk: 0 at 0, toggles every 10 up to 200. cnt (4 bit): 0 at 0, 1 at 15, 1 again at 25 (no edge), 2 at 35, X
// at 55, A at 75, 0 at 95. volt (real): a triangle 0..10..0 over 0..200 in steps of 10.
std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(top, "cnt", SignalKind::Logic, 4);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 1);
    for (Time t = 0; t <= 200; t += 10) {
        st->mutableSignal(clk).appendLogic(t, (t / 10) % 2 ? "1" : "0");
        const double v = t <= 100 ? static_cast<double>(t) / 10.0 : static_cast<double>(200 - t) / 10.0;
        st->mutableSignal(volt).appendReal(t, v);
    }
    Signal& c = st->mutableSignal(cnt);
    c.appendLogic(0, "0000");
    c.appendLogic(15, "0001");
    c.appendLogic(25, "0001");
    c.appendLogic(35, "0010");
    c.appendLogic(55, "XXXX");
    c.appendLogic(75, "1010");
    c.appendLogic(95, "0000");
    st->extendTimeRange(0);
    st->extendTimeRange(200);
    return st;
}

DisplayModel makeModel() {
    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
    m.root().children.push_back(DisplayModel::makeSignal("top.cnt", Radix::Hex));
    DisplayItem volt = DisplayModel::makeSignal("top.volt");
    volt.analog.enabled = true;
    m.root().children.push_back(volt);
    m.root().children.push_back(
        DisplayModel::makeBus("pair", {BusMember("top.clk"), BusMember("top.cnt", 0)}));
    return m;
}

struct Fixture {
    QWidget window;
    WaveWidget* w = nullptr;
    WaveToolBar* bar = nullptr;
    Fixture() {
        auto* lay = new QVBoxLayout(&window);
        w = new WaveWidget;
        bar = new WaveToolBar(w);
        lay->addWidget(bar);
        lay->addWidget(w);
        window.resize(1000, 400);
        w->setDisplayModel(makeModel());
        w->setStore(makeStore());
        w->setView(0.0, 0.25); // 0 .. ~200 visible
    }
    int row(const char* name) const {
        for (int i = 0; i < w->rowCount(); ++i)
            if (w->rowName(i) == name)
                return i;
        return -1;
    }
    double cursor() const { return w->cursorTime(w->activeCursor()).value_or(-1.0); }
    QAction* action(const char* name) const { return bar->findChild<QAction*>(name); }
};

} // namespace

class TestWaveSearch : public QObject {
    Q_OBJECT
private slots:
    void tabAndShiftTabStepThroughEdgesOfTheSelection() {
        Fixture f;
        f.w->selectRow(f.row("top.cnt"));
        f.w->setCursorTime(0, 0.0);
        QTest::keyClick(f.w, Qt::Key_Tab);
        QCOMPARE(f.cursor(), 15.0);
        QTest::keyClick(f.w, Qt::Key_Tab); // 25 repeats the value: not an edge
        QCOMPARE(f.cursor(), 35.0);
        QTest::keyClick(f.w, Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(f.cursor(), 15.0);
        QTest::keyClick(f.w, Qt::Key_Right);
        QCOMPARE(f.cursor(), 35.0);
        QTest::keyClick(f.w, Qt::Key_Left);
        QCOMPARE(f.cursor(), 15.0);
        // A cursor between two times: the edge at 15 is "before" a cursor at 15.4.
        f.w->setCursorTime(0, 15.4);
        QVERIFY(f.w->jumpToEdge(-1));
        QCOMPARE(f.cursor(), 15.0);
    }

    void risingAndFallingFromTheToolbar() {
        Fixture f;
        f.w->selectRow(f.row("top.clk"));
        f.w->setCursorTime(0, 0.0);
        auto* button = f.bar->findChild<QToolButton*>("edgeKind");
        QVERIFY(button != nullptr);
        QVERIFY(button->menu() != nullptr);
        QSignalSpy kind(f.w, &WaveWidget::edgeKindChanged);
        button->menu()->findChild<QAction*>("edgeRising")->trigger();
        QCOMPARE(f.w->edgeKind(), EdgeKind::Rising);
        QCOMPARE(kind.count(), 1);
        QCOMPARE(button->text(), QString("Rising edge"));
        f.action("nextEdge")->trigger();
        QCOMPARE(f.cursor(), 10.0);
        f.action("nextEdge")->trigger();
        QCOMPARE(f.cursor(), 30.0);
        f.action("prevEdge")->trigger();
        QCOMPARE(f.cursor(), 10.0);
        f.w->setEdgeKind(EdgeKind::Falling); // the button follows the widget
        QCOMPARE(button->text(), QString("Falling edge"));
        QVERIFY(button->menu()->findChild<QAction*>("edgeFalling")->isChecked());
        QTest::keyClick(f.w, Qt::Key_Tab);
        QCOMPARE(f.cursor(), 20.0);
    }

    void risingNeedsASingleBitRow() {
        Fixture f;
        f.w->selectRow(f.row("top.cnt"));
        f.w->setCursorTime(0, 0.0);
        QSignalSpy msg(f.w, &WaveWidget::navigationMessage);
        QVERIFY(!f.w->jumpToEdge(+1, EdgeKind::Rising));
        QCOMPARE(f.cursor(), 0.0);
        QVERIFY(msg.last().at(0).toString().contains("single-bit"));
        // A bit of the expanded vector is a single bit: bit 1 rises at 35.
        f.w->toggleBits(f.row("top.cnt"));
        f.w->selectRow(f.row("top.cnt[1]"));
        QVERIFY(f.w->jumpToEdge(+1, EdgeKind::Rising));
        QCOMPARE(f.cursor(), 35.0);
        QVERIFY(!f.w->jumpToEdge(+1, EdgeKind::Rising) || f.cursor() > 35.0);
    }

    void withoutSelectionAllRowsCountAndNothingAfterTheEnd() {
        Fixture f;
        f.w->clearSelection();
        f.w->setCursorTime(0, 190.0);
        QVERIFY(f.w->jumpToEdge(+1)); // clk at 200
        QCOMPARE(f.cursor(), 200.0);
        QSignalSpy msg(f.w, &WaveWidget::navigationMessage);
        QVERIFY(!f.w->jumpToEdge(+1));
        QVERIFY(msg.last().at(0).toString().startsWith("No transition after"));
    }

    void virtualBusEdges() {
        Fixture f;
        f.w->selectRow(f.row("pair"));
        f.w->setCursorTime(0, 12.0);
        QVERIFY(f.w->jumpToEdge(+1));
        QCOMPARE(f.cursor(), 15.0); // cnt bit 0 rises at 15 while clk is 1
    }

    void aJumpOutOfViewTurnsFollowOff() {
        Fixture f;
        f.w->setView(150.0, 0.05); // 150 .. 190 visible
        f.w->setFollow(true);
        QVERIFY(f.w->follow());
        f.w->selectRow(f.row("top.cnt"));
        f.w->setCursorTime(0, 100.0);
        QVERIFY(f.w->jumpToEdge(-1)); // 95: outside the followed window
        QCOMPARE(f.cursor(), 95.0);
        QVERIFY(!f.w->follow());
        QVERIFY(f.w->view().startTime <= 95.0 && f.w->view().endTime() >= 95.0);
    }

    void findValueInTheRowRadix() {
        Fixture f;
        f.w->selectRow(f.row("top.cnt"));
        f.w->setCursorTime(0, 0.0);
        WaveWidget::FindSpec s;
        s.value = "a";
        auto r = f.w->findValue(s, +1);
        QVERIFY(r.found);
        QCOMPARE(r.time, 75.0);
        QCOMPARE(f.cursor(), 75.0);
        QCOMPARE(r.row, f.row("top.cnt"));
        QVERIFY(r.message.contains("Found at"));
        // X in hex: the XXXX interval.
        s.value = "x";
        QCOMPARE(f.w->findValue(s, -1).time, 55.0);
        // Not equal to 0: becomes true at 15 (from the start)
        s.condition = SearchCondition::NotEquals;
        s.value = "0";
        f.w->setCursorTime(0, 0.0);
        QCOMPARE(f.w->findValue(s, +1).time, 15.0);
        // Nothing after the last match, then wrap.
        s.condition = SearchCondition::Equals;
        s.value = "2";
        f.w->setCursorTime(0, 100.0);
        r = f.w->findValue(s, +1, false);
        QVERIFY(!r.found);
        QCOMPARE(f.cursor(), 100.0);
        QVERIFY(r.message.startsWith("Not found after"));
        r = f.w->findValue(s, +1, true);
        QVERIFY(r.found);
        QVERIFY(r.wrapped);
        QCOMPARE(r.time, 35.0);
        // F3 repeats (with the last wrap setting), Shift+F3 backwards.
        f.w->setCursorTime(0, 100.0);
        QTest::keyClick(f.w, Qt::Key_F3);
        QCOMPARE(f.cursor(), 35.0);
        // A value that does not fit is reported, nothing moves.
        s.value = "1F";
        r = f.w->findValue(s, +1);
        QVERIFY(!r.found);
        QVERIFY(r.message.contains("more bits"));
    }

    void findNeedsASelectionAndThresholdsNeedNumbers() {
        Fixture f;
        f.w->clearSelection();
        WaveWidget::FindSpec s;
        s.value = "1";
        QVERIFY(f.w->findValue(s, +1).message.contains("Select"));
        f.w->selectRow(f.row("top.volt"));
        f.w->selectRow(f.row("pair"), true);
        f.w->setCursorTime(0, 0.0);
        s.condition = SearchCondition::CrossRising;
        s.value = "4.5";
        auto r = f.w->findValue(s, +1);
        QVERIFY(r.found);
        QCOMPARE(r.time, 50.0);
        QVERIFY(r.message.contains("skipped pair"));
        s.condition = SearchCondition::CrossFalling;
        QCOMPARE(f.w->findValue(s, +1).time, 160.0); // 5 at 150, 4 at 160
        s.condition = SearchCondition::CrossEither;
        QCOMPARE(f.w->findValue(s, -1).time, 50.0);
        s.value = "abc";
        QVERIFY(f.w->findValue(s, +1).message.contains("not a number"));
    }

    void theFindDialog() {
        Fixture f;
        f.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.window));
        f.w->selectRow(f.row("top.cnt"));
        f.w->setCursorTime(0, 0.0);

        QAction* find = f.action("findValue");
        QVERIFY(find != nullptr);
        QCOMPARE(find->shortcut(), QKeySequence(QKeySequence::Find));
        QVERIFY(f.w->actions().contains(find)); // Ctrl+F works while the wave window has the focus
        find->trigger();
        FindValueDialog* d = f.w->findDialog();
        QVERIFY(d != nullptr);
        QVERIFY(d->isVisible());
        QCOMPARE(d->signalsLabel()->text(), QString("top.cnt"));
        QVERIFY(!d->wrapBox()->isChecked()); // wrap is off by default
        QVERIFY(d->valueEdit()->placeholderText().contains("hex"));

        d->valueEdit()->setText("2");
        QTest::mouseClick(d->findChild<QPushButton*>("findNext"), Qt::LeftButton);
        QCOMPARE(f.cursor(), 35.0);
        QVERIFY(d->statusLabel()->text().contains("Found at 35"));
        QTest::mouseClick(d->findChild<QPushButton*>("findNext"), Qt::LeftButton);
        QCOMPARE(f.cursor(), 35.0);
        QVERIFY(d->statusLabel()->text().startsWith("Not found"));
        d->wrapBox()->setChecked(true);
        QTest::mouseClick(d->findChild<QPushButton*>("findNext"), Qt::LeftButton);
        QVERIFY(d->statusLabel()->text().contains("wrapped"));
        QTest::mouseClick(d->findChild<QPushButton*>("findPrevious"), Qt::LeftButton);
        QCOMPARE(f.cursor(), 35.0); // wrapped backwards from 35 to the last (and only) 2

        // Threshold conditions relabel the field; the selection label follows the wave window.
        f.w->selectRow(f.row("top.volt"));
        QCOMPARE(d->signalsLabel()->text(), QString("top.volt"));
        d->conditionBox()->setCurrentIndex(
            d->conditionBox()->findData(static_cast<int>(SearchCondition::CrossRising)));
        QCOMPARE(d->findChild<QLabel*>("valueLabel")->text(), QString("&Threshold:"));
        d->valueEdit()->setText("7");
        f.w->setCursorTime(0, 0.0);
        QCOMPARE(d->find(+1).time, 70.0);
        // The same dialog is reused.
        QCOMPARE(f.w->showFindDialog(), d);
    }
};

QTEST_MAIN(TestWaveSearch)
#include "test_wave_search.moc"
