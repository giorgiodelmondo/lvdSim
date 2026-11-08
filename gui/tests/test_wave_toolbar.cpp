// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QLineEdit>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    st->setTimescaleExponent(-15);
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    for (Time t = 0; t <= 1000; t += 100) st->mutableSignal(clk).appendLogic(t, (t / 100) % 2 ? "1" : "0");
    st->extendTimeRange(0);
    st->extendTimeRange(1000);
    return st;
}

}  // namespace

class TestWaveToolBar : public QObject {
    Q_OBJECT

    struct Fixture {
        WaveWidget wave;
        WaveToolBar bar{&wave};
        Fixture() {
            wave.resize(900, 300);
            DisplayModel m;
            m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
            wave.setDisplayModel(m);
            wave.setStore(makeStore());
        }
        QAction* action(const char* name) { return bar.findChild<QAction*>(name); }
    };

private slots:
    void allActionsExist() {
        Fixture f;
        for (const char* n : {"goStart", "prevEdge", "nextEdge", "goEnd", "goTo", "zoomIn", "zoomOut", "zoomFit",
                              "addCursor", "removeCursor", "addDivider"}) {
            QVERIFY2(f.action(n) != nullptr, n);
            QVERIFY(!f.action(n)->toolTip().isEmpty());
        }
        QVERIFY(f.bar.timeEdit() != nullptr);
    }

    void startEndAndTransitionButtons() {
        Fixture f;
        f.action("goEnd")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 1000.0);
        f.action("prevEdge")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 900.0);
        f.action("prevEdge")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 800.0);
        f.action("nextEdge")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 900.0);
        f.action("goStart")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 0.0);
        f.action("nextEdge")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 100.0);
    }

    void goToEnteredTime() {
        Fixture f;
        QVERIFY(f.bar.goToText("0.4 ps"));  // 400 fs
        QCOMPARE(*f.wave.cursorTime(0), 400.0);
        QVERIFY(f.bar.timeEdit()->styleSheet().isEmpty());

        f.bar.timeEdit()->setText("700 fs");
        QTest::keyClick(f.bar.timeEdit(), Qt::Key_Return);
        QCOMPARE(*f.wave.cursorTime(0), 700.0);

        f.bar.timeEdit()->setText("200 fs");
        f.action("goTo")->trigger();
        QCOMPARE(*f.wave.cursorTime(0), 200.0);
        QCOMPARE(f.wave.cursorCount(), 1);  // moves the active cursor
    }

    void badTimeIsRejectedAndMarked() {
        Fixture f;
        QVERIFY(f.bar.goToText("300 fs"));
        QVERIFY(!f.bar.goToText("soon"));
        QVERIFY(!f.bar.timeEdit()->styleSheet().isEmpty());
        QCOMPARE(*f.wave.cursorTime(0), 300.0);  // unchanged
        QVERIFY(f.bar.goToText("500 fs"));       // a valid entry clears the mark
        QVERIFY(f.bar.timeEdit()->styleSheet().isEmpty());
    }

    void zoomAndCursorButtons() {
        Fixture f;
        const double tpp = f.wave.view().timePerPixel;
        f.action("zoomIn")->trigger();
        QVERIFY(f.wave.view().timePerPixel < tpp);
        f.action("zoomOut")->trigger();
        QVERIFY(qAbs(f.wave.view().timePerPixel - tpp) < 1e-9);
        f.action("zoomIn")->trigger();
        f.action("zoomFit")->trigger();
        QVERIFY(qAbs(f.wave.view().timePerPixel - tpp) < 1e-9);

        f.action("addCursor")->trigger();
        f.action("addCursor")->trigger();
        QCOMPARE(f.wave.cursorCount(), 2);
        f.action("removeCursor")->trigger();
        QCOMPARE(f.wave.cursorCount(), 1);
    }
};

QTEST_MAIN(TestWaveToolBar)
#include "test_wave_toolbar.moc"
