// SPDX-License-Identifier: GPL-3.0-or-later
// The whole coverage flow of the GUI with the real nvc, as a user goes through it: Simulation > Collect
// coverage, start (Library > Simulate, Simulation > Start with vsim options), run, Simulation > Refresh
// coverage (dock numbers, Source marks), the end of the simulation, the end of the session (the counts
// written at exit are loaded), Restart in a new process; batch databases opened from another folder; a mixed
// VHDL/Verilog design.
#include "app/coverage_pane.hpp"
#include "app/library_pane.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QSignalSpy>
#include <QTest>

#include <coverage.hpp>

using namespace lvd::gui;
namespace cov = lvd::tools::cov;

namespace {

// A state machine with a branch that is never taken and a state that is never entered. The entity name has
// upper case letters: nvc names its default database after the top as written, the GUI in lower case.
const char* kFsm = R"(library ieee;
use ieee.std_logic_1164.all;
entity FsmTop is
end entity;
architecture rtl of FsmTop is
  type state_t is (IDLE, RUN, DONE, NEVER);
  signal state : state_t := IDLE;
  signal clk : std_logic := '0';
  signal cnt : integer range 0 to 15 := 0;
  signal vec : std_logic_vector(3 downto 0) := (others => '0');
begin
  clk <= not clk after 5 ns when now < 300 ns;
  process (clk)
  begin
    if rising_edge(clk) then
      vec(cnt mod 4) <= '1';
      case state is
        when IDLE => state <= RUN;
        when RUN =>
          if cnt = 5 then
            state <= DONE;
          else
            cnt <= cnt + 1;
          end if;
        when DONE => null;
        when NEVER => state <= IDLE;
      end case;
    end if;
  end process;
end architecture;
)";
constexpr int kNeverLine = 26; // when NEVER => ...
constexpr int kIdleLine = 18;  // when IDLE => ...

std::int64_t hitSum(const cov::Database* db) {
    std::int64_t n = 0;
    if (db != nullptr)
        for (const cov::Bin& b : db->bins)
            n += b.hits;
    return n;
}

QString tail(const SimWindow& w) {
    return w.transcript()->text().right(1200);
}

bool runNvc(const testing::AnalysedDesign& d, const std::vector<std::string>& args) {
    std::vector<std::string> full = {d.nvc.exe.toStdString(), "-L", d.nvc.libDir.toStdString()};
    full.insert(full.end(), args.begin(), args.end());
    lvd::tools::ProcessOptions po;
    po.capture = true;
    po.env = {{"NVC_MAX_THREADS", "1"}};
    po.workingDir = d.dir.path().toStdString();
    if (!d.nvc.dllDir.isEmpty())
        po.pathPrepend.push_back(d.nvc.dllDir.toStdString());
    const auto r = lvd::tools::runProcess(full, po);
    if (r.exitCode != 0)
        qWarning("%s", r.output.c_str());
    return r.started && r.exitCode == 0;
}

} // namespace

class TestCoverageFlow : public QObject {
    Q_OBJECT
private slots:
    void collectRefreshEndAndRestart() {
        testing::AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY(d.prepare(kFsm, "fsm.vhd"));
        SimWindow w(d.environment(), d.file("flow.ini"));
        w.setDialogsEnabled(false);
        w.show();
        w.findChild<QAction*>("collectCoverage")->setChecked(true);
        // Library > Simulate on the unit.
        emit w.library()->simulateRequested(QStringLiteral("work.FsmTop"));
        QVERIFY(w.simulationRunning());
        QVERIFY(w.waitUntilIdle());
        // An absolute database name for nvc: replies and a later change of the working directory agree on it.
        QCOMPARE(w.sessionCoverageFile(), d.file("fsmtop.ncdb"));
        QVERIFY(w.sessionArguments().contains("--cover-file=" + d.file("fsmtop.ncdb")));
        w.sendCommand("run 100 ns");
        QVERIFY(w.waitUntilIdle());

        // Refresh: the counts so far in the dock and in the Source gutter.
        QSignalSpy loaded(&w, &SimWindow::coverageLoaded);
        QVERIFY(w.refreshCoverage());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 30000);
        QVERIFY(w.waitForCoverageTool());
        QVERIFY(w.coverage()->database() != nullptr);
        QVERIFY2(w.coverage()->database()->totals().allHit() > 0, qPrintable(tail(w)));
        QCOMPARE(w.coverage()->selectedInstancePath(), QString("/fsmtop"));
        QVERIFY(w.findChild<QDockWidget*>("coverageDock")->isVisible());
        QVERIFY(w.coverage()->summary()->text().contains("Total"));
        QCOMPARE(QFileInfo(w.source()->filePath()).fileName(), QString("fsm.vhd"));
        QVERIFY(w.source()->hasCoverage());
        QVERIFY(w.source()->view()->coverageMarks().value(kNeverLine).uncovered());
        QVERIFY(w.source()->view()->coverageMarks().value(kIdleLine).covered());
        const std::int64_t first = hitSum(w.coverage()->database());

