// SPDX-License-Identifier: GPL-3.0-or-later
// Break and the exit dialog with a real nvc: Simulation > Break ends a run that never ends on its own (status
// "interrupted"), the run goes on from there, commands typed meanwhile run; Break at the prompt does nothing;
// a simulator that does not exit (or does not end its run) is waited for or killed as the wait policy says.
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"

#include <QAction>
#include <QElapsedTimer>
#include <QLabel>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace lvd::gui;
using lvd::gui::testing::AnalysedDesign;

namespace {

// A clock that never stops: run -all only ends by a break.
const char* kClock = "entity t is end;\n"
                     "architecture a of t is\n"
                     "  signal clk : bit := '0';\n"
                     "  signal count : natural := 0;\n"
                     "begin\n"
                     "  clk <= not clk after 5 ns;\n"
                     "  counter: process (clk) is\n"
                     "  begin\n"
                     "    if clk = '1' then\n"
                     "      count <= count + 1;\n"
                     "    end if;\n"
                     "  end process;\n"
                     "end;\n";

// At 10 ns a process loops for ever without a wait: the run never reaches its next time step.
const char* kLoop = "entity t is end;\n"
                    "architecture a of t is\n"
                    "  signal n : natural := 0;\n"
                    "begin\n"
                    "  spin: process is\n"
                    "    variable v : natural := 0;\n"
                    "  begin\n"
                    "    wait for 10 ns;\n"
                    "    loop\n"
                    "      v := (v + 1) mod 1000;\n"
                    "    end loop;\n"
                    "  end process;\n"
                    "end;\n";

QAction* action(SimWindow& w, const char* name) {
    return w.findChild<QAction*>(name);
}

/// Spins the event loop for a while (the simulator keeps running).
void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QTest::qWait(10);
}

struct Started {
    AnalysedDesign d;
    std::unique_ptr<SimWindow> w;

    bool start(const char* design) {
        if (!d.prepare(design))
            return false;
        w = std::make_unique<SimWindow>(d.environment(), d.file("session.ini"));
        w->setDialogsEnabled(false);
        w->show();
        return w->startSimulation({"work.t"}) && w->waitUntilIdle();
    }
};

#define START(s, design)                                                                                     \
    if (!(s).d.nvc.found)                                                                                    \
        QSKIP("nvc not found (set LVDSIM_NVC)");                                                             \
    QVERIFY2((s).start(design), "could not start the test design")

void breakDuringRunAll(SimWindow& w) {
    QSignalSpy replies(&w, &SimWindow::stopped);
    QVERIFY(!action(w, "break")->isEnabled()); // nothing to break at the prompt
    action(w, "runAll")->trigger();
    QVERIFY(w.session().busy());
    w.sendCommand("echo queued_while_running"); // waits in the shell's input
    spin(400);
    QVERIFY(w.session().busy()); // the clock never stops
    QVERIFY(action(w, "break")->isEnabled());

    QElapsedTimer t;
    t.start();
    action(w, "break")->trigger();
    QVERIFY(w.waitUntilIdle(10000));
    QVERIFY2(t.elapsed() < 5000, qPrintable(QString("break took %1 ms").arg(t.elapsed())));
    QVERIFY(w.lastStatus() == RunStatus::Interrupted);
    QVERIFY(!replies.isEmpty());
    const RunReply interrupted = replies.last().at(0).value<RunReply>();
    QVERIFY(interrupted.status == RunStatus::Interrupted);
    QVERIFY(interrupted.nowFs > 0);
    QCOMPARE(w.stopText(), QString("Interrupted at %1").arg(formatFemtoseconds(interrupted.nowFs)));
    QCOMPARE(w.findChild<QLabel*>("stopLabel")->text(), w.stopText());
    QVERIFY(w.transcript()->text().contains("# Interrupted at"));
    QVERIFY(w.transcript()->text().contains("queued_while_running"));
    QVERIFY(!w.transcript()->text().contains("Unreadable reply"));
    QVERIFY(!action(w, "break")->isEnabled());

    // the run goes on from the time of the interrupt
    w.sendCommand("run 100 ns");
    QVERIFY(w.waitUntilIdle());
    QVERIFY(w.lastStatus() == RunStatus::Paused);
    QCOMPARE(replies.last().at(0).value<RunReply>().nowFs, interrupted.nowFs + 100000000);
    QVERIFY(w.session().running());
}

} // namespace

class TestBreakRun : public QObject {
    Q_OBJECT
private slots:
    void breakEndsRunAllAndTheSimulationGoesOn() {
        Started s;
        START(s, kClock);
        breakDuringRunAll(*s.w);
    }

