// SPDX-License-Identifier: GPL-3.0-or-later
// The source debugger end to end: a real nvc simulation driven through SimWindow (gutter breakpoints, stops,
// statement steps, call stack, variables, refused commands, typed commands, persistence).
#include "app/breakpoint_pane.hpp"
#include "app/call_stack_pane.hpp"
#include "app/locals_pane.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"

#include <QAction>
#include <QLabel>
#include <QStatusBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <QtTest>

using namespace lvd::gui;
using lvd::gui::testing::AnalysedDesign;

namespace {

// Line numbers matter: the tests set breakpoints on them.
const char* kDesign = "entity t is end;\n"                                 // 1
                      "architecture a of t is\n"                           // 2
                      "  type pair is record\n"                            // 3
                      "    a : integer;\n"                                 // 4
                      "    b : bit;\n"                                     // 5
                      "  end record;\n"                                    // 6
                      "  signal count : integer := 0;\n"                   // 7
                      "  function twice (x : integer) return integer is\n" // 8
                      "  begin\n"                                          // 9
                      "    return x * 2;\n"                                // 10
                      "  end function;\n"                                  // 11
                      "begin\n"                                            // 12
                      "  counter: process is\n"                            // 13
                      "    variable total : integer := 0;\n"               // 14
                      "    variable both  : pair := (7, '1');\n"           // 15
                      "  begin\n"                                          // 16
                      "    for i in 1 to 3 loop\n"                         // 17
                      "      total := total + twice(i);\n"                 // 18
                      "      both.a := both.a + 1;\n"                      // 19
                      "      count <= total;\n"                            // 20
                      "      wait for 10 ns;\n"                            // 21
                      "    end loop;\n"                                    // 22
                      "    wait;\n"                                        // 23
                      "  end process;\n"                                   // 24
                      "end;\n";                                            // 25

#define PREPARE(d)                                                                                           \
    if (!(d).nvc.found)                                                                                      \
        QSKIP("nvc not found (set LVDSIM_NVC)");                                                             \
    QVERIFY2((d).prepare(kDesign), "could not analyse the test design")

/// Clicks the Source gutter on a line of the file on view, as the user does.
bool clickGutter(SimWindow& w, int line) {
    SourceView* v = w.source()->view();
    v->showLine(line);
    QCoreApplication::processEvents();
    for (int y = 0; y < v->viewport()->height(); ++y) {
        if (v->lineAt(y) == line) {
            QTest::mouseClick(v->gutter(), Qt::LeftButton, {}, QPoint(6, y + 2));
            return true;
        }
    }
    return false;
}

QAction* action(SimWindow& w, const char* name) {
    return w.findChild<QAction*>(name);
}

} // namespace

class TestDebugSession : public QObject {
    Q_OBJECT
private slots:
    void stopStepInspectAndResume() {
        AnalysedDesign d;
        PREPARE(d);
        SimWindow w(d.environment(), d.file("session.ini"));
        w.setDialogsEnabled(false);
        w.resize(1400, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.stoppedAtSource());
        QVERIFY(!action(w, "continue")->isEnabled());
        QVERIFY(!action(w, "stepOver")->isEnabled());
        QVERIFY(action(w, "stepInto")->isEnabled()); // runs to the first statement
        QVERIFY(action(w, "run")->isEnabled());

        // a click in the gutter sets a line breakpoint in the kernel
        QVERIFY(w.showSource(d.file("t.vhd"), 18));
        QVERIFY(clickGutter(w, 18));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lineBreakpoints().size(), 1);
        QCOMPARE(w.lineBreakpoints()[0].line, 18);
        QCOMPARE(w.lineBreakpoints()[0].state, LineBreakpoint::State::Verified);
        QCOMPARE(w.lineBreakpoints()[0].ids.size(), 1);
        QCOMPARE(w.source()->view()->breakpointMarks().value(18), BreakpointMark::Enabled);
        QCOMPARE(w.breakpoints()->lineBreakpointCount(), 1);

