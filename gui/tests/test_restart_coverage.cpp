// SPDX-License-Identifier: GPL-3.0-or-later
// With the real nvc: Restart in the running process (`restart -json`, the R record of the live stream), a new
// process when a library was analysed again since the start, and Simulation > Refresh coverage
// (`coverage save -json`) during a session.
#include "app/coverage_pane.hpp"
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"
#include "wave/wave_widget.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <coverage.hpp>

using namespace lvd::gui;
using lvd::wave::Time;

namespace {

const char* kCounter = R"(
entity t is end;
architecture a of t is
  signal clk : bit := '0';
  signal cnt : integer := 0;
begin
  clk <= not clk after 5 ns;
  process (clk) begin
    if clk = '1' then
      if cnt < 1000 then cnt <= cnt + 1; end if;
      report "tick" severity note;
    end if;
  end process;
end;
)";

#define PREPARE(d)                                                                                           \
    do {                                                                                                     \
        if (!(d).nvc.found)                                                                                  \
            QSKIP("nvc not found (set LVDSIM_NVC)");                                                         \
        QVERIFY((d).prepare(kCounter));                                                                      \
    } while (0)

std::size_t changes(const SimWindow& w, const char* path) {
    return w.store()->signal(w.store()->findSignal(path)).changeCount();
}

} // namespace

class TestRestartCoverage : public QObject {
    Q_OBJECT
private slots:
    void restartInTheRunningProcess() {
        testing::AnalysedDesign d;
        PREPARE(d);
        SimWindow w(d.environment(), d.file("settings.ini"));
        w.setDialogsEnabled(false);
        QSignalSpy started(&w, &SimWindow::simulationStarted);
        QSignalSpy restarted(&w, &SimWindow::restarted);
        QVERIFY(w.startSimulation({"-do", "add wave -r /*; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{100000000});
        const std::size_t clkFirst = changes(w, "t.clk");
        const std::size_t cntFirst = changes(w, "t.cnt");
        QVERIFY(clkFirst > 10);
        w.wave()->addCursor(50e6);
        QCOMPARE(w.wave()->cursorCount(), 1);
        const int rows = w.wave()->rowCount();

        w.restartSimulation();
        QTRY_COMPARE_WITH_TIMEOUT(restarted.count(), 1, 20000);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(started.count(), 1); // the same process
        QCOMPARE(w.store()->endTime(), Time{0});
        QVERIFY(changes(w, "t.clk") <= 1); // only the value at time zero
        QCOMPARE(w.wave()->rowCount(), rows);
        QCOMPARE(w.wave()->cursorCount(), 0); // a new view of the store
        QVERIFY(w.transcript()->text().contains("Restarted at time 0"));
        QCOMPARE(w.stopText(), QString("Restarted at 0 fs"));

        w.sendCommand("run 100 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{100000000});
        QCOMPARE(changes(w, "t.clk"), clkFirst); // the second run matches the first
        QCOMPARE(changes(w, "t.cnt"), cntFirst);
        QVERIFY(w.wave()->rowHasSource(0));
    }

    void restartAfterAnalysisStartsANewProcess() {
        testing::AnalysedDesign d;
        PREPARE(d);
        SimWindow w(d.environment(), d.file("settings.ini"));
        w.setDialogsEnabled(false);
        QSignalSpy started(&w, &SimWindow::simulationStarted);
        QVERIFY(w.startSimulation({"-do", "run 20 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.librariesChangedSinceStart());
        QThread::msleep(30);
        QVERIFY(d.prepare(kCounter)); // analysed again: the kernel would keep the old design
        QVERIFY(w.librariesChangedSinceStart());
        w.restartSimulation();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(started.count(), 2);
        QVERIFY(w.transcript()->text().contains("new simulator process"));
    }

    void refreshCoverageDuringTheSession() {
        testing::AnalysedDesign d;
        PREPARE(d);
        SimWindow w(d.environment(), d.file("settings.ini"));
        w.setDialogsEnabled(false);
        w.setCoverageEnabled(true);
        QVERIFY(w.startSimulation({"-do", "run 200 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.sessionCoverageFile().isEmpty());

        QSignalSpy loaded(&w, &SimWindow::coverageLoaded);
        QVERIFY(w.refreshCoverage());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 30000);
        QVERIFY(w.coverage()->database() != nullptr);
        const auto first = w.coverage()->database()->totals();
        QVERIFY(first.allTotal() > 0);
        QVERIFY(first.allHit() > 0); // counts of the running simulation, not only the elaboration
        auto hitSum = [&w] {
            std::int64_t n = 0;
            for (const auto& b : w.coverage()->database()->bins)
                n += b.hits;
            return n;
        };
        const std::int64_t sumFirst = hitSum();

        // More simulation, more counts: the statement counts grow.
        w.sendCommand("run 200 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.refreshCoverage());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 30000);
        QVERIFY(hitSum() > sumFirst);
        QVERIFY(w.waitForCoverageTool());
        w.stopSimulation();
    }
};

QTEST_MAIN(TestRestartCoverage)
#include "test_restart_coverage.moc"