    void breakAtThePromptDoesNothing() {
        Started s;
        START(s, kClock);
        SimWindow& w = *s.w;
        QVERIFY(!w.breakRun()); // the GUI does not send it
        // sent anyway (the run just ended, for example): the shell ignores it
        const auto r = w.session().interrupt();
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QVERIFY(!w.session().interruptPending());
        spin(1000);
        QVERIFY(w.session().running());
        w.sendCommand("run 10 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.lastStatus() == RunStatus::Paused);
    }

    void exitTimeoutAsksWaitThenKill() {
        Started s;
        START(s, kClock);
        SimWindow& w = *s.w;
        // A Tcl command that takes 20 s: the shell reads `exit` after it, and a break does not end it.
        w.sendCommand("after 20000");
        QVERIFY(w.session().busy());
        QVector<int> waited;
        w.session().setWaitPolicy([&](NvcSession::Unresponsive what, int ms) {
            if (what != NvcSession::Unresponsive::Exit)
                return NvcSession::WaitDecision::Wait;
            waited.push_back(ms);
            return waited.size() < 2 ? NvcSession::WaitDecision::Wait : NvcSession::WaitDecision::Kill;
        });
        QElapsedTimer t;
        t.start();
        w.session().stop(400);
        QVERIFY(t.elapsed() < 5000);
        QCOMPARE(waited.size(), 2);
        QVERIFY(waited[0] >= 400 && waited[1] >= 800);
        QVERIFY(!w.session().running());
    }

    void windowCloseUsesTheWaitPolicy() {
        Started s;
        START(s, kClock);
        SimWindow& w = *s.w;
        w.sendCommand("after 20000");
        int asked = 0;
        w.session().setWaitPolicy([&](NvcSession::Unresponsive, int) {
            ++asked;
            return NvcSession::WaitDecision::Kill;
        });
        QElapsedTimer t;
        t.start();
        w.close();
        QCOMPARE(asked, 1);
        QVERIFY(t.elapsed() < 10000);
        QVERIFY(!w.session().running());
    }

    void withoutAPolicyStopKills() {
        Started s;
        START(s, kClock);
        SimWindow& w = *s.w;
        w.sendCommand("after 20000");
        w.session().setWaitPolicy({});
        QElapsedTimer t;
        t.start();
        w.session().stop(300);
        QVERIFY(t.elapsed() < 5000);
        QVERIFY(!w.session().running());
    }

    void stopInterruptsARunFirst() {
        Started s;
        START(s, kClock);
        SimWindow& w = *s.w;
        action(w, "runAll")->trigger();
        spin(300);
        int asked = 0;
        w.session().setWaitPolicy([&](NvcSession::Unresponsive, int) {
            ++asked;
            return NvcSession::WaitDecision::Kill;
        });
        QSignalSpy finished(&w.session(), &NvcSession::finished);
        action(w, "stop")->trigger();
        QCOMPARE(asked, 0); // it exited on its own, after the break
        QVERIFY(!w.session().running());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toInt(), 0);
    }

    void secondBreakOffersToKillALoopingProcess() {
        Started s;
        START(s, kLoop);
        SimWindow& w = *s.w;
        action(w, "runAll")->trigger();
        spin(300);
        QVERIFY(w.breakRun());
        spin(700);
        QVERIFY(w.session().busy()); // the loop never returns to the kernel
        QVERIFY(w.session().interruptPending());
        QVERIFY(action(w, "break")->isEnabled());
        QVector<NvcSession::Unresponsive> asked;
        w.session().setWaitPolicy([&](NvcSession::Unresponsive what, int ms) {
            asked.push_back(what);
            return ms >= 500 ? NvcSession::WaitDecision::Kill : NvcSession::WaitDecision::Wait;
        });
        QVERIFY(w.breakRun());
        QCOMPARE(asked.size(), 1);
        QVERIFY(asked[0] == NvcSession::Unresponsive::Break);
        QTRY_VERIFY_WITH_TIMEOUT(!w.session().running(), 5000);
        QVERIFY(w.transcript()->text().contains("killed"));
    }

#ifdef Q_OS_WIN
    // Last: without a console, as the GUI started from the Start menu. The child then gets a console of its
    // own and the break is sent by attaching to it for a moment.
    void breakWithoutAConsole() {
        FreeConsole();
        Started s;
        START(s, kClock);
        breakDuringRunAll(*s.w);
    }
#endif
};

QTEST_MAIN(TestBreakRun)
#include "test_break_run.moc"
