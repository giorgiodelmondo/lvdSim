// SPDX-License-Identifier: GPL-3.0-or-later
// The console with a real nvc shell: command names from `info commands`, design paths, multi-line commands,
// links to the source and dropped do files.
#include "app/console_input.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTextBlock>
#include <QUrl>
#include <QtTest>

using namespace lvd::gui;
using lvd::gui::testing::AnalysedDesign;

namespace {

const char* kDesign = "entity sub is\n"                                            // 1
                      "  port (a : in bit; y : out bit);\n"                        // 2
                      "end;\n"                                                     // 3
                      "architecture a of sub is begin y <= not a; end;\n"          // 4
                      "entity t is end;\n"                                         // 5
                      "architecture a of t is\n"                                   // 6
                      "  signal clk : bit := '0';\n"                               // 7
                      "  signal cnt : integer := 0;\n"                             // 8
                      "  signal count_en : bit := '1';\n"                          // 9
                      "  signal y : bit;\n"                                        // 10
                      "begin\n"                                                    // 11
                      "  clk <= not clk after 5 ns;\n"                             // 12
                      "  u_sub : entity work.sub port map (clk, y);\n"             // 13
                      "  process (clk) begin\n"                                    // 14
                      "    if clk = '1' then\n"                                    // 15
                      "      cnt <= cnt + 1;\n"                                    // 16
                      "      assert cnt /= 3 report \"three\" severity warning;\n" // 17
                      "    end if;\n"                                              // 18
                      "  end process;\n"                                           // 19
                      "end;\n";                                                    // 20

#define PREPARE(d)                                                                                           \
    if (!(d).nvc.found)                                                                                      \
        QSKIP("nvc not found (set LVDSIM_NVC)");                                                             \
    QVERIFY2((d).prepare(kDesign), "could not analyse the test design")

void type(SimWindow& w, const QString& text, bool enter = true) {
    QTest::keyClicks(w.transcript()->input(), text);
    if (enter)
        QTest::keyClick(w.transcript()->input(), Qt::Key_Return);
}

struct Session {
    AnalysedDesign d;
    std::unique_ptr<SimWindow> w;

    bool start() {
        w = std::make_unique<SimWindow>(d.environment(), d.file("session.ini"));
        w->setDialogsEnabled(false);
        w->resize(1200, 800);
        w->show();
        if (!QTest::qWaitForWindowExposed(w.get()))
            return false;
        return w->startSimulation({"work.t"}) && w->waitUntilIdle();
    }
};

} // namespace

class TestConsoleSession : public QObject {
    Q_OBJECT
private slots:
    void commandNamesComeFromTheShell() {
        Session s;
        PREPARE(s.d);
        QVERIFY(s.start());
        SimWindow& w = *s.w;
        TranscriptWidget* t = w.transcript();
        QVERIFY(!t->completer().hasShellCommands()); // asked only when a command name is completed

        type(w, "proc lvdTestAnswer {} { return 42 }");
        QVERIFY(w.waitUntilIdle());
        type(w, "lvdTestAn", false);
        QTest::keyClick(t->input(), Qt::Key_Tab);
        QVERIFY(w.waitUntilIdle()); // the shell answers `info commands`
        QCoreApplication::processEvents();
        QVERIFY(t->completer().hasShellCommands());
        QCOMPARE(t->input()->text(), QString("lvdTestAnswer "));
        const QStringList cmds = t->completer().commands();
        for (const char* c : {"examine", "run", "force", "quietly", "lvdTestAnswer", "lsort"})
            QVERIFY2(cmds.contains(c), c);
        QVERIFY(!t->text().contains("@@lvd commands ")); // the answer is not shown (only the query is echoed)
        QTest::keyClick(t->input(), Qt::Key_Return);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(t->text().contains("\n42\n") || t->text().contains("% 42\n"));
    }

    void pathsComeFromTheDesign() {
        Session s;
        PREPARE(s.d);
        QVERIFY(s.start());
        SimWindow& w = *s.w;
        TranscriptWidget* t = w.transcript();
        type(w, "run 20 ns");
        QVERIFY(w.waitUntilIdle());

        type(w, "examine /co", false);
        QTest::keyClick(t->input(), Qt::Key_Tab);
        QCOMPARE(t->input()->text(), QString("examine /count_en "));
        type(w, "/u_s", false);
        QTest::keyClick(t->input(), Qt::Key_Tab);
        QCOMPARE(t->input()->text(), QString("examine /count_en /u_sub/"));
        QTest::keyClick(t->input(), Qt::Key_Tab); // a and y: the popup
        QVERIFY(t->completionPopupVisible());
        QCOMPARE(t->completionLabels(), (QStringList{"a", "y"}));
        QTest::keyClick(t->completionPopupCompleter()->popup(), Qt::Key_Return);
        QCOMPARE(t->input()->text(), QString("examine /count_en /u_sub/a "));
        QTest::keyClick(t->input(), Qt::Key_Return);
        QVERIFY(w.waitUntilIdle());
        // the shell knows both names the completion made
        QVERIFY2(!t->text().contains("cannot find name"), qPrintable(t->text()));
        QVERIFY(t->text().contains("'1' '"));
    }