        // run: stops on that line, the marker and the status bar say where
        QSignalSpy stops(&w, &SimWindow::stoppedAtLine);
        action(w, "runAll")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(stops.count(), 1);
        QVERIFY(w.stoppedAtSource());
        QVERIFY(w.lastStop().atBreakpoint);
        QCOMPARE(w.lastStop().process, QString("/counter"));
        QCOMPARE(w.lastStop().frame.line, 18);
        QVERIFY(w.source()->filePath().endsWith("t.vhd"));
        QCOMPARE(w.source()->executionLine(), 18);
        QCOMPARE(w.source()->view()->executionMark(), ExecutionMark::Innermost);
        QCOMPARE(w.stopText(), QString("Stopped at t.vhd:18 in /counter"));
        QCOMPARE(w.findChild<QLabel*>("stopLabel")->text(), w.stopText());
        QVERIFY(!w.transcript()->text().contains("Unreadable reply"));
        QVERIFY(w.transcript()->text().contains("# Breakpoint"));
        QVERIFY(action(w, "continue")->isEnabled());
        QVERIFY(action(w, "stepOver")->isEnabled());
        QVERIFY(action(w, "stepOut")->isEnabled());
        QVERIFY(!action(w, "run")->isEnabled()); // a time is refused while stopped
        QVERIFY(!action(w, "stepDelta")->isEnabled());
        QCOMPARE(w.callStack()->frameCount(), 1);
        QCOMPARE(w.locals()->valueOf("total"), QString("0"));
        QCOMPARE(w.locals()->valueOf("both.a"), QString("7"));
        QCOMPARE(w.locals()->valueOf("both.b"), QString("'1'"));
        QCOMPARE(w.lineBreakpoints()[0].hits, 1);