        // A project opened during the session changes the working directory: the refresh still finds the
        // file.
        const QString elsewhere = d.file("elsewhere");
        QVERIFY(QDir().mkpath(elsewhere));
        QDir::setCurrent(elsewhere);
        // The end of the simulation (no more events) loads the counts by itself.
        w.sendCommand("run -all");
        QVERIFY(w.waitUntilIdle());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 30000);
        QVERIFY(w.waitForCoverageTool());
        QVERIFY2(hitSum(w.coverage()->database()) > first, qPrintable(tail(w)));
        QDir::setCurrent(d.dir.path());

        // The end of the session: the simulator writes the counts at exit and the dock shows them.
        w.stopSimulation();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 30000);
        QVERIFY(w.waitForCoverageTool());
        QVERIFY2(!w.transcript()->text().contains("may have ended without"), qPrintable(tail(w)));
        QVERIFY(w.source()->hasCoverage());
        const std::int64_t ended = hitSum(w.coverage()->database());
        QVERIFY(ended > first);

        // Restart in a new process (the session ended): a new elaboration writes fsmtop.ncdb again. The
        // databases behind the dock are a copy now, so merge and report still give the counts on view.
        w.restartSimulation();
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.simulationRunning());
        QVERIFY(w.waitForCoverageTool());
        QCOMPARE(hitSum(w.coverage()->database()), ended);
        QCOMPARE(w.coverageDatabases().size(), 1);
        QVERIFY(w.coverageDatabases().front() != w.sessionCoverageFile());
        QVERIFY(w.saveMergedCoverage(d.file("kept.ncdb")));
        QVERIFY(w.waitForCoverageTool());
        QVERIFY(w.exportCoverageReport(d.file("html")));
        QVERIFY(w.waitForCoverageTool());
        QVERIFY(QFileInfo::exists(d.file("html/index.html")));
        QVERIFY(w.openCoverage({d.file("kept.ncdb")}));
        QVERIFY(w.waitForCoverageTool());
        QCOMPARE(hitSum(w.coverage()->database()), ended);
        // The Source marks still find fsm.vhd (relative in the database, next to kept.ncdb).
        QVERIFY(w.coverage()->selectInstance("/fsmtop"));
        QVERIFY(w.source()->view()->coverageMarks().value(kNeverLine).uncovered());

        // The new session counts from zero.
        w.sendCommand("run 50 ns");
        QVERIFY(w.waitUntilIdle());
        const int before = static_cast<int>(loaded.count());
        QVERIFY(w.refreshCoverage());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), before + 1, 30000);
        QVERIFY(w.waitForCoverageTool());
        const std::int64_t fresh = hitSum(w.coverage()->database());
        QVERIFY(fresh > 0 && fresh < ended);
        w.stopSimulation();
        QVERIFY(w.waitForCoverageTool());
    }

    void startOptionsWithSpaces() {
        // Simulation > Start with vsim options: a relative -lvdcoverfile in a folder with a space, and a
        // refresh that keeps the instance selected in the dock.
        testing::AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY(d.prepare(kFsm, "fsm.vhd"));
        QVERIFY(QDir().mkpath(d.file("cov dir")));
        SimWindow w(d.environment(), d.file("flow.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(!w.coverageEnabled());
        QVERIFY(w.startSimulation({"-coverage", "-lvdcoverfile", "cov dir/my run.ncdb", "work.FsmTop"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.sessionCoverageFile(), d.file("cov dir/my run.ncdb"));
        w.sendCommand("run 120 ns");
        QVERIFY(w.waitUntilIdle());
        QSignalSpy loaded(&w, &SimWindow::coverageLoaded);
        QVERIFY(w.refreshCoverage());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 30000);
        QVERIFY(w.waitForCoverageTool());
        QVERIFY2(QFileInfo::exists(d.file("cov dir/my run.ncdb")), qPrintable(tail(w)));
        QVERIFY(w.coverage()->database()->totals().allHit() > 0);
        w.sendCommand("run 20 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.refreshCoverage());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 30000);
        QCOMPARE(w.coverage()->selectedInstancePath(), QString("/fsmtop"));
        w.stopSimulation();
        QVERIFY(w.waitForCoverageTool());
    }

    void batchDatabaseFromAnotherFolder() {
        // A batch run in its own folder (as `vsim -c -coverage -do "run -all; quit"` does it): nvc keeps the
        // relative source name. Opened from a window whose working directory is elsewhere, the Source pane
        // still finds and annotates the file.
        testing::AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY(d.prepare(kFsm, "fsm.vhd"));
        QVERIFY(runNvc(d, {"--std=08", "--work=work:work", "-e", "--cover=all", "--cover-file=batch.ncdb",
                           "FsmTop", "-r"}));
        QVERIFY(QDir().mkpath(d.file("other")));
        QDir::setCurrent(d.file("other"));
        SimWindow w(d.environment(), d.file("flow.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.openCoverage({d.file("batch.ncdb")}));
        QVERIFY(w.waitForCoverageTool());
        QVERIFY(w.coverage()->database() != nullptr);
        QCOMPARE(QString::fromStdString(w.coverage()->database()->files.front()), QString("fsm.vhd"));
        QCOMPARE(QDir::cleanPath(w.source()->filePath()), QDir::cleanPath(d.file("fsm.vhd")));
        QVERIFY(w.source()->hasCoverage());
        QVERIFY(w.source()->view()->coverageMarks().value(kNeverLine).uncovered());
    }

    void mixedLanguageDesign() {
        // nvc instruments the VHDL part of a mixed design; the Verilog instance has no coverage scope.
        testing::AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY(d.prepare("module vcount(input clk, output reg [3:0] q);\n"
                          "  initial q = 0;\n  always @(posedge clk) q <= q + 1;\nendmodule\n",
                          "vcount.v"));
        std::ofstream(d.file("mixed.vhd").toStdString())
            << "library ieee;\nuse ieee.std_logic_1164.all;\n"
               "entity MixTop is end;\n"
               "architecture a of MixTop is\n"
               "  component vcount is\n"
               "    port (clk : in std_logic; q : out std_logic_vector(3 downto 0));\n"
               "  end component;\n"
               "  signal clk : std_logic := '0';\n  signal q : std_logic_vector(3 downto 0);\n"
               "begin\n"
               "  clk <= not clk after 5 ns when now < 100 ns;\n"
               "  u : vcount port map (clk, q);\n"
               "  process (clk) begin\n    if clk = '1' then null; end if;\n"
               "  end process;\nend;\n";
        QVERIFY(runNvc(d, {"--std=08", "--work=work:work", "-a", "mixed.vhd"}));
        SimWindow w(d.environment(), d.file("flow.ini"));
        w.setDialogsEnabled(false);
        w.setCoverageEnabled(true);
        QVERIFY(w.startSimulation({"work.MixTop"}));
        QVERIFY(w.waitUntilIdle());
        QSignalSpy loaded(&w, &SimWindow::coverageLoaded);
        w.sendCommand("run -all");
        QVERIFY(w.waitUntilIdle());
        QTRY_VERIFY2_WITH_TIMEOUT(loaded.count() == 1, qPrintable(tail(w)),
                                  30000); // the end of the simulation
        QVERIFY(w.waitForCoverageTool());
        const cov::Database* db = w.coverage()->database();
        QVERIFY2(db != nullptr, qPrintable(tail(w)));
        QVERIFY(db->totals().allHit() > 0);
        QVERIFY(db->findScope("/mixtop") >= 0);
        QCOMPARE(w.coverage()->visibleInstancePaths(), QStringList{"/mixtop"});
        w.stopSimulation();
        QVERIFY(w.waitForCoverageTool());
    }
};

QTEST_MAIN(TestCoverageFlow)
#include "test_coverage_flow.moc"