    void multiLineCommandsRunInTheShell() {
        Session s;
        PREPARE(s.d);
        QVERIFY(s.start());
        SimWindow& w = *s.w;
        TranscriptWidget* t = w.transcript();
        type(w, "proc twice {x} {");
        type(w, "  # a comment: $x and [brackets] reach the shell unchanged");
        type(w, "  return [expr {2 * $x}]");
        QCOMPARE(t->input()->document()->blockCount(), 4); // still being typed
        type(w, "}");
        QVERIFY(t->input()->text().isEmpty());
        QVERIFY(w.waitUntilIdle());
        type(w, "puts \"twice=[twice 21]\"");
        QVERIFY(w.waitUntilIdle());
        QVERIFY2(t->text().contains("twice=42"), qPrintable(t->text()));

        // pasted lines are separate commands: run keeps its -json reply (the GUI follows the time)
        t->input()->setText("set lvdA 5\nputs \"a=$lvdA\"\nrun 30 ns");
        QTest::keyClick(t->input(), Qt::Key_Return);
        QVERIFY(w.waitUntilIdle());
        QVERIFY(t->text().contains("a=5"));
        QVERIFY(t->text().contains("run -json 30 ns"));
        QCOMPARE(w.store()->endTime(), lvd::wave::Time{30000000});
        QVERIFY(!t->text().contains("Unreadable reply"));
        QCOMPARE(t->history().last(), QString("set lvdA 5\nputs \"a=$lvdA\"\nrun 30 ns"));
    }

    void linksOpenTheSourceAndDroppedScriptsRun() {
        Session s;
        PREPARE(s.d);
        QVERIFY(s.start());
        SimWindow& w = *s.w;
        TranscriptWidget* t = w.transcript();
        type(w, "run 50 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(t->text().contains("** Warning"));

        // the process line under the report: "... at t.vhd:14"
        QPlainTextEdit* v = t->textView();
        QTextBlock b = v->document()->firstBlock();
        while (b.isValid() && !b.text().contains("t.vhd:14"))
            b = b.next();
        QVERIFY2(b.isValid(), qPrintable(t->text()));
        v->setTextCursor(QTextCursor(b));
        v->ensureCursorVisible();
        QTextCursor c(b);
        c.setPosition(b.position() + static_cast<int>(b.text().indexOf("t.vhd:14")) + 2);
        const QPoint at = v->cursorRect(c).center();
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at);
        QVERIFY(w.source()->filePath().endsWith("t.vhd"));
        QCOMPARE(w.source()->view()->textCursor().blockNumber() + 1, 14);

        // a do file dropped on the console runs; it is in the recent list and in the history
        {
            QFile f(s.d.file("drop.do"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("puts \"dropped script ran\"\nexamine /nosuch\n");
        }
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(s.d.file("drop.do"))});
        QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QVERIFY2(t->history().last().startsWith("do "), qPrintable(t->history().join('|')));
        QVERIFY(w.waitUntilIdle());
        QVERIFY2(t->text().contains("dropped script ran"), qPrintable(t->text()));
        QVERIFY(w.recentDoFiles().value(0).endsWith("drop.do"));
        QVERIFY(t->history().last().startsWith("do "));
        // the error trace names the script and its line: a link
        QSignalSpy spy(t, &TranscriptWidget::sourceActivated);
        b = v->document()->lastBlock();
        while (b.isValid() && !b.text().contains("line 2)"))
            b = b.previous();
        QVERIFY2(b.isValid(), qPrintable(t->text()));
        v->setTextCursor(QTextCursor(b));
        v->ensureCursorVisible();
        QTextCursor c2(b);
        c2.setPosition(b.position() + static_cast<int>(b.text().indexOf("drop.do")) + 2);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->cursorRect(c2).center());
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toString().endsWith("drop.do"));
        QCOMPARE(spy.at(0).at(1).toInt(), 2);
        QVERIFY(w.source()->filePath().endsWith("drop.do"));
    }
};

QTEST_MAIN(TestConsoleSession)
#include "test_console_session.moc"