        // step into the function: the marker moves into it, the stack has two frames
        action(w, "stepInto")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lastStop().frame.kind, QString("function"));
        QCOMPARE(w.lastStop().frame.line, 10);
        QCOMPARE(w.source()->executionLine(), 10);
        QCOMPARE(w.callStack()->frameCount(), 2);
        QCOMPARE(w.stackFrames().at(1).kind, QString("process"));
        QCOMPARE(w.stackFrames().at(1).line, 18);
        QCOMPARE(w.locals()->variableCount(), 0); // functions show no variables in this kernel version
        QVERIFY(!w.locals()->note().isEmpty());

        // the caller frame: the marker goes to the call, Locals shows the process variables
        w.callStack()->selectLevel(1);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.selectedFrame(), 1);
        QCOMPARE(w.source()->executionLine(), 18);
        QCOMPARE(w.source()->view()->executionMark(), ExecutionMark::Caller);
        QCOMPARE(w.locals()->valueOf("total"), QString("0"));

        // step out: back in the process after the call; total changed since it was last shown
        action(w, "stepOut")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lastStop().frame.kind, QString("process"));
        QVERIFY(w.lastStop().frame.line >= 18);
        QCOMPARE(w.callStack()->frameCount(), 1);
        QCOMPARE(w.locals()->valueOf("total"), QString("2"));
        QVERIFY(w.locals()->changedPaths().contains("total"));
        const int afterOut = w.lastStop().frame.line;

        // step over, typed in the console: the panes follow
        emit w.transcript()->commandEntered("step -over");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.stoppedAtSource());
        QVERIFY(w.lastStop().frame.line > afterOut);
        QCOMPARE(w.source()->executionLine(), w.lastStop().frame.line);

        // refused while stopped: the kernel's error is shown, the stop stays
        w.sendCommand("force /count 5");
        QVERIFY(w.waitUntilIdle());
        QVERIFY2(w.lastShellError().contains("stopped"), qPrintable(w.lastShellError()));
        QVERIFY(w.statusBar()->currentMessage().startsWith("Refused while stopped"));
        w.sendCommand("run 5 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.stoppedAtSource());
        QVERIFY(w.transcript()->text().contains("** Error"));
        QCOMPARE(w.source()->executionLine(), w.lastStop().frame.line);
        // other commands work while stopped
        emit w.transcript()->commandEntered("stack");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("kind process"));

        // a line breakpoint typed in the console joins the list and the gutter
        emit w.transcript()->commandEntered("breakpoint add t.vhd:21");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.lineBreakpoints().indexOf(d.file("t.vhd"), 21) >= 0);
        QCOMPARE(w.source()->view()->breakpointMarks().value(21), BreakpointMark::Enabled);

        // disable 21 in the Breakpoints pane, remove 18 by the gutter: continue runs to the end
        const int row = w.breakpoints()->rowOfLine(d.file("t.vhd"), 21);
        QVERIFY(row >= 0);
        w.breakpoints()->table()->item(row, 0)->setCheckState(Qt::Unchecked);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.lineBreakpoints()[w.lineBreakpoints().indexOf(d.file("t.vhd"), 21)].enabled);
        QCOMPARE(w.source()->view()->breakpointMarks().value(21), BreakpointMark::Disabled);
        QVERIFY(clickGutter(w, 18));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lineBreakpoints().indexOf(d.file("t.vhd"), 18), -1);
        w.refreshBreakpoints(); // the kernel agrees: nothing left that could stop
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lineBreakpoints().size(), 1);

        action(w, "continue")->trigger();
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.stoppedAtSource());
        QVERIFY(w.lastStatus() == RunStatus::Finished);
        QCOMPARE(w.source()->executionLine(), 0);
        QCOMPARE(w.callStack()->frameCount(), 0);
        QVERIFY(!action(w, "continue")->isEnabled());
        QVERIFY(!action(w, "stepInto")->isEnabled()); // nothing left to run
        QCOMPARE(stops.count(), 4);                   // breakpoint, into, out, over
    }

    void breakpointsAreKeptAndAppliedAtStartAndRestart() {
        AnalysedDesign d;
        PREPARE(d);
        const QString ini = d.file("keep.ini");
        {
            SimWindow w(d.environment(), ini);
            w.setDialogsEnabled(false);
            // set without a simulation: planned, sent when it starts
            QVERIFY(w.toggleLineBreakpoint(d.file("t.vhd"), 20));
            QCOMPARE(w.lineBreakpoints()[0].state, LineBreakpoint::State::Planned);
            QVERIFY(w.startSimulation({"work.t"}));
            QVERIFY(w.waitUntilIdle());
            QCOMPARE(w.lineBreakpoints()[0].state, LineBreakpoint::State::Verified);
            w.sendCommand("run -all");
            QVERIFY(w.waitUntilIdle());
            QVERIFY(w.stoppedAtSource());
            QCOMPARE(w.lastStop().frame.line, 20);

            // restart while stopped: from time zero, the breakpoint is set again and reached again
            w.restartSimulation();
            QVERIFY(w.waitUntilIdle());
            QVERIFY(!w.stoppedAtSource());
            QCOMPARE(w.lineBreakpoints().size(), 1);
            QCOMPARE(w.lineBreakpoints()[0].state, LineBreakpoint::State::Verified);
            w.sendCommand("run");
            QVERIFY(w.waitUntilIdle());
            QVERIFY(w.stoppedAtSource());
            QCOMPARE(w.lastStop().frame.line, 20);
            QCOMPARE(w.lastStop().nowFs, qint64(0));
            w.saveSession();
        }
        // a new window for the same design has it, and a stop inside the startup script is noticed
        SimWindow w2(d.environment(), ini);
        w2.setDialogsEnabled(false);
        QCOMPARE(w2.lineBreakpoints().size(), 1);
        QCOMPARE(w2.lineBreakpoints()[0].line, 20);
        QVERIFY(w2.startSimulation({"-do", "run -all", "work.t"}));
        QVERIFY(w2.waitUntilIdle());
        QVERIFY(w2.stoppedAtSource());
        QCOMPARE(w2.lastStop().frame.line, 20);
        QCOMPARE(w2.lastStop().process, QString("/counter"));
        QCOMPARE(w2.source()->executionLine(), 20);
        w2.deleteAllBreakpoints();
        QVERIFY(w2.waitUntilIdle());
        QCOMPARE(w2.lineBreakpoints().size(), 0);
        w2.sendCommand("run -continue"); // the compat layer resumes a stopped run
        QVERIFY(w2.waitUntilIdle());
        QVERIFY(!w2.stoppedAtSource());
        QVERIFY(!w2.transcript()->text().contains("** Error"));

        // another design (unit) in the same directory has its own list
        QVERIFY(SimWindow::lineBreakpointKey({"work.t"}) != SimWindow::lineBreakpointKey({"work.other"}));
        QCOMPARE(SimWindow::lineBreakpointKey({"-do", "run", "work.t"}),
                 SimWindow::lineBreakpointKey({"work.t"}));
    }

    void aRefusedBreakpointLooksUnverified() {
        AnalysedDesign d;
        PREPARE(d);
        SimWindow w(d.environment(), d.file("refused.ini"));
        w.setDialogsEnabled(false);
        w.resize(1200, 700);
        w.show();
        // a shell whose breakpoint add fails, as the kernel does for a name it cannot use
        QVERIFY(
            w.startSimulation({"-do",
                               "rename breakpoint ::lvd_bp; proc breakpoint {args} {if {[lindex $args 0] eq "
                               "{add}} {error {cannot set a breakpoint here}}; ::lvd_bp {*}$args}",
                               "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.showSource(d.file("t.vhd"), 18));
        QVERIFY(clickGutter(w, 18));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lineBreakpoints().size(), 1);
        QCOMPARE(w.lineBreakpoints()[0].state, LineBreakpoint::State::Rejected);
        QVERIFY2(w.lineBreakpoints()[0].note.contains("cannot set a breakpoint here"),
                 qPrintable(w.lineBreakpoints()[0].note));
        QCOMPARE(w.source()->view()->breakpointMarks().value(18), BreakpointMark::Unverified);
        QVERIFY(w.breakpoints()->table()->item(0, 3)->text().contains("not set"));
        // a click removes it like any other
        QVERIFY(clickGutter(w, 18));
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.lineBreakpoints().size(), 0);
    }

    void stepIntoWithoutAStopRunsToTheFirstStatement() {
        AnalysedDesign d;
        PREPARE(d);
        SimWindow w(d.environment(), d.file("first.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        w.stepStatement(StepKind::Into);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.stoppedAtSource());
        QCOMPARE(w.lastStop().atBreakpoint, false);
        QVERIFY(w.stopText().startsWith("Stopped at t.vhd:"));
        QVERIFY(w.transcript()->text().contains("# Step:"));
        // stopping the simulation while stopped ends it cleanly
        w.stopSimulation();
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.stoppedAtSource());
        QCOMPARE(w.source()->executionLine(), 0);
    }

    void verilogStopWaitsAndContinues() {
        // Change set 36: in nvc -i a Verilog $stop stops the process like a breakpoint (reason "stop").
        const char* tb = "module t;\n"            // 1
                         "  integer count = 0;\n" // 2
                         "  initial begin\n"      // 3
                         "    #10 count = 1;\n"   // 4
                         "    $stop;\n"           // 5
                         "    count = 2;\n"       // 6
                         "    #10 count = 3;\n"   // 7
                         "  end\n"                // 8
                         "endmodule\n";           // 9
        AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY2(d.prepare(tb, "tb.v"), "could not analyse the Verilog test design");
        SimWindow w(d.environment(), d.file("vstop.ini"));
        w.setDialogsEnabled(false);
        w.resize(1200, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());

        QSignalSpy stops(&w, &SimWindow::stoppedAtLine);
        action(w, "runAll")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(stops.count(), 1);
        QVERIFY(w.stoppedAtSource());
        QVERIFY(w.lastStop().reason == StopReason::Stop);
        QVERIFY(!w.lastStop().atBreakpoint);
        QCOMPARE(w.lastStop().nowFs, qint64(10));
        QCOMPARE(w.lastStop().frame.line, 5);
        QCOMPARE(w.stopText(), QString("Stopped by $stop at tb.v:5"));
        QCOMPARE(w.findChild<QLabel*>("stopLabel")->text(), w.stopText());
        QVERIFY(w.source()->filePath().endsWith("tb.v"));
        QCOMPARE(w.source()->executionLine(), 5);
        QVERIFY(w.transcript()->text().contains("# $stop:"));
        QVERIFY(!w.transcript()->text().contains("Unreadable"));
        QVERIFY(action(w, "continue")->isEnabled());
        QVERIFY(!action(w, "run")->isEnabled());
        QCOMPARE(w.callStack()->frameCount(), 1);
        QCOMPARE(w.stackFrames().at(0).kind, QString("process"));

        // Continue resumes after the $stop and the run finishes at 20 fs
        action(w, "continue")->trigger();
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.stoppedAtSource());
        QCOMPARE(w.source()->executionLine(), 0);
        QVERIFY2(w.stopText().startsWith("Finished at 20 fs"), qPrintable(w.stopText()));
        QVERIFY(!action(w, "continue")->isEnabled());
    }
};

QTEST_MAIN(TestDebugSession)
#include "test_debug_session.moc"
