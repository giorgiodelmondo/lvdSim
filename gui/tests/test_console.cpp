// SPDX-License-Identifier: GPL-3.0-or-later
// The console widget: multi-line input, history (Up/Down, Ctrl+R, dialog, persistence), Tab completion with
// its popup, coloured lines, source links, find, save, copy without prompts, the line cap and dropped do
// files.
#include "app/console_input.hpp"
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"

#include <lvdwave/store.hpp>

#include <QAbstractItemView>
#include <QAction>
#include <QClipboard>
#include <QCompleter>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QUrl>
#include <QtTest>

#include <environment.hpp>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

bool touch(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly);
}

std::unique_ptr<WaveStore> makeStore() {
    auto st = std::make_unique<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "t");
    st->addSignal(top, "clk", SignalKind::Logic, 1);
    st->addSignal(top, "cnt", SignalKind::Logic, 32);
    st->addSignal(top, "count_en", SignalKind::Logic, 1);
    st->addSignal(st->addScope(top, "u_sub"), "a", SignalKind::Logic, 1);
    st->addSignal(st->addScope(top, "u_sub2"), "z", SignalKind::Logic, 1);
    return st;
}

QPoint charPos(QPlainTextEdit* view, int block, int ch) {
    const QTextBlock b = view->document()->findBlockByNumber(block);
    QTextCursor c(b);
    c.setPosition(b.position() + ch);
    const QRect r = view->cursorRect(c);
    return QPoint(r.left() + 3, r.center().y());
}

QTextCharFormat formatAt(QPlainTextEdit* view, int block, int ch) {
    const QTextBlock b = view->document()->findBlockByNumber(block);
    QTextCursor c(b);
    c.setPosition(b.position() + ch + 1); // the format of the character before the position
    return c.charFormat();
}

struct Shown {
    TranscriptWidget t;
    Shown() {
        t.resize(700, 300);
        t.show();
        (void)QTest::qWaitForWindowExposed(&t);
        t.activateWindow();
        (void)QTest::qWaitForWindowActive(&t);
        t.input()->setFocus();
    }
};

} // namespace

class TestConsole : public QObject {
    Q_OBJECT
private slots:
    // ---- multi-line input ---------------------------------------------------------------------------------
    void unfinishedCommandsContinueOnTheNextLine() {
        Shown s;
        ConsoleInput* in = s.t.input();
        QSignalSpy spy(&s.t, &TranscriptWidget::commandEntered);
        const int oneLine = in->height();
        QTest::keyClicks(in, "proc greet {who} {");
        QTest::keyClick(in, Qt::Key_Return);
        QCOMPARE(spy.count(), 0); // braces open: not sent
        QCOMPARE(in->document()->blockCount(), 2);
        QTest::keyClicks(in, "  puts \"hello $who\"");
        QTest::keyClick(in, Qt::Key_Return);
        QCOMPARE(spy.count(), 0);
        QVERIFY(in->height() > oneLine); // the line grows with the command
        QTest::keyClicks(in, "}");
        QTest::keyClick(in, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("proc greet {who} {\n  puts \"hello $who\"\n}"));
        QVERIFY(in->text().isEmpty());
        QCOMPARE(in->height(), oneLine);
        QCOMPARE(s.t.history().last(), QString("proc greet {who} {\n  puts \"hello $who\"\n}"));
    }

