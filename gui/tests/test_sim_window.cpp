// SPDX-License-Identifier: GPL-3.0-or-later
// End to end: a real nvc simulation, driven through SimWindow, shown in the wave window.
#include "app/breakpoint_pane.hpp"
#include "app/console_input.hpp"
#include "app/library_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/processes_pane.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/transcript_widget.hpp"
#include "wave/list_model.hpp"
#include "wave/list_window.hpp"
#include "wave/render_policy.hpp"
#include "wave/signal_mime.hpp"
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolBar>
#include <QTreeWidget>
#include <QtTest>

#include <environment.hpp>
#include <filesystem>
#include <fstream>
#include <process.hpp>

using namespace lvd::gui;
using namespace lvd::wave;
namespace fs = std::filesystem;

namespace {

const char* kCounter = R"(
entity t is end;
architecture a of t is
  signal clk : bit := '0';
  signal cnt : integer := 0;
begin
  clk <= not clk after 5 ns;
  process (clk) begin if clk = '1' then cnt <= cnt + 1; end if; end process;
end;
)";

// Ports of every mode, VHDL and Verilog. The last event is at 40 ns.
const char* kPorts = R"(
library ieee; use ieee.std_logic_1164.all;
entity sub is
  port (i : in std_logic; o : out std_logic; io : inout std_logic; b : buffer std_logic);
end;
architecture a of sub is
  signal s : std_logic;
begin
  s <= i;
  o <= s;
  b <= not i;
end;
library ieee; use ieee.std_logic_1164.all;
entity t is end;
architecture a of t is
  signal i : std_logic := '0';
  signal o, io, b, vo : std_logic;
  component vsub is port (vi : in std_logic; vout : out std_logic); end component;
begin
  u : entity work.sub port map (i, o, io, b);
  vl : vsub port map (i, vo);
  i <= '1' after 10 ns, '0' after 40 ns;
end;
)";

const char* kPortsVerilog = R"(
module vsub(input vi, output vout);
  wire w;
  assign w = vi;
  assign vout = w;
endmodule
)";

QString envOr(const char* name, const QString& fallback) {
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : QString::fromLocal8Bit(v);
}

struct Nvc {
    QString exe, libDir, dllDir;
    bool found = false;
};

Nvc findNvc() {
    Nvc n;
    const QString build = QStringLiteral(LVD_KERNEL_BUILD);
#ifdef Q_OS_WIN
    n.exe = envOr("LVDSIM_NVC", build + "/bin/nvc.exe");
    n.dllDir =
        envOr("LVDSIM_NVC_DLLDIR", QDir("C:/msys64/mingw64/bin").exists() ? "C:/msys64/mingw64/bin" : "");
#else
    n.exe = envOr("LVDSIM_NVC", build + "/bin/nvc");
    n.dllDir = envOr("LVDSIM_NVC_DLLDIR", "");
#endif
    n.libDir = envOr("LVDSIM_NVC_LIBDIR", build + "/lib");
    n.found = QFileInfo::exists(n.exe);
    return n;
}

// A working directory with the design analysed into a library called work.
struct Design {
    QTemporaryDir dir;
    Nvc nvc = findNvc();
    QString previousCwd = QDir::currentPath();
    const char* vhdl = kCounter;
    const char* verilog = nullptr; // analysed first when set

    bool prepare() {
        if (!nvc.found || !dir.isValid())
            return false;
        QDir::setCurrent(dir.path());
        std::ofstream("t.vhd") << vhdl;
        QDir().mkpath("work");
        std::ofstream("work/_NVC_LIB") << "lvdsim\n";

        lvd::tools::ProcessOptions po;
        po.capture = true;
        if (!nvc.dllDir.isEmpty())
            po.pathPrepend.push_back(nvc.dllDir.toStdString());
        std::vector<std::string> args = {nvc.exe.toStdString(), "-L", nvc.libDir.toStdString(), "--std=08",
                                         "--work=work:work",    "-a"};
        if (verilog != nullptr) {
            std::ofstream("v.v") << verilog;
            args.push_back("v.v");
        }
        args.push_back("t.vhd");
        const auto r = lvd::tools::runProcess(args, po);
        return r.started && r.exitCode == 0;
    }
    ~Design() { QDir::setCurrent(previousCwd); }

    lvd::tools::Environment environment() const {
        lvd::tools::Environment e = lvd::tools::discoverEnvironment();
        e.nvc = nvc.exe.toStdString();
        e.nvcDllDir = nvc.dllDir.toStdString();
        e.ctx.nvcGlobalArgs = {"-L", nvc.libDir.toStdString()};
        return e;
    }
};

/// Sets (or, with nullptr, removes) LVDSIM_RENDERER for one test.
struct EnvGuard {
    QByteArray old;
    bool had;
    explicit EnvGuard(const char* value)
        : old(qgetenv("LVDSIM_RENDERER"))
        , had(qEnvironmentVariableIsSet("LVDSIM_RENDERER")) {
        if (value)
            qputenv("LVDSIM_RENDERER", value);
        else
            qunsetenv("LVDSIM_RENDERER");
    }
    ~EnvGuard() {
        if (had)
            qputenv("LVDSIM_RENDERER", old);
        else
            qunsetenv("LVDSIM_RENDERER");
    }
};

#define PREPARE(d)                                                                                           \
    if (!(d).nvc.found)                                                                                      \
        QSKIP("nvc not found (set LVDSIM_NVC)");                                                             \
    QVERIFY2((d).prepare(), "could not analyse the test design")

} // namespace

class TestSimWindow : public QObject {
    Q_OBJECT

    static int rowNamed(const SimWindow& w, const QString& name) {
        for (int i = 0; i < w.wave()->rowCount(); ++i)
            if (QString::fromStdString(w.wave()->rowName(i)) == name)
                return i;
        return -1;
    }

private slots:
    void aSimulationFillsTheWaveWindow() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QSignalSpy started(&w, &SimWindow::simulationStarted);

        QVERIFY(w.startSimulation({"-do", "add wave -r /*; run 100 ns", "work.t"}));
        QCOMPARE(started.count(), 1);
        QVERIFY(w.waitUntilIdle());

        QVERIFY(w.store() != nullptr);
        QCOMPARE(w.wave()->rowCount(), 2); // add wave -r /* found clk and cnt
        QVERIFY(rowNamed(w, "t.clk") >= 0);
        QVERIFY(rowNamed(w, "t.cnt") >= 0);

