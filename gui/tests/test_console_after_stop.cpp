// SPDX-License-Identifier: GPL-3.0-or-later
// Console commands after a stop (line breakpoint, step, $stop, time breakpoint) with the real nvc: every
// typed command gets its answer and the prompt comes back, also when commands are typed while the GUI's own
// follow-up commands (stack, locals, processes, breakpoint list) are still on their way. Repeated, because
// the failures this guards against depend on how the shell's output is cut into reads.
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"

#include <QAction>
#include <QtTest>

using namespace lvd::gui;
using lvd::gui::testing::AnalysedDesign;

namespace {

const char* kDesign = "entity t is end;\n"                                 // 1
                      "architecture a of t is\n"                           // 2
                      "  signal count : integer := 0;\n"                   // 3
                      "  function twice (x : integer) return integer is\n" // 4
                      "  begin\n"                                          // 5
                      "    return x * 2;\n"                                // 6
                      "  end function;\n"                                  // 7
                      "begin\n"                                            // 8
                      "  counter: process is\n"                            // 9
                      "    variable total : integer := 0;\n"               // 10
                      "  begin\n"                                          // 11
                      "    for i in 1 to 1000 loop\n"                      // 12
                      "      total := total + twice(i);\n"                 // 13
                      "      count <= total mod 1000;\n"                   // 14
                      "      wait for 10 ns;\n"                            // 15
                      "    end loop;\n"                                    // 16
                      "    wait;\n"                                        // 17
                      "  end process;\n"                                   // 18
                      "end;\n";                                            // 19

#define PREPARE(d, src, name)                                                                                \
    if (!(d).nvc.found)                                                                                      \
        QSKIP("nvc not found (set LVDSIM_NVC)");                                                             \
    QVERIFY2((d).prepare(src, name), "could not analyse the test design")

/// Types the usual console commands at once (without waiting for answers) and checks that each one is
/// answered: a marker printed by the last part of every command must reach the transcript.
bool typeAndCheck(SimWindow& w, int round, QString* why) {
    TranscriptWidget* t = w.transcript();
    const QString tag = QStringLiteral("r%1").arg(round);
    const QStringList cmds = {
        QStringLiteral("examine /count; puts %1_examine").arg(tag),
        QStringLiteral("stack; puts %1_stack").arg(tag),
        QStringLiteral("add wave /count"),
        QStringLiteral("set lvd_a 1\nputs %1_multi_$lvd_a").arg(tag), // two lines: sent as one eval
        QStringLiteral("puts %1_plain").arg(tag),
    };
    for (const QString& c : cmds)
        emit t->commandEntered(c);
    // Tab completion on a command name asks the shell for its commands.
    t->completer().invalidateShellCommands();
    t->setCommandText(QStringLiteral("exa"));
    t->completeAtCursor();
    emit t->commandEntered(QStringLiteral("puts %1_after_completion").arg(tag));
    if (!w.waitUntilIdle(15000)) {
        *why = QStringLiteral("round %1: the shell did not come back to its prompt").arg(round);
        return false;
    }
    for (const char* m : {"_examine", "_stack", "_multi_1", "_plain", "_after_completion"}) {
        if (!t->text().contains(tag + QLatin1String(m))) {
            *why = QStringLiteral("round %1: no answer for %2").arg(round).arg(m);
            return false;
        }
    }
    if (t->text().contains(QLatin1String("Unreadable"))) {
        *why = QStringLiteral("round %1: a reply went to the wrong command").arg(round);
        return false;
    }
    return true;
}

} // namespace

class TestConsoleAfterStop : public QObject {
    Q_OBJECT
private slots:
    void lineBreakpointStopsThenTypedCommands() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("s.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.toggleLineBreakpoint(d.file("t.vhd"), 13));
        QVERIFY(w.waitUntilIdle());
        for (int round = 0; round < 15; ++round) {
            w.sendCommand(round == 0 ? "run -all" : "run");
            QString why;
            // Typed while the stop and the GUI's follow-up commands are still on their way.
            QVERIFY2(typeAndCheck(w, round, &why), qPrintable(why));
            QVERIFY2(w.stoppedAtSource(), qPrintable(QString("round %1").arg(round)));
            QCOMPARE(w.lastStop().frame.line, 13);
        }
    }