    void shiftEnterInsertsANewline() {
        Shown s;
        ConsoleInput* in = s.t.input();
        QSignalSpy spy(&s.t, &TranscriptWidget::commandEntered);
        QTest::keyClicks(in, "run 10 ns");
        QTest::keyClick(in, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(in, "examine /cnt");
        QCOMPARE(spy.count(), 0);
        QTest::keyClick(in, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("run 10 ns\nexamine /cnt"));
        QVERIFY(in->promptWidth() > 0);
    }

    void upAndDownWalkTheHistoryAndMultiLineEntries() {
        Shown s;
        ConsoleInput* in = s.t.input();
        s.t.setHistory({"run 10 ns", "proc f {} {\n  return 1\n}"});
        QTest::keyClicks(in, "draft");
        QTest::keyClick(in, Qt::Key_Up);
        QCOMPARE(in->text(), QString("proc f {} {\n  return 1\n}"));
        // the cursor is at the end: Up moves inside the command until its first line
        QTest::keyClick(in, Qt::Key_Up);
        QTest::keyClick(in, Qt::Key_Up);
        QCOMPARE(in->text(), QString("proc f {} {\n  return 1\n}"));
        QCOMPARE(in->textCursor().blockNumber(), 0);
        QTest::keyClick(in, Qt::Key_Up);
        QCOMPARE(in->text(), QString("run 10 ns"));
        QTest::keyClick(in, Qt::Key_Down);
        QTest::keyClick(in, Qt::Key_Down);
        QCOMPARE(in->text(), QString("draft"));
    }

    // ---- history search and dialog ------------------------------------------------------------------------
    void ctrlRSearchesTheHistory() {
        Shown s;
        ConsoleInput* in = s.t.input();
        s.t.setHistory({"run 10 ns", "examine /cnt", "add wave /cnt", "run -all"});
        QSignalSpy spy(&s.t, &TranscriptWidget::commandEntered);
        QTest::keyClicks(in, "typed");
        QTest::keyClick(in, Qt::Key_R, Qt::ControlModifier);
        QVERIFY(s.t.historySearchBar()->isVisible());
        QLineEdit* search = s.t.historySearchEdit();
        QTest::keyClicks(search, "cnt");
        QCOMPARE(in->text(), QString("add wave /cnt"));
        QTest::keyClick(search, Qt::Key_R, Qt::ControlModifier); // older
        QCOMPARE(in->text(), QString("examine /cnt"));
        QTest::keyClick(search, Qt::Key_R, Qt::ControlModifier); // none older: stays
        QCOMPARE(in->text(), QString("examine /cnt"));
        QTest::keyClick(search, Qt::Key_Escape); // cancel: what was typed comes back
        QVERIFY(!s.t.historySearchBar()->isVisible());
        QCOMPARE(in->text(), QString("typed"));
        QCOMPARE(spy.count(), 0);

        QTest::keyClick(in, Qt::Key_R, Qt::ControlModifier);
        QTest::keyClicks(search, "10");
        QTest::keyClick(search, Qt::Key_Return); // runs the match
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("run 10 ns"));
        QCOMPARE(s.t.history().last(), QString("run 10 ns"));

        QTest::keyClick(in, Qt::Key_R, Qt::ControlModifier);
        QTest::keyClicks(search, "wave");
        QTest::keyClick(search, Qt::Key_Right); // take it for editing
        QCOMPARE(in->text(), QString("add wave /cnt"));
        QCOMPARE(spy.count(), 1);
    }

    void historyDialogInsertsRunsAndClears() {
        Shown s;
        s.t.setHistory({"run 10 ns", "examine /cnt", "proc f {} {\n}"});
        QSignalSpy entered(&s.t, &TranscriptWidget::commandEntered);
        QSignalSpy changed(&s.t, &TranscriptWidget::historyChanged);
        ConsoleHistoryDialog* d = s.t.showHistoryDialog();
        QVERIFY(d->isVisible());
        QCOMPARE(d->shown(), (QStringList{"proc f {} {\n}", "examine /cnt", "run 10 ns"})); // newest first
        QVERIFY(d->list()->item(0)->text().contains("(2 lines)"));
        d->filterEdit()->setText("EXA");
        QCOMPARE(d->shown(), QStringList{"examine /cnt"});
        d->findChild<QPushButton*>("insert")->click();
        QCOMPARE(s.t.input()->text(), QString("examine /cnt"));
        QCOMPARE(entered.count(), 0);
        d->filterEdit()->clear();
        d->list()->setCurrentRow(2);
        d->findChild<QPushButton*>("run")->click();
        QCOMPARE(entered.count(), 1);
        QCOMPARE(entered.at(0).at(0).toString(), QString("run 10 ns"));
        QCOMPARE(d->shown().first(), QString("run 10 ns")); // the dialog follows the history
        d->findChild<QPushButton*>("clear")->click();
        QVERIFY(s.t.history().isEmpty());
        QVERIFY(changed.count() >= 2);
        QVERIFY(d->shown().isEmpty());
    }