        // 20 clock edges in 100 ns, the counter counts the rising ones
        QCOMPARE(w.store()->endTime(), Time{100000000});
        QCOMPARE(w.store()->signal(w.store()->findSignal("t.clk")).changeCount(), std::size_t{21});
        w.wave()->setCursorTime(0, 100000000.0);
        QCOMPARE(QString::fromStdString(w.wave()->rowValueText(rowNamed(w, "t.cnt"))), QString("0000000A"));

        // the objects browser knows the design
        QCOMPARE(w.objects()->signalCount(), 2);
        QCOMPARE(w.objects()->scopeCount(), 1);

        // the transcript shows the shell, not the internal notices
        const QString text = w.transcript()->text();
        QVERIFY(text.contains("% "));
        QVERIFY(!text.contains("@@lvd"));

        // while nothing was zoomed, the whole run is visible
        QVERIFY(w.wave()->autoFit());
        QVERIFY(w.wave()->view().endTime() >= 100000000.0 - 1.0);
    }

    void droppingSignalsFromObjectsOnTheLiveWaveWindow() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 0); // nothing added yet: the stream has the whole design
        QCOMPARE(w.objects()->signalCount(), 2);

        // select clk and cnt in the Objects pane, drag them onto the wave window
        QTreeWidget* tree = w.objects()->tree();
        std::function<void(QTreeWidgetItem*)> selectAll = [&](QTreeWidgetItem* it) {
            if (it->childCount() == 0)
                it->setSelected(true);
            for (int i = 0; i < it->childCount(); ++i)
                selectAll(it->child(i));
        };
        selectAll(tree->invisibleRootItem());
        const std::unique_ptr<QMimeData> mime(w.objects()->mimeDataForSelection());
        QVERIFY(mime != nullptr);
        QCOMPARE(signalPathsFromMime(mime.get()).size(), 2);

        WaveWidget* wave = w.wave();
        const QPoint p(80, wave->rowsTop() + 30);
        QDragEnterEvent enter(p, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(wave, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(p), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(wave, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(wave->rowCount(), 2);
        QVERIFY(rowNamed(w, "t.clk") >= 0);
        QVERIFY(rowNamed(w, "t.cnt") >= 0);
        // the live data is there at once: the values come from the stream
        wave->setCursorTime(0, 100000000.0);
        QCOMPARE(QString::fromStdString(wave->rowValueText(rowNamed(w, "t.cnt"))), QString("0000000A"));
        // the same rows land in the .do export
        const QString script = QString::fromStdString(wave->doScript());
        QVERIFY(script.contains("add wave /t/clk"));
        QVERIFY(wave->undo());
        QCOMPARE(wave->rowCount(), 0);
    }

    void runningMoreExtendsTheWaves() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave /cnt; run 50 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{50000000});

        w.setRunLength("30 ns");
        w.sendCommand("run " + w.runLengthText());
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{80000000});
        QVERIFY(w.wave()->view().endTime() >= 80000000.0 - 1.0); // the view grew with the data

        w.sendCommand("puts \"cnt=[examine /cnt]\"");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("cnt=8"));
    }

    void theViewDoesNotJumpAfterTheUserZoomed() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave /cnt; run 50 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        w.wave()->setView(10000000.0, 20000.0); // the user looks at a window in the middle
        const double start = w.wave()->view().startTime;
        w.sendCommand("run 50 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.wave()->autoFit());
        QCOMPARE(w.wave()->view().startTime, start); // not at the end before, so it stays where it was
    }

    void followKeepsTheRightEdgeAtTheEndOfARunningSimulation() {
        Design d;
        PREPARE(d);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SimWindow w(d.environment(), dir.path() + "/s.ini");
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave /cnt; run 50 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{50000000});
        WaveWidget* wave = w.wave();

        wave->zoomBy(4.0); // a window of a quarter of the run: the width Follow keeps
        const double tpp = wave->view().timePerPixel;
        QVERIFY(w.applyWaveOp("follow on"));
        QVERIFY(wave->follow());
        QVERIFY(w.waveToolBar()->followAction()->isChecked());
        for (int i = 1; i <= 3; ++i) {
            w.sendCommand("run 40 ns"); // the live stream grows; the 60 ms poll updates the view
            QVERIFY(w.waitUntilIdle());
            const double end = static_cast<double>(w.store()->endTime());
            QCOMPARE(end, 50000000.0 + 40000000.0 * i);
            QVERIFY2(qAbs(wave->view().endTime() - end) <= tpp * 2,
                     qPrintable(QString::number(wave->view().endTime())));
            QCOMPARE(wave->view().timePerPixel, tpp);
        }

        // a manual pan switches it off and the next run leaves the view where it is
        QVERIFY(w.applyWaveOp("scroll-time 0"));
        QVERIFY(!wave->follow());
        QVERIFY(!w.waveToolBar()->followAction()->isChecked());
        const double start = wave->view().startTime;
        w.sendCommand("run 40 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(wave->view().startTime, start);

        // and back on: it jumps to the end again
        w.waveToolBar()->followAction()->trigger();
        QVERIFY(wave->follow());
        QVERIFY(qAbs(wave->view().endTime() - static_cast<double>(w.store()->endTime())) <= tpp * 2);
        QVERIFY(!w.applyWaveOp("follow maybe"));
    }

    void followIsASettingAndAMenuEntry() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.path() + "/s.ini";
        const lvd::tools::Environment env = lvd::tools::discoverEnvironment();
        {
            SimWindow w(env, ini);
            QVERIFY(!w.wave()->follow());
            QAction* a = w.waveToolBar()->followAction();
            QVERIFY(!a->shortcut().isEmpty());
            // the View menu has the same action
            bool inView = false;
            for (QAction* m : w.menuBar()->actions())
                if (m->menu() && m->text().remove('&') == "View")
                    inView = m->menu()->actions().contains(a);
            QVERIFY(inView);
            a->trigger();
            QVERIFY(w.wave()->follow());
        }
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value("wave/follow").toBool(), true);
        {
            SimWindow w(env, ini);
            QVERIFY(w.wave()->follow()); // restored
            QVERIFY(w.waveToolBar()->followAction()->isChecked());
            w.wave()->setView(0.0, 1.0); // a manual change turns it off and the setting follows
            QVERIFY(!w.wave()->follow());
        }
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value("wave/follow").toBool(), false);
        {
            SimWindow w(env, ini);
            QVERIFY(!w.wave()->follow());
        }
    }

    void addWaveVariantsFromTheShell() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 20 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 0); // nothing added yet

        w.sendCommand("add wave -radix unsigned -color orange -label Counter /cnt");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 1);
        QCOMPARE(QString::fromStdString(w.wave()->rowName(0)), QString("Counter"));
        QCOMPARE(w.wave()->displayModel().root().children[0].radix, Radix::Unsigned);
        QVERIFY(w.wave()->rowColor(0).has_value());

        w.sendCommand("add wave -divider Control");
        w.sendCommand("add wave /t/clk"); // the form with the design name
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 3);

        w.sendCommand("add wave /nothing");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("no signal matches"));
        QCOMPARE(w.wave()->rowCount(), 3);

        w.sendCommand("delete wave *");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 0);
    }

    void expressionsDatasetsAndCompareFromTheShell() {
        Design d;
        PREPARE(d);
        // a finished run of the same design as the reference dataset
        {
            lvd::tools::ProcessOptions po;
            po.capture = true;
            if (!d.nvc.dllDir.isEmpty())
                po.pathPrepend.push_back(d.nvc.dllDir.toStdString());
            const auto r = lvd::tools::runProcess({d.nvc.exe.toStdString(), "-L", d.nvc.libDir.toStdString(),
                                                   "--std=08", "--work=work:work", "-e", "t", "-r",
                                                   "--stop-time=200ns", "--wave=ref.fst"},
                                                  po);
            QVERIFY2(r.started && r.exitCode == 0, r.output.c_str());
        }
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        w.sendCommand("add wave -expr {cnt > 3} -label big");
        QVERIFY(w.waitUntilIdle());
        const int big = rowNamed(w, "big");
        QVERIFY(big >= 0);
        QVERIFY(w.wave()->rowHasSource(big));
        const std::size_t before = w.wave()->rowSignal(big)->changeCount();
        w.sendCommand("add wave -expr {cnt and} -label bad");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("** add wave -expr: col"));
        QCOMPARE(rowNamed(w, "bad"), -1);

        // the expression row follows the run (cnt passes 3 at 35 ns; the value is then held)
        w.sendCommand("run 50 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowSignal(big)->changeCount(), before);
        w.wave()->setCursorTime(0, 140e6);
        QCOMPARE(w.wave()->rowValueText(big), std::string("1"));

        QVERIFY(w.openDataset("ref.fst"));
        QCOMPARE(w.wave()->datasetNames(), QStringList{"ref"});
        QVERIFY(!w.openDataset("ref.fst")); // the name is taken
        QVERIFY(w.lastError().contains("already open"));

        w.sendCommand("add wave ref:/t/cnt");
        w.sendCommand("add wave -compare -label same ref:/t/cnt /t/cnt");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.wave()->rowHasSource(rowNamed(w, "ref:t.cnt")));
        QVERIFY(w.wave()->rowHasSource(rowNamed(w, "same")));
        QCOMPARE(w.wave()->differenceCount(), std::size_t{0}); // the same design: no difference
        QVERIFY(!w.applyWaveOp("difference next"));

        w.sendCommand("add list -expr {cnt > 3} -label big");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.list()->model()->headerText(1), QString("big"));
        QVERIFY(w.list()->model()->rowCount() > 0);

        QVERIFY(w.closeDataset("ref"));
        QVERIFY(!w.wave()->rowHasSource(rowNamed(w, "same")));
    }

    void doubleClickingAnObjectAddsIt() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 10 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        emit w.objects()->addToWave({"t.clk", "t.cnt"});
        QCOMPARE(w.wave()->rowCount(), 2);
        QCOMPARE(QString::fromStdString(w.wave()->rowName(0)), QString("t.clk"));
        QVERIFY(w.wave()->rowHasSource(0));
    }

    void restartKeepsTheRowsAndStartsOver() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave -r /*; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{100000000});

        w.restartSimulation();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 2); // kept, and the startup script did not add them again
        QCOMPARE(w.store()->endTime(), Time{0});
        QVERIFY(w.wave()->rowHasSource(0)); // the rows are bound to the new run

        w.sendCommand("run 20 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{20000000});
        QVERIFY(w.transcript()->text().contains("restart"));
    }

    void compatCommandsAreAvailableInTheSession() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());

        w.sendCommand("force -freeze /cnt 99; run 5 ns; puts \"v=[examine /cnt]\"");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("v=99"));
    }

    void quittingEndsTheSession() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QSignalSpy finished(&w, &SimWindow::simulationFinished);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());

        w.sendCommand("quit -f");
        QVERIFY(w.waitUntilIdle());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(finished.at(0).at(0).toInt(), 0);
        QVERIFY(!w.simulationRunning());
        QVERIFY(w.transcript()->text().contains("simulation ended"));
    }

    void invalidCommandLinesAreReported() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(!w.startSimulation({}));
        QVERIFY(!w.lastError().isEmpty());
        QVERIFY(!w.startSimulation({"nolib.top"}));
        QVERIFY(w.lastError().contains("nolib"));
        QVERIFY(!w.startSimulation({"-wrong"}));
        QVERIFY(!w.simulationRunning());

        w.sendCommand("run 1 ns"); // no simulation: reported, not sent
        QVERIFY(w.transcript()->text().contains("No simulation is running"));
    }

    void aMissingSimulatorIsReported() {
        Design d;
        PREPARE(d);
        lvd::tools::Environment e = d.environment();
        e.nvc = "this_nvc_does_not_exist";
        SimWindow w(e);
        w.setDialogsEnabled(false);
        QVERIFY(!w.startSimulation({"work.t"}));
        QVERIFY(!w.simulationRunning());
    }

    void typedCommandsGoToTheShell() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment(), d.dir.path() + "/session.ini"); // typed commands go into the history
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());

        auto* edit = w.transcript()->input();
        QTest::keyClicks(edit, "run 10 ns");
        QTest::keyClick(edit, Qt::Key_Return);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains(
            "run -json 10 ns")); // echoed by the shell; run gets -json for the GUI

        QTest::keyClicks(edit, "restart");
        QTest::keyClick(edit, Qt::Key_Return);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{0});
    }

    void librariesAndStructureDocksFollowTheDesign() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);

        // the analysed design is in the library dock before anything runs
        QCOMPARE(w.library()->libraryCount(), 1);
        QCOMPARE(w.library()->unitCount(), 1);
        QCOMPARE(w.library()->tree()->topLevelItem(0)->child(0)->text(0), QString("t"));

        QVERIFY(w.startSimulation({"-do", "run 10 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.structure()->scopeCount(), 1);
        QCOMPARE(w.library()->tree()->topLevelItem(0)->child(0)->text(1),
                 QString("elaborated")); // after vsim elaborated it

        // selecting the scope lists its signals only
        QTreeWidgetItem* design = w.structure()->tree()->topLevelItem(0);
        w.structure()->tree()->setCurrentItem(design->child(0));
        QCOMPARE(w.objects()->scope(), QString("t"));
        QCOMPARE(w.objects()->tree()->topLevelItemCount(), 2); // clk, cnt
        w.structure()->tree()->setCurrentItem(design);
        QCOMPARE(w.objects()->scope(), QString());
    }

    void reportsBecomeMarkersAndLinksAndRestartClearsThem() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave /cnt; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->eventMarkerCount(), std::size_t(0));

        // the shell prints a line in the format of an NVC report
        w.sendCommand("puts {** Warning: 20ns+0: from the shell}");
        w.sendCommand("puts {a plain line, like a Verilog display}");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->eventMarkerCount(), std::size_t(1));
        QCOMPARE(w.wave()->eventMarker(0).timeFs, 20000000LL);
        QVERIFY(w.wave()->eventMarker(0).severity == EventSeverity::Warning);
        QCOMPARE(w.wave()->eventMarker(0).text, QString("from the shell"));

        w.show(); // the click needs a laid out text area
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QPlainTextEdit* view = w.transcript()->textView();
        const QTextBlock block =
            view->document()->findBlock(w.transcript()->text().indexOf("** Warning: 20ns"));
        QTextCursor c(block);
        c.setPosition(block.position() + 15);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {},
                          QPoint(view->cursorRect(c).left() + 3, view->cursorRect(c).center().y()));
        QCOMPARE(*w.wave()->cursorTime(0), 20000000.0);

        w.restartSimulation();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->eventMarkerCount(), std::size_t(0));
    }

    void aSimulateRequestFromTheLibraryStartsASimulation() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QSignalSpy started(&w, &SimWindow::simulationStarted);
        emit w.library()->simulateRequested("work.t");
        QCOMPARE(started.count(), 1);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.store() != nullptr);
    }

    void theWaveFormatSurvivesASaveAndALoad() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 40 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        w.sendCommand("add wave -radix unsigned -color orange -label Counter /cnt");
        w.sendCommand("add wave -divider Control");
        w.sendCommand("add wave -group Clocks /clk");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 4); // counter, divider, group, clk
        const std::string before = w.wave()->displayModel().serialize();

        const QString file = d.dir.path() + "/layout.do";
        QVERIFY(w.saveWaveFormat(file));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains("add wave -radix unsigned -color #ffa030 -label Counter /t/cnt"));
        QVERIFY(text.contains("add wave -divider Control"));
        QVERIFY(text.contains("add wave -group Clocks /t/clk"));

        w.wave()->clearWave();
        QCOMPARE(w.wave()->rowCount(), 0);
        QVERIFY(w.loadWaveFormat(file));
        QCOMPARE(w.wave()->displayModel().serialize(), before);

        // a script can also be sourced by the simulator itself, through the command compatibility layer
        w.wave()->clearWave();
        w.sendCommand("source {" + file + "}");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->displayModel().serialize(), before);
    }

    void loadingAWaveFormatNeedsKnownSignals() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        const QString file = d.dir.path() + "/x.do";
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("add wave /clk\n");
        f.close();
        QVERIFY(!w.loadWaveFormat(file)); // no simulation yet
        QVERIFY(w.lastError().contains("start a simulation"));
        QVERIFY(!w.loadWaveFormat(d.dir.path() + "/missing.do"));
    }

    void theSessionIsRemembered() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.path() + "/session.ini";
        lvd::tools::Environment env = lvd::tools::discoverEnvironment();
        {
            SimWindow w(env, ini);
            w.setDialogsEnabled(false);
            w.setRunLength("250 ns");
            w.resize(1111, 666);
            w.saveSession();
            QVERIFY(QFileInfo::exists(ini));
        }
        {
            SimWindow w(env, ini);
            QCOMPARE(w.runLengthText(), QString("250 ns"));
            QCOMPARE(w.settingsFile(), ini);
        }
    }

    void rendererMenuHasThreeExclusiveEntries() {
        EnvGuard env(nullptr);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SimWindow w(lvd::tools::discoverEnvironment(), dir.path() + "/s.ini");
        QAction* a = w.findChild<QAction*>("rendererAuto");
        QAction* c = w.findChild<QAction*>("rendererCpu");
        QAction* g = w.findChild<QAction*>("rendererGpu");
        QVERIFY(a && c && g);
        QVERIFY(a->actionGroup() != nullptr && a->actionGroup() == c->actionGroup() &&
                a->actionGroup() == g->actionGroup());
        QVERIFY(a->actionGroup()->isExclusive());
        QCOMPARE(a->actionGroup()->actions().size(), 3);
        QCOMPARE(w.renderPolicy(), RenderPolicy::Auto); // the default
        const bool gpuHere = !platformHasNoGpu(QGuiApplication::platformName());
        // The check mark follows what is used: offscreen cannot use the GPU, so QPainter is marked.
        QCOMPARE(a->isChecked(), gpuHere);
        QCOMPARE(c->isChecked(), !gpuHere);
        QVERIFY(!g->isChecked());
        c->trigger();
        QVERIFY(c->isChecked() && !a->isChecked() && !g->isChecked());
        QCOMPARE(w.wave()->renderBackend(), WaveWidget::Backend::Cpu);
        QCOMPARE(w.wave()->requestedBackend(), WaveWidget::Backend::Cpu);
        QVERIFY(w.findChild<QLabel*>("rendererLabel")->text().contains("QPainter"));
    }

    void rendererChoiceSurvivesARestart() {
        EnvGuard env(nullptr);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.path() + "/s.ini";
        const lvd::tools::Environment e = lvd::tools::discoverEnvironment();
        {
            SimWindow w(e, ini);
            w.findChild<QAction*>("rendererCpu")->trigger();
        }
        {
            QSettings s(ini, QSettings::IniFormat);
            QCOMPARE(s.value("render/backend").toString(), QString("cpu"));
        }
        {
            SimWindow w(e, ini);
            QCOMPARE(w.renderPolicy(), RenderPolicy::Cpu);
            QVERIFY(w.findChild<QAction*>("rendererCpu")->isChecked());
            w.findChild<QAction*>("rendererGpu")->trigger();
            QCOMPARE(w.renderPolicy(), RenderPolicy::Gpu);
        }
        SimWindow w(e, ini);
        QCOMPARE(w.renderPolicy(), RenderPolicy::Gpu);
        QSettings s(ini, QSettings::IniFormat);
        QCOMPARE(s.value("render/backend").toString(), QString("gpu"));
    }

    void environmentBeatsTheStoredRenderer() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.path() + "/s.ini";
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue("render/backend", "gpu");
        }
        const lvd::tools::Environment e = lvd::tools::discoverEnvironment();
        {
            EnvGuard env("cpu");
            SimWindow w(e, ini);
            QCOMPARE(w.renderPolicy(), RenderPolicy::Cpu);
            QCOMPARE(w.wave()->requestedBackend(), WaveWidget::Backend::Cpu);
        }
        EnvGuard env(nullptr);
        SimWindow w(e, ini);
        QCOMPARE(w.renderPolicy(), RenderPolicy::Gpu);
    }

    void gpuFallbackIsReportedOnce() {
        if (!platformHasNoGpu(QGuiApplication::platformName()))
            QSKIP("needs a platform without a GPU path");
        EnvGuard env("gpu");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SimWindow w(lvd::tools::discoverEnvironment(), dir.path() + "/s.ini");
        const QString line = QString("GPU renderer not available (platform %1), using QPainter")
                                 .arg(QGuiApplication::platformName());
        QVERIFY2(w.transcript()->text().contains(line), qPrintable(w.transcript()->text()));
        QCOMPARE(w.transcript()->text().count(line), 1);
        QCOMPARE(w.wave()->renderBackend(), WaveWidget::Backend::Cpu);
        QVERIFY(w.findChild<QLabel*>("rendererLabel")->text().contains("GPU not available: platform"));
        QVERIFY(w.findChild<QAction*>("rendererGpu")->toolTip().contains("using QPainter"));
        QVERIFY(w.findChild<QAction*>("rendererCpu")->isChecked());
        w.setRenderPolicy(RenderPolicy::Gpu, false); // asking again does not repeat the line
        QCOMPARE(w.transcript()->text().count(line), 1);
    }

    void automaticFallbackOnAHeadlessPlatformStaysQuiet() {
        if (!platformHasNoGpu(QGuiApplication::platformName()))
            QSKIP("needs a platform without a GPU path");
        EnvGuard env(nullptr);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SimWindow w(lvd::tools::discoverEnvironment(), dir.path() + "/s.ini");
        QVERIFY(!w.transcript()->text().contains("GPU renderer not available"));
        QVERIFY(w.findChild<QLabel*>("rendererLabel")->text().contains("GPU not available"));
    }

    void thePreviousArgumentsComeBackForTheStartDialog() {
        Design d;
        PREPARE(d);
        const QString ini = d.dir.path() + "/s.ini";
        {
            SimWindow w(d.environment(), ini);
            w.setDialogsEnabled(false);
            QVERIFY(w.startSimulation({"-do", "run 5 ns", "work.t"}));
            QVERIFY(w.waitUntilIdle());
        }
        SimWindow again(d.environment(), ini);
        QCOMPARE(again.previousArguments(), (QStringList{"-do", "run 5 ns", "work.t"}));
    }

    void processesAndStepping() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QSignalSpy stopped(&w, &SimWindow::stopped);
        QVERIFY(w.startSimulation({"-do", "run 20 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        // the process list was read once the shell was idle after the startup script
        QVERIFY(w.processes()->processCount() >= 1);
        bool located = false;
        for (const lvd::gui::ProcessInfo& p : w.processes()->processes())
            if (p.file.endsWith("t.vhd") && p.line > 0 && !p.state.isEmpty())
                located = true;
        QVERIFY(located);

        // selecting a process shows its file with the marker on its line
        w.processes()->table()->selectRow(0);
        const lvd::gui::ProcessInfo sel = w.processes()->selectedProcess();
        QVERIFY(w.source()->filePath().endsWith("t.vhd"));
        QCOMPARE(w.source()->currentLine(), sel.line);
        QVERIFY(w.source()->view()->toPlainText().contains("entity t"));

        stopped.clear();
        w.stepDelta();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(stopped.count(), 1);
        QVERIFY(w.lastStatus() == lvd::gui::RunStatus::Paused);
        QVERIFY(w.stopText().startsWith("Paused at "));
        QVERIFY(w.transcript()->text().contains("step -json -delta"));
        QVERIFY(!w.transcript()->text().contains("{\"v\"")); // replies are summarised, not dumped
        QCOMPARE(w.findChild<QLabel*>("stopLabel")->text(), w.stopText());

        const Time before = w.store()->endTime();
        w.stepTime();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(stopped.count(), 2);
        QVERIFY(w.store()->endTime() >= before);

        // the toolbar buttons do the same
        w.findChild<QAction*>("stepDelta")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(stopped.count(), 3);

        // a plain run is rewritten to -json too, so its stop is reported
        w.sendCommand("run 10 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(stopped.count(), 4);
        QVERIFY(w.stopText().startsWith("Paused at "));
    }

    void timeBreakpoints() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QSignalSpy stopped(&w, &SimWindow::stopped);
        QVERIFY(w.startSimulation({"-do", "add wave /cnt; run 10 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 0);

        w.breakpoints()->setTimeText("40", "ns");
        QVERIFY(w.breakpoints()->submitAdd());
        w.breakpoints()->setTimeText("0.5", "us");
        QVERIFY(w.breakpoints()->submitAdd());
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 2);
        QCOMPARE(w.breakpoints()->table()->item(0, 1)->text(), QString("40 ns"));
        QCOMPARE(w.breakpoints()->table()->item(1, 1)->text(), QString("500 ns"));

        w.sendCommand("run 100 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.lastStatus() == lvd::gui::RunStatus::Breakpoint);
        QCOMPARE(w.stopText(), QString("Breakpoint 1 at 40 ns"));
        QVERIFY(w.transcript()->text().contains("Breakpoint 1 hit at 40 ns"));
        QCOMPARE(w.findChild<QLabel*>("stopLabel")->text(), QString("Breakpoint 1 at 40 ns"));
        QCOMPARE(w.store()->endTime(), Time{40000000});
        QCOMPARE(w.breakpoints()->breakpointCount(), 1); // the kernel deleted the one that was hit
        QCOMPARE(stopped.last().at(0).value<lvd::gui::RunReply>().breakpoint->id, 1);

        // delete the other one by the pane, and check with the kernel's own list
        w.breakpoints()->table()->selectRow(0);
        w.breakpoints()->requestDeleteSelected();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 0);
        w.refreshBreakpoints();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 0);

        w.addBreakpoint("60", "ns");
        w.addBreakpoint("70", "ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 2);
        w.deleteAllBreakpoints();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 0);

        // a breakpoint in the past is an error of the kernel: shown in the transcript, nothing is listed
        w.addBreakpoint("1", "ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 0);
        QVERIFY(!w.session().busy());
    }

    void breakpointsFromTheCommandLineAreListed() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "breakpoint add 30 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 1); // read after the startup script
        w.sendCommand("breakpoint add 35 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.breakpoints()->breakpointCount(), 2);
    }

    void unreadableRepliesAreShownNotFatal() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "proc step {args} {puts {{\"v\":2,\"now\":1}}}", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        w.sendCommand("step"); // an unknown schema version
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("Unreadable reply"));
        QVERIFY(w.transcript()->text().contains("\"v\":2"));
        QVERIFY(w.stopText().isEmpty());

        // a reply nobody asked for (typed by the user) is printed as the shell printed it
        w.sendCommand("puts {{\"v\":1,\"hello\":true}}");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("{\"v\":1,\"hello\":true}"));

        // and the session still works
        w.sendCommand("puts alive");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("alive"));
    }

    void dockVisibilityIsKeptInTheSessionFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("session.ini");
        {
            SimWindow w(lvd::tools::discoverEnvironment(), ini);
            for (const char* name : {"processesDock", "breakpointsDock", "sourceDock"})
                QVERIFY2(w.findChild<QDockWidget*>(name) != nullptr, name);
            w.findChild<QDockWidget*>("sourceDock")->hide();
            w.findChild<QDockWidget*>("processesDock")->hide();
            w.saveSession();
        }
        SimWindow w2(lvd::tools::discoverEnvironment(), ini);
        QVERIFY(w2.findChild<QDockWidget*>("sourceDock")->isHidden());
        QVERIFY(w2.findChild<QDockWidget*>("processesDock")->isHidden());
        QVERIFY(!w2.findChild<QDockWidget*>("breakpointsDock")->isHidden());
    }

    void dockTabBarsAreInsideTheirAreasAndNotDuplicated() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SimWindow w(lvd::tools::discoverEnvironment(), dir.filePath("fresh.ini"));
        w.resize(1400, 800);
        w.show();
        w.settleLayout(); // what the headless screenshot path does: no event loop, still one tab bar per
                          // group
        QList<QTabBar*> bars;
        for (QTabBar* b : w.findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly))
            if (b->isVisible() && b->count() > 0)
                bars.push_back(b);
        QCOMPARE(bars.size(), 2); // Library/Objects/Sim and Processes/Breakpoints
        const QRect status = w.statusBar()->geometry();
        const QRect inside = QRect(QPoint(0, 0), w.size()).adjusted(0, 0, 0, -status.height());
        for (QTabBar* b : bars) {
            const QRect g = b->geometry();
            QVERIFY2(inside.contains(g),
                     qPrintable(QString("tab bar %1 outside the dock region").arg(b->tabText(0))));
            QVERIFY2(!g.intersects(status),
                     qPrintable(QString("tab bar %1 over the status bar").arg(b->tabText(0))));
            // The tab bar must sit directly under a visible dock widget of its own group.
            bool owned = false;
            for (QDockWidget* dock : w.findChildren<QDockWidget*>())
                if (dock->isVisible() && dock->windowTitle() == b->tabText(b->currentIndex()) &&
                    qAbs(dock->geometry().bottom() - g.top()) < 12)
                    owned = true;
            QVERIFY2(owned, qPrintable(QString("tab bar %1 is not under its dock").arg(b->tabText(0))));
        }
        QVERIFY(!bars[0]->geometry().intersects(bars[1]->geometry()));
    }

    void theToolbarsUseIconsOnly() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        for (const char* name : {"run", "runAll", "restart", "stop", "stepDelta", "stepTime"}) {
            QAction* a = w.findChild<QAction*>(name);
            QVERIFY2(a != nullptr, name);
            QVERIFY2(!a->icon().isNull(), name);
            QVERIFY2(!a->toolTip().isEmpty(), name);
        }
        // The window's tool bars; the small tool bars inside docks (Project, Regression) use text.
        for (QToolBar* bar : w.findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly))
            QCOMPARE(bar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    }

    void docksCanBeHiddenAndRaised() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.show();
        QDockWidget* source = w.findChild<QDockWidget*>("sourceDock");
        QVERIFY(source != nullptr);
        QVERIFY(source->isVisible());
        QVERIFY(w.setDockVisible("source", false));
        QVERIFY(!source->isVisible());
        QVERIFY(w.setDockVisible("Source", true));
        QVERIFY(source->isVisible());
        QVERIFY(w.setDockVisible("objects", true));
        QVERIFY(w.setDockVisible("sim", true));
        QVERIFY(!w.setDockVisible("nonsense", true));
    }

    // ---- default layout and Library pane -----------------------------------------------------------
    static QTabBar* tabBarWith(SimWindow& w, const QString& text) {
        for (QTabBar* bar : w.findChildren<QTabBar*>())
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabText(i) == text)
                    return bar;
        return nullptr;
    }

    static void checkDefaultLayout(SimWindow& w) {
        auto dock = [&w](const char* n) { return w.findChild<QDockWidget*>(n); };
        QDockWidget* lib = dock("libraryDock");
        QDockWidget* sim = dock("structureDock");
        QDockWidget* obj = dock("objectsDock");
        QVERIFY(lib && sim && obj);
        // Library and Sim are tabs of one area, Objects is not one of them
        QVERIFY(w.tabifiedDockWidgets(lib).contains(sim));
        QVERIFY(!w.tabifiedDockWidgets(lib).contains(obj));
        QVERIFY(w.tabifiedDockWidgets(obj).isEmpty());
        QTabBar* bar = tabBarWith(w, "Library");
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->count(), 3); // Library | Sim | Project
        QVERIFY(tabBarWith(w, "Sim") == bar);
        QVERIFY(tabBarWith(w, "Project") == bar);
        // all three are shown (the hidden tab is the one behind), in one column, Objects below the tabs
        QVERIFY(!obj->isHidden() && !lib->isHidden() && !sim->isHidden());
        QVERIFY(obj->isVisible());
        QCOMPARE(w.dockWidgetArea(lib), Qt::LeftDockWidgetArea);
        QCOMPARE(w.dockWidgetArea(obj), Qt::LeftDockWidgetArea);
        const QRect tabs = QRect(bar->mapTo(&w, QPoint(0, 0)), bar->size());
        const QPoint objTop = obj->mapTo(&w, QPoint(0, 0));
        QVERIFY2(objTop.y() >= tabs.bottom(), "Objects must be below the Library|Sim tabs");
        QVERIFY(qAbs(objTop.x() - tabs.left()) < 8); // same column
        QVERIFY(obj->height() > 90);
        QVERIFY(lib->width() > 150);
        QVERIFY(w.wave()->width() > lib->width()); // the waveform gets the room
        for (const char* n : {"transcriptDock", "processesDock", "sourceDock"})
            QVERIFY(dock(n)->isVisible());
    }

    void defaultLayoutOnFirstStart_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("default") << QSize(1400, 800);
        QTest::newRow("small") << QSize(900, 560);
    }

    void defaultLayoutOnFirstStart() {
        QFETCH(QSize, size);
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SimWindow w(lvd::tools::discoverEnvironment(), tmp.filePath("fresh.ini"));
        w.resize(size);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.settleLayout();
        checkDefaultLayout(w);
        // before a design is loaded the Library tab is in front
        QTabBar* bar = tabBarWith(w, "Library");
        QCOMPARE(bar->tabText(bar->currentIndex()), QString("Library"));
    }

    void resetLayoutRestoresTheDefault() {
        QTemporaryDir tmp;
        SimWindow w(lvd::tools::discoverEnvironment(), tmp.filePath("reset.ini"));
        w.resize(1400, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QDockWidget* obj = w.findChild<QDockWidget*>("objectsDock");
        QDockWidget* lib = w.findChild<QDockWidget*>("libraryDock");
        obj->setFloating(true);
        lib->hide();
        w.findChild<QDockWidget*>("sourceDock")->hide();
        QAction* reset = w.findChild<QAction*>("resetLayout");
        QVERIFY(reset != nullptr);
        reset->trigger();
        w.settleLayout();
        QVERIFY(!obj->isFloating());
        QVERIFY(w.findChild<QDockWidget*>("sourceDock")->isVisible());
        checkDefaultLayout(w);
    }

    void aSavedLayoutStillRestores() {
        QTemporaryDir tmp;
        const QString ini = tmp.filePath("saved.ini");
        {
            SimWindow w(lvd::tools::discoverEnvironment(), ini);
            w.resize(1200, 700);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QVERIFY(w.setDockVisible("source", false));
            w.saveSession();
        }
        SimWindow w2(lvd::tools::discoverEnvironment(), ini);
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        QVERIFY(!w2.findChild<QDockWidget*>("sourceDock")->isVisible());
        QVERIFY(w2.tabifiedDockWidgets(w2.findChild<QDockWidget*>("libraryDock"))
                    .contains(w2.findChild<QDockWidget*>("structureDock")));
    }

    void libraryDoubleClickNeverSimulates() {
        QTemporaryDir tmp;
        SimWindow w(lvd::tools::discoverEnvironment(), tmp.filePath("dbl.ini"));
        w.setDialogsEnabled(false);
        lvd::tools::LibraryInfo work;
        work.name = "work";
        work.units.push_back({"counter", {"rtl"}, true});
        w.library()->setLibraries({work});
        QSignalSpy started(&w, &SimWindow::simulationStarted);
        QSignalSpy requested(w.library(), &LibraryPane::simulateRequested);
        QSignalSpy viewed(w.library(), &LibraryPane::viewSourceRequested);
        w.library()->setSourceGate({}); // whether an nvc was found does not matter here
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTreeWidget* tree = w.library()->tree();
        QTreeWidgetItem* lib = tree->topLevelItem(0);
        QTreeWidgetItem* unit = lib->child(0);

        // a real double click on the unit row asks for its source (here unknown: a note) and starts nothing
        QVERIFY(!unit->isExpanded());
        const QPoint at = tree->visualItemRect(unit).center();
        // a double click is press, release, double-click event, release
        auto doubleClick = [&](const QPoint& p) {
            QWidget* vp = tree->viewport();
            QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, p);
            QTest::mouseDClick(vp, Qt::LeftButton, Qt::NoModifier, p);
        };
        doubleClick(at);
        QCOMPARE(viewed.count(), 1);
        QCOMPARE(viewed.at(0).at(0).toString(), QString("work.counter"));
        QVERIFY(!unit->isExpanded()); // the arrow expands it
        QVERIFY(w.transcript()->text().contains("View Source"));

        // the library row collapses and expands (it starts expanded)
        QVERIFY(lib->isExpanded());
        const QPoint libAt = tree->visualItemRect(lib).center();
        doubleClick(libAt);
        QVERIFY(!lib->isExpanded());
        doubleClick(libAt);
        QVERIFY(lib->isExpanded());

        // Return (activation) does not simulate either
        tree->setCurrentItem(unit);
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(requested.count(), 0);
        QCOMPARE(started.count(), 0);
        QVERIFY(!w.simulationRunning());
    }

    void libraryContextMenuSimulates() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QSignalSpy started(&w, &SimWindow::simulationStarted);
        QTreeWidget* tree = w.library()->tree();
        QTreeWidgetItem* lib = tree->topLevelItem(0);
        QVERIFY(lib != nullptr);

        // on a library the entry is there but disabled
        QMenu* onLib = w.library()->createContextMenu(lib);
        QAction* simLib = onLib->findChild<QAction*>("simulate");
        QVERIFY(simLib != nullptr);
        QCOMPARE(simLib->text(), QString("Simulate"));
        QVERIFY(!simLib->isEnabled());
        delete onLib;

        QMenu* menu = w.library()->createContextMenu(lib->child(0));
        QAction* sim = menu->findChild<QAction*>("simulate");
        QVERIFY(sim != nullptr && sim->isEnabled());
        QSignalSpy requested(w.library(), &LibraryPane::simulateRequested);
        sim->trigger();
        delete menu;
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), QString("work.t"));
        QCOMPARE(started.count(), 1);
        QVERIFY(w.simulationRunning());
        QVERIFY(w.waitUntilIdle());
        // the Sim tab comes to the front once there is a design
        QTabBar* bar = tabBarWith(w, "Sim");
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->tabText(bar->currentIndex()), QString("Sim"));
    }

    void screenshotOfARunningSimulation() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        w.resize(1200, 700);
        QVERIFY(w.startSimulation({"-do", "add wave -r /*; run 200 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        const QImage img = w.grab().toImage();
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(1200, 700));
    }
    // ---- arranging the wave rows -------------------------------------------------------------------
    void arrangedRowsKeepTheirOrderAcrossARestart() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave -r /*; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->rowCount(), 2);

        QVERIFY(w.saveWaveFormat(d.dir.path() + "/clean.do"));
        QVERIFY(!w.waveLayoutModified());
        QVERIFY(w.applyWaveOp("move-up t.cnt"));
        QVERIFY(w.waveLayoutModified()); // arranging rows marks the layout as modified
        QVERIFY(w.applyWaveOp("group t.clk Pair"));
        QVERIFY(w.applyWaveOp("divider-above Pair top"));
        QVERIFY(w.applyWaveOp("rename Pair Clocks"));
        const std::string order = w.wave()->displayModel().serialize();
        QCOMPARE(QString::fromStdString(w.wave()->rowName(0)), QString("t.cnt"));

        w.restartSimulation();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.wave()->displayModel().serialize(), order);
        QCOMPARE(QString::fromStdString(w.wave()->rowName(0)), QString("t.cnt"));
        QCOMPARE(QString::fromStdString(w.wave()->rowName(1)), QString("top"));
        QVERIFY(w.wave()->rowHasSource(0)); // still bound to the new run
        QVERIFY(w.wave()->canUndo());       // the history belongs to the rows, which are the same

        // the .do export of the arranged rows loads back into the same arrangement
        const QString file = d.dir.path() + "/arranged.do";
        QVERIFY(w.saveWaveFormat(file));
        QVERIFY(!w.waveLayoutModified());
        w.wave()->clearWave();
        QVERIFY(w.loadWaveFormat(file));
        QCOMPARE(w.wave()->displayModel().serialize(), order);
    }

    void theWaveMenuArrangesRowsAndWaveOpsReportErrors() {
        Design d;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave -r /*; run 40 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        QMenu* menu = w.findChild<QMenu*>("waveMenu");
        QVERIFY(menu != nullptr);
        w.wave()->selectRow(1);
        emit menu->aboutToShow();
        QAction* up = menu->findChild<QAction*>("moveUp");
        QVERIFY(up != nullptr);
        QVERIFY(up->isEnabled());
        up->trigger();
        QCOMPARE(QString::fromStdString(w.wave()->rowName(0)), QString("t.cnt"));
        emit menu->aboutToShow(); // rebuilt for the new state
        QVERIFY(menu->findChild<QAction*>("undo")->isEnabled());

        QVERIFY(!w.applyWaveOp("move-up nosuchrow"));
        QVERIFY(w.lastError().contains("nosuchrow"));
        QVERIFY(!w.applyWaveOp("frobnicate"));
        QSignalSpy rejected(w.wave(), &WaveWidget::editRejected);
        QVERIFY(w.applyWaveOp("group t.cnt Pair"));
        QVERIFY(!w.applyWaveOp("rename Pair \" \"")); // an empty group name is refused
        QCOMPARE(rejected.count(), 1);
        QVERIFY(w.statusBar()->currentMessage().contains("group"));
    }

    void objectsShowThePortModesTheKernelStreams() {
        Design d;
        d.vhdl = kPorts;
        d.verilog = kPortsVerilog;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 10 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        // no describe request: the modes arrive with the signal definitions of the live stream
        const ObjectsPane* o = w.objects();
        QCOMPARE(o->portModeOf("t.i"), PortMode::Internal);
        QCOMPARE(o->portModeOf("t.u.i"), PortMode::In); // collapsed into t.i, still an input of u
        QCOMPARE(o->portModeOf("t.u.o"), PortMode::Out);
        QCOMPARE(o->portModeOf("t.u.io"), PortMode::InOut);
        QCOMPARE(o->portModeOf("t.u.b"), PortMode::Buffer);
        QCOMPARE(o->portModeOf("t.u.s"), PortMode::Internal);
        QCOMPARE(o->portModeOf("t.vl.vi"), PortMode::In); // Verilog ports
        QCOMPARE(o->portModeOf("t.vl.vout"), PortMode::Out);
        QCOMPARE(o->portModeOf("t.vl.w"), PortMode::Internal);

        // and the rows of the tree carry them
        int checked = 0;
        std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
            const QString path = it->data(0, Qt::UserRole).toString();
            if (path == "t.u.b") {
                QVERIFY(it->toolTip(0).contains("buffer port"));
                ++checked;
            } else if (path == "t.vl.vout") {
                QVERIFY(it->toolTip(0).contains("output port"));
                ++checked;
            }
            for (int i = 0; i < it->childCount(); ++i)
                walk(it->child(i));
        };
        walk(o->tree()->invisibleRootItem());
        QCOMPARE(checked, 2);
        // nothing was asked behind the user's back for the modes: the only describe is the probe at the first
        // prompt, which learns whether the kernel reports source locations (View Declaration)
        const QString text = w.transcript()->text();
        QCOMPARE(text.count("describe"), text.count("describe -json -location {/}"));
        QVERIFY(text.count("describe") <= 1);
    }

    void theDataEndsWhereTheRunEndedNotAtTheLastEvent() {
        Design d;
        d.vhdl = kPorts;
        d.verilog = kPortsVerilog;
        PREPARE(d);
        SimWindow w(d.environment());
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "add wave /i; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());

        // the last value change is at 40 ns, the run went on to 100 ns
        const Signal& i = w.store()->signal(w.store()->findSignal("t.i"));
        QCOMPARE(i.time(i.changeCount() - 1), Time{40000000});
        QCOMPARE(w.store()->endTime(), Time{100000000});
        QVERIFY(w.wave()->autoFit());
        QVERIFY(w.wave()->view().endTime() >= 100000000.0 - 1.0);

        // the next run continues from 100 ns, not from the last event
        w.sendCommand("run 100 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.store()->endTime(), Time{200000000});
    }
};

QTEST_MAIN(TestSimWindow)
#include "test_sim_window.moc"