    void statementStepsThenTypedCommands() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("s.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        for (int round = 0; round < 15; ++round) {
            w.stepStatement(round % 3 == 2 && w.stoppedAtSource() ? StepKind::Over : StepKind::Into);
            QString why;
            QVERIFY2(typeAndCheck(w, round, &why), qPrintable(why));
            QVERIFY(w.stoppedAtSource());
        }
    }

    void verilogStopThenTypedCommands() {
        const char* tb = "module tv;\n"
                         "  integer count = 0;\n"
                         "  initial begin\n"
                         "    repeat (20) begin\n"
                         "      #10 count = count + 1;\n"
                         "      $stop;\n"
                         "    end\n"
                         "  end\n"
                         "endmodule\n";
        AnalysedDesign d;
        PREPARE(d, tb, "tv.v");
        SimWindow w(d.environment(), d.file("v.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.tv"}));
        QVERIFY(w.waitUntilIdle());
        for (int round = 0; round < 15; ++round) {
            if (round == 0)
                w.sendCommand("run -all");
            else
                w.continueRun();
            QString why;
            QVERIFY2(typeAndCheck(w, round, &why), qPrintable(why));
            QVERIFY(w.stoppedAtSource());
            QVERIFY(w.lastStop().reason == StopReason::Stop);
        }
    }

    void stepAfterStopWithoutLineBreakpointsIsRefused() {
        // A run started without line breakpoints runs compiled code: after its $stop a statement step would
        // resume a run that never stops again (here a free-running clock). The kernel refuses it instead.
        const char* tb = "module tc;\n"
                         "  reg clk = 0;\n"
                         "  integer count = 0;\n"
                         "  always #5 clk = ~clk;\n"
                         "  initial begin\n"
                         "    #10 count = 1;\n"
                         "    $stop;\n"
                         "    count = 2;\n"
                         "  end\n"
                         "endmodule\n";
        AnalysedDesign d;
        PREPARE(d, tb, "tc.v");
        SimWindow w(d.environment(), d.file("c.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.tc"}));
        QVERIFY(w.waitUntilIdle());
        w.sendCommand("run -all");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.stoppedAtSource());
        for (StepKind k : {StepKind::Over, StepKind::Into, StepKind::Out}) {
            w.stepStatement(k);
            QVERIFY2(w.waitUntilIdle(15000), "a step after $stop never came back");
            QVERIFY(w.stoppedAtSource());
            QVERIFY2(w.lastShellError().contains("cannot step"), qPrintable(w.lastShellError()));
        }
        QString why;
        QVERIFY2(typeAndCheck(w, 0, &why), qPrintable(why));
        // a line breakpoint set now applies from the next run: say so
        emit w.transcript()->commandEntered("breakpoint add tc.v:8");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("takes effect from the next run"));
    }

    void timeBreakpointAddedWhileStoppedEndsTheRun() {
        // Stopped at a line, the line breakpoint goes and a time breakpoint comes: Continue must stop there
        // (the clock runs for ever, so a missed breakpoint never returns).
        const char* src = "entity tk is end;\n"            // 1
                          "architecture a of tk is\n"      // 2
                          "  signal clk : bit := '0';\n"   // 3
                          "  signal n : natural := 0;\n"   // 4
                          "begin\n"                        // 5
                          "  clk <= not clk after 5 ns;\n" // 6
                          "  p: process (clk) is\n"        // 7
                          "  begin\n"                      // 8
                          "    if clk = '1' then\n"        // 9
                          "      n <= n + 1;\n"            // 10
                          "    end if;\n"                  // 11
                          "  end process;\n"               // 12
                          "end;\n";                        // 13
        AnalysedDesign d;
        PREPARE(d, src, "tk.vhd");
        SimWindow w(d.environment(), d.file("k.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.tk"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.toggleLineBreakpoint(d.file("tk.vhd"), 10));
        QVERIFY(w.waitUntilIdle());
        w.sendCommand("run -all");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.stoppedAtSource());
        w.deleteAllBreakpoints();
        w.addBreakpoint("300", "ns");
        QVERIFY(w.waitUntilIdle());
        w.continueRun();
        QVERIFY2(w.waitUntilIdle(15000), "the resumed run went past the time breakpoint");
        QVERIFY(!w.stoppedAtSource());
        QVERIFY(w.lastStatus() == RunStatus::Breakpoint);
        QVERIFY2(w.stopText().contains("300 ns"), qPrintable(w.stopText()));
    }

    void programNamesDoNotEatTheCommandStream() {
        // Tcl runs an unknown command word as a program when the shell is interactive; that program would
        // read the shell's input (the GUI's commands) and the shell would never answer again.
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("p.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        emit w.transcript()->commandEntered("sort");
        emit w.transcript()->commandEntered("puts after_program_name");
        QVERIFY2(w.waitUntilIdle(10000), "the shell did not answer after a program name");
        QVERIFY(w.transcript()->text().contains("after_program_name"));
        QVERIFY(w.transcript()->text().contains("invalid command name \"sort\""));
    }

    void aCommandTypedWhileRunningGetsANote() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("n.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        const QString note = QStringLiteral("typed commands wait until the run");
        emit w.transcript()->commandEntered("puts quick"); // answered at once: no note
        QVERIFY(w.waitUntilIdle());
        QTest::qWait(1800);
        QVERIFY(!w.transcript()->text().contains(note));
        w.sendCommand("after 3000"); // the shell is busy for three seconds
        emit w.transcript()->commandEntered("puts waited");
        QTRY_VERIFY_WITH_TIMEOUT(w.transcript()->text().contains(note), 2800);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("waited"));
        QCOMPARE(w.transcript()->text().count(note), 1);
    }

    void timeBreakpointsThenTypedCommands() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("s.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        for (int round = 0; round < 15; ++round) {
            w.addBreakpoint(QString::number(25 * (round + 1)), "ns");
            QVERIFY(w.waitUntilIdle());
            w.sendCommand("run -all");
            QString why;
            QVERIFY2(typeAndCheck(w, round, &why), qPrintable(why));
            QVERIFY(w.lastStatus() == RunStatus::Breakpoint);
        }
    }
};

QTEST_MAIN(TestConsoleAfterStop)
#include "test_console_after_stop.moc"