    void historyIsKeptPerUserInTheSettings() {
        QTemporaryDir dir;
        const QString ini = dir.filePath("gui.ini");
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(SimWindow::kHistoryLimitKey, 3);
        }
        {
            SimWindow w(lvd::tools::discoverEnvironment(), ini);
            w.setDialogsEnabled(false);
            for (const char* c : {"one", "two", "three", "four"})
                w.transcript()->submitCommand(c); // no simulation: the commands only go into the history
            w.transcript()->submitCommand("multi {\n  line\n}");
        }
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value("console/history").toStringList(),
                 (QStringList{"three", "four", "multi {\n  line\n}"}));
        SimWindow w2(lvd::tools::discoverEnvironment(), ini);
        QCOMPARE(w2.transcript()->history(), (QStringList{"three", "four", "multi {\n  line\n}"}));
        QTest::keyClick(w2.transcript()->input(), Qt::Key_Up);
        QCOMPARE(w2.transcript()->input()->text(), QString("multi {\n  line\n}"));
    }

    // ---- completion ---------------------------------------------------------------------------------------
    void tabCompletesAUniqueCandidate() {
        Shown s;
        auto st = makeStore();
        s.t.completer().setStore(st.get());
        ConsoleInput* in = s.t.input();
        QTest::keyClicks(in, "examine /co");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(in->text(), QString("examine /count_en "));
        QVERIFY(!s.t.completionPopupVisible());
        in->clear();
        QTest::keyClicks(in, "add wave /u_sub2");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(in->text(), QString("add wave /u_sub2/")); // a scope: no blank, Tab goes on inside it
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(in->text(), QString("add wave /u_sub2/z "));
        in->clear();
        QTest::keyClicks(in, "run -a");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(in->text(), QString("run -all "));
    }

    void severalCandidatesOpenThePopup() {
        Shown s;
        auto st = makeStore();
        s.t.completer().setStore(st.get());
        ConsoleInput* in = s.t.input();
        QTest::keyClicks(in, "add wave /u");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(in->text(), QString("add wave /u_sub")); // the common part
        QVERIFY(s.t.completionPopupVisible());
        QCOMPARE(s.t.completionLabels(), (QStringList{"u_sub/", "u_sub2/"}));

        in->clear(); // the popup goes with the word
        QVERIFY(!s.t.completionPopupVisible());
        QTest::keyClicks(in, "examine /c");
        QTest::keyClick(in, Qt::Key_Tab);
        QVERIFY(s.t.completionPopupVisible());
        QCOMPARE(s.t.completionLabels(), (QStringList{"clk", "cnt", "count_en"}));
        QAbstractItemView* popup = s.t.completionPopupCompleter()->popup();
        // typing goes on narrowing the list (keys reach the command line through the popup)
        QTest::keyClicks(popup, "o");
        QCOMPARE(in->text(), QString("examine /co"));
        QCOMPARE(s.t.completionLabels(), QStringList{"count_en"});
        QTest::keyClick(popup, Qt::Key_Return); // takes the selected candidate
        QCOMPARE(in->text(), QString("examine /count_en "));
        QVERIFY(!s.t.completionPopupVisible());

        in->clear();
        QTest::keyClicks(in, "examine /c");
        QTest::keyClick(in, Qt::Key_Tab);
        QTest::keyClick(popup, Qt::Key_Down);
        QTest::keyClick(popup, Qt::Key_Tab);
        QCOMPARE(in->text(), QString("examine /cnt "));
        QTest::keyClicks(in, "/c");
        QTest::keyClick(in, Qt::Key_Tab);
        QVERIFY(s.t.completionPopupVisible());
        QTest::keyClick(popup, Qt::Key_Escape);
        QVERIFY(!s.t.completionPopupVisible());
        QCOMPARE(in->text(), QString("examine /cnt /c"));
    }

    void commandNamesAskTheShellOnce() {
        Shown s;
        ConsoleInput* in = s.t.input();
        QSignalSpy wanted(&s.t, &TranscriptWidget::shellCommandsWanted);
        QTest::keyClicks(in, "myP");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(wanted.count(), 1);
        QCOMPARE(in->text(), QString("myP")); // not known yet
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(wanted.count(), 1);             // already asked
        s.t.setShellCommands({"myProc", "run"}); // the answer completes the waiting Tab
        QCOMPARE(in->text(), QString("myProc "));
        in->clear();
        QTest::keyClicks(in, "myP");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(wanted.count(), 1);
        QCOMPARE(in->text(), QString("myProc "));
        // a proc may add commands: the next command completion asks again
        s.t.submitCommand("proc other {} {}");
        QTest::keyClicks(in, "oth");
        QTest::keyClick(in, Qt::Key_Tab);
        QCOMPARE(wanted.count(), 2);
        // an answer for a line that changed meanwhile does not touch it
        QTest::keyClicks(in, "x");
        s.t.setShellCommands({"other"});
        QCOMPARE(in->text(), QString("othx"));
    }

    // ---- transcript ---------------------------------------------------------------------------------------
    void linesAreColouredBySeverity() {
        Shown s;
        s.t.append("% run 10 ns\n** Error: invalid command name \"foo\"\n** Warning: (lvdsim) w\n"
                   "** Note: 5ns+0: note text\nplain line\n");
        QPlainTextEdit* v = s.t.textView();
        const QBrush def = formatAt(v, 4, 2).foreground();
        const QBrush err = formatAt(v, 1, 20).foreground();
        const QBrush warn = formatAt(v, 2, 18).foreground();
        const QBrush note = formatAt(v, 3, 20).foreground();
        QVERIFY(err != def);
        QVERIFY(warn != def);
        QVERIFY(note != def);
        QVERIFY(err != warn && warn != note && err != note);
        QCOMPARE(formatAt(v, 0, 3).foreground(), def); // the command
        QVERIFY(formatAt(v, 3, 10).fontUnderline());   // the report time stays a link
        QCOMPARE(s.t.text(),
                 QString("% run 10 ns\n** Error: invalid command name \"foo\"\n** Warning: (lvdsim) w\n"
                         "** Note: 5ns+0: note text\nplain line\n"));
    }

    void fileReferencesOpenTheSource() {
        Shown s;
        QSignalSpy spy(&s.t, &TranscriptWidget::sourceActivated);
        s.t.append("** Note: 35ns+0: counter passed three\n   Process :t:_p1 at t.vhd:17\n"
                   "    (file \"D:/x/run.do\" line 3)\n");
        QPlainTextEdit* v = s.t.textView();
        QVERIFY(formatAt(v, 1, 23).fontUnderline());
        QVERIFY(!formatAt(v, 1, 5).fontUnderline());
        const auto ref = s.t.sourceLinkAt(charPos(v, 1, 23));
        QVERIFY(ref.has_value());
        QCOMPARE(ref->first, QString("t.vhd"));
        QCOMPARE(ref->second, 17);
        QTest::mouseMove(v->viewport(), charPos(v, 1, 23));
        QCOMPARE(v->viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, charPos(v, 1, 23));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("t.vhd"));
        QCOMPARE(spy.at(0).at(1).toInt(), 17);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, charPos(v, 2, 15));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QString("D:/x/run.do"));
        QCOMPARE(spy.at(1).at(1).toInt(), 3);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, charPos(v, 1, 5)); // not a link
        QCOMPARE(spy.count(), 2);
    }

    void findWrapsAndReports() {
        Shown s;
        s.t.append("% run\nalpha\nbeta\nAlpha again\n");
        QVERIFY(!s.t.findBar()->isVisible());
        s.t.activateWindow();
        s.t.textView()->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&s.t));
        QTest::keyClick(s.t.textView(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(s.t.findBar()->isVisible());
        QVERIFY(s.t.find("alpha"));
        QCOMPARE(s.t.textView()->textCursor().blockNumber(), 1);
        QVERIFY(s.t.find("alpha")); // not case-sensitive by default
        QCOMPARE(s.t.textView()->textCursor().blockNumber(), 3);
        QVERIFY(s.t.find("alpha")); // wraps to the start
        QCOMPARE(s.t.textView()->textCursor().blockNumber(), 1);
        QVERIFY(s.t.find("alpha", true)); // backwards wraps to the end
        QCOMPARE(s.t.textView()->textCursor().blockNumber(), 3);
        QVERIFY(!s.t.find("gamma"));
        QCOMPARE(s.t.textView()->textCursor().blockNumber(), 3); // stays
        QTest::keyClicks(s.t.findEdit(), "beta");
        QTest::keyClick(s.t.findEdit(), Qt::Key_Return);
        QCOMPARE(s.t.textView()->textCursor().selectedText(), QString("beta"));
        QTest::keyClick(s.t.findEdit(), Qt::Key_Escape);
        QVERIFY(!s.t.findBar()->isVisible());
    }

    void saveClearAndCopyWithoutPrompts() {
        Shown s;
        s.t.append("% run 10 ns\n** Note: 5ns+0: x\n% examine /cnt\n4\n% ");
        QCOMPARE(s.t.textWithoutPrompts(), QString("run 10 ns\n** Note: 5ns+0: x\nexamine /cnt\n4\n"));
        QTemporaryDir dir;
        const QString f = dir.filePath("t.txt");
        QVERIFY(s.t.saveTranscript(f));
        QFile in(f);
        QVERIFY(in.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(in.readAll()), s.t.text());
        QVERIFY(!s.t.saveTranscript(dir.filePath("no/such/dir/t.txt")));

        // a selection: only that, without prompts
        QTextCursor c(s.t.textView()->document()->findBlockByNumber(2));
        c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        s.t.textView()->setTextCursor(c);
        QCOMPARE(s.t.textWithoutPrompts(true), QString("examine /cnt\n4"));
        s.t.copyWithoutPromptsAction()->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("examine /cnt\n4"));

        s.t.clearAction()->trigger();
        QVERIFY(s.t.text().isEmpty());
    }

    void theTranscriptKeepsTheNewestLines() {
        Shown s;
        s.t.setMaximumLines(100);
        QCOMPARE(s.t.maximumLines(), 100);
        QSignalSpy reports(&s.t, &TranscriptWidget::reportLogged);
        QString chunk;
        for (int i = 0; i < 1000; ++i) {
            chunk += QStringLiteral("line %1\n").arg(i);
            if (i % 37 == 0) {
                s.t.append(chunk);
                chunk.clear();
            }
        }
        s.t.append(chunk + "** Error: 9ns+0: last\n% ");
        const QTextDocument* doc = s.t.textView()->document();
        QVERIFY(doc->blockCount() <= 100);
        QCOMPARE(doc->lastBlock().text(), QString("% "));
        QCOMPARE(doc->lastBlock().previous().text(), QString("** Error: 9ns+0: last"));
        QCOMPARE(doc->lastBlock().previous().previous().text(), QString("line 999"));
        QCOMPARE(reports.count(), 1);
        QVERIFY(formatAt(s.t.textView(), doc->blockCount() - 2, 12).fontUnderline());

        // one chunk larger than the cap
        QString big;
        for (int i = 0; i < 500; ++i)
            big += QStringLiteral("** Note: %1ns+0: n\n").arg(i);
        s.t.append(big);
        QVERIFY(doc->blockCount() <= 100);
        QCOMPARE(reports.count(), 501);
        QCOMPARE(doc->lastBlock().previous().text(), QString("** Note: 499ns+0: n"));
    }

    void droppedDoFilesAreRun() {
        Shown s;
        QTemporaryDir dir;
        const QString doFile = dir.filePath("wave setup.do");
        const QString txtFile = dir.filePath("notes.txt");
        QVERIFY(touch(doFile));
        QVERIFY(touch(txtFile));
        QSignalSpy spy(&s.t, &TranscriptWidget::doFileDropped);
        for (QWidget* target : {s.t.textView()->viewport(), s.t.input()->viewport()}) {
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(txtFile)});
            QDragEnterEvent refuse(QPoint(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &refuse);
            mime.setUrls({QUrl::fromLocalFile(doFile)});
            QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &enter);
            QVERIFY(enter.isAccepted());
            QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &drop);
            QVERIFY(drop.isAccepted());
        }
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QFileInfo(doFile).absoluteFilePath());
        QVERIFY(s.t.input()->text().isEmpty()); // the file name was not typed into the command line
    }

    void theTranscriptMenuAndRecentDoFiles() {
        QTemporaryDir dir;
        const QString ini = dir.filePath("gui.ini");
        const QString script = dir.filePath("my run.do");
        QVERIFY(touch(script));
        SimWindow w(lvd::tools::discoverEnvironment(), ini);
        w.setDialogsEnabled(false);
        auto* menu = w.findChild<QMenu*>("transcriptMenu");
        QVERIFY(menu);
        QVERIFY(menu->actions().contains(w.transcript()->findAction()));
        QVERIFY(menu->actions().contains(w.transcript()->saveAction()));
        QVERIFY(menu->actions().contains(w.transcript()->historyAction()));
        QVERIFY(menu->actions().contains(w.transcript()->copyWithoutPromptsAction()));
        QVERIFY(w.transcript()->actions().contains(w.transcript()->findAction())); // Ctrl+F in the transcript
        QCOMPARE(w.transcript()->findAction()->shortcutContext(), Qt::WidgetWithChildrenShortcut);

        QVERIFY(w.recentDoFiles().isEmpty());
        w.runDoFile(script); // no simulation: a note, but the script is remembered
        QCOMPARE(w.recentDoFiles(), QStringList{QDir::cleanPath(QFileInfo(script).absoluteFilePath())});
        QCOMPARE(w.transcript()->history().last(), "do {" + QFileInfo(script).absoluteFilePath() + "}");
        QVERIFY(w.transcript()->text().contains("No simulation is running"));
        w.transcript()->submitCommand("do no_such_file.do"); // not a file: not remembered
        QCOMPARE(w.recentDoFiles().size(), 1);
        for (int i = 0; i < 12; ++i) {
            const QString f = dir.filePath(QStringLiteral("s%1.do").arg(i));
            QVERIFY(touch(f));
            w.runDoFile(f);
        }
        QCOMPARE(w.recentDoFiles().size(), SimWindow::kRecentDoFilesLimit);
        QVERIFY(w.recentDoFiles().first().endsWith("s11.do"));
        w.runDoFile(dir.filePath("s5.do")); // again: moves to the top, no duplicate
        QVERIFY(w.recentDoFiles().first().endsWith("s5.do"));
        QCOMPARE(w.recentDoFiles().filter("s5.do").size(), 1);

        auto* recent = w.findChild<QMenu*>("recentDoMenu");
        QVERIFY(recent);
        emit recent->aboutToShow();
        QCOMPARE(recent->actions().size(), SimWindow::kRecentDoFilesLimit + 2); // files, separator, clear
    }
};

QTEST_MAIN(TestConsole)
#include "test_console.moc"
