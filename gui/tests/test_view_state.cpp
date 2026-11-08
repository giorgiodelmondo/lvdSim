// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/view_state.hpp"

#include <QtTest>

using namespace lvd::gui;

class TestViewState : public QObject {
    Q_OBJECT
private slots:
    void mapping() {
        ViewState v;
        v.startTime = 1000;
        v.timePerPixel = 10;
        v.widthPx = 500;
        QCOMPARE(v.timeAt(0), 1000.0);
        QCOMPARE(v.timeAt(50), 1500.0);
        QCOMPARE(v.xAt(2000), 100.0);
        QCOMPARE(v.endTime(), 6000.0);
    }

    void zoomKeepsAnchor() {
        ViewState v;
        v.startTime = 1000;
        v.timePerPixel = 10;
        v.widthPx = 500;
        const double before = v.timeAt(200);
        v.zoomAt(200, 2.0);
        QCOMPARE(v.timePerPixel, 5.0);
        QVERIFY(qFuzzyCompare(v.timeAt(200), before));
        v.zoomAt(0, 0.5);
        QCOMPARE(v.timePerPixel, 10.0);
        v.zoomAt(0, -1.0);  // ignored
        QCOMPARE(v.timePerPixel, 10.0);
    }

    void fitAndDegenerate() {
        ViewState v;
        v.widthPx = 400;
        v.fit(100, 500);
        QCOMPARE(v.startTime, 100.0);
        QCOMPARE(v.timePerPixel, 1.0);
        v.fit(300, 300);  // degenerate range is widened
        QVERIFY(v.timePerPixel > 0.0);
        QVERIFY(v.endTime() > v.startTime);
    }

    void panAndClamp() {
        ViewState v;
        v.startTime = 100;
        v.timePerPixel = 2;
        v.panPixels(-1000);
        QCOMPARE(v.startTime, 0.0);  // never before zero
        v.panPixels(10);
        QCOMPARE(v.startTime, 20.0);
        v.startTime = 5000;
        v.clamp(1000);
        QCOMPARE(v.startTime, 1000.0);
        v.timePerPixel = 1e-9;
        v.clamp(1000);
        QVERIFY(v.timePerPixel >= 1e-3);
    }

    void tickSpacingAndAlignment() {
        ViewState v;
        v.startTime = 3;
        v.timePerPixel = 1;
        v.widthPx = 1000;
        const auto ticks = rulerTicks(v, 80);
        QVERIFY(ticks.size() >= 5);
        for (std::size_t i = 1; i < ticks.size(); ++i) {
            QVERIFY(ticks[i] - ticks[i - 1] >= 80.0);
            QVERIFY(ticks[i] > ticks[i - 1]);
        }
        // step is on a 1-2-5 grid and ticks are multiples of it
        const double step = ticks[1] - ticks[0];
        QCOMPARE(std::fmod(ticks[0], step), 0.0);
        QVERIFY(ticks.front() >= v.startTime);
        QVERIFY(ticks.back() <= v.endTime());
    }

    void tickEdgeCases() {
        ViewState v;
        v.widthPx = 0;
        QVERIFY(rulerTicks(v, 80).empty());
        v.widthPx = 100;
        v.timePerPixel = 0;
        QVERIFY(rulerTicks(v, 80).empty());
    }

    void timeFormatting() {
        QCOMPARE(QString::fromStdString(formatTime(0, -15)), QString("0 fs"));
        QCOMPARE(QString::fromStdString(formatTime(1000, -15)), QString("1 ps"));
        QCOMPARE(QString::fromStdString(formatTime(12500000, -15)), QString("12.5 ns"));
        QCOMPARE(QString::fromStdString(formatTime(999, -15)), QString("999 fs"));
        QCOMPARE(QString::fromStdString(formatTime(1e15, -15)), QString("1 s"));
        QCOMPARE(QString::fromStdString(formatTime(2500, -9)), QString("2.5 us"));  // 2500 ns
        QCOMPARE(QString::fromStdString(formatTime(1234567, -15)), QString("1.235 ns"));
    }

    void parseTimeUnits() {
        auto ms = [](const char* text, int exp) { return parseTime(text, exp); };
        QCOMPARE(*ms("1500 ps", -15), 1500000.0);
        QCOMPARE(*ms("12.5ns", -12), 12500.0);
        QCOMPARE(*ms("  3 us ", -9), 3000.0);
        QCOMPARE(*ms("2", -12), 2000.0);          // no unit: ns by default
        QCOMPARE(*ms("2 NS", -12), 2000.0);       // case insensitive
        QCOMPARE(*ms("100 fs", -15), 100.0);
        QCOMPARE(*ms("1 ms", -15), 1e12);
        QCOMPARE(*ms("0.5 s", -12), 5e11);
        QCOMPARE(*ms("0", -15), 0.0);
        QCOMPARE(*ms("0.4 ps", -15), 400.0);
        QCOMPARE(*parseTime("7", -12, -12), 7.0);  // explicit default unit
    }

    void parseTimeRejectsGarbage() {
        QVERIFY(!parseTime("", -15));
        QVERIFY(!parseTime("   ", -15));
        QVERIFY(!parseTime("abc", -15));
        QVERIFY(!parseTime("-5 ns", -15));
        QVERIFY(!parseTime("5 parsecs", -15));
        QVERIFY(!parseTime("5 ns extra", -15));
        QVERIFY(!parseTime("inf ns", -15));
    }

};

QTEST_APPLESS_MAIN(TestViewState)
#include "test_view_state.moc"
