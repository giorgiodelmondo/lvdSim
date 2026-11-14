// SPDX-License-Identifier: GPL-3.0-or-later
// The console's text rules without widgets: complete commands, splitting and one-line encoding, prompts,
// severities, file:line references, the history and Tab completion.
#include "app/console_completer.hpp"
#include "app/console_history.hpp"
#include "app/console_text.hpp"

#include <lvdwave/store.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

bool touch(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly);
}

/// The design top "t" with signals clk, cnt, count_en and an instance u_sub (a, y) and u_sub2 (z).
std::unique_ptr<WaveStore> makeStore() {
    auto st = std::make_unique<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "t");
    st->addSignal(top, "clk", SignalKind::Logic, 1);
    st->addSignal(top, "cnt", SignalKind::Logic, 32);
    st->addSignal(top, "count_en", SignalKind::Logic, 1);
    const ScopeId sub = st->addScope(top, "u_sub");
    st->addSignal(sub, "a", SignalKind::Logic, 1);
    st->addSignal(sub, "y", SignalKind::Logic, 1);
    const ScopeId sub2 = st->addScope(top, "u_sub2");
    st->addSignal(sub2, "z", SignalKind::Logic, 1);
    return st;
}

} // namespace

class TestConsoleText : public QObject {
    Q_OBJECT
private slots:
    void completeCommands_data() {
        QTest::addColumn<QString>("script");
        QTest::addColumn<bool>("complete");
        QTest::newRow("empty") << "" << true;
        QTest::newRow("plain") << "run 100 ns" << true;
        QTest::newRow("open brace") << "proc f {} {" << false;
        QTest::newRow("closed body") << "proc f {} {\n  puts hi\n}" << true;
        QTest::newRow("nested") << "foreach x {1 2} { if {$x} { puts $x }" << false;
        QTest::newRow("nested closed") << "foreach x {1 2} { if {$x} { puts $x } }" << true;
        QTest::newRow("open quote") << "puts \"abc" << false;
        QTest::newRow("quote closed on next line") << "puts \"abc\ndef\"" << true;
        QTest::newRow("open bracket") << "set x [expr {1 +" << false;
        QTest::newRow("bracket closed") << "set x [expr {1 + 2}]" << true;
        QTest::newRow("bracket in quotes") << "puts \"[string length abc]\"" << true;
        QTest::newRow("open bracket in quotes") << "puts \"[string length abc\"" << false;
        QTest::newRow("trailing backslash") << "add wave /a \\" << false;
        QTest::newRow("escaped brace") << "puts \\{" << true;
        QTest::newRow("brace inside a word") << "puts a{b" << true;
        QTest::newRow("quote inside a word") << "puts a\"b" << true;
        QTest::newRow("brace in a comment") << "# a { in a comment\nputs x" << true;
        QTest::newRow("comment continued") << "# comment \\" << false;
        QTest::newRow("escaped brace in braces") << "set x {a \\} b}" << true;
        QTest::newRow("brace in braces quoted") << "proc f {} { puts \"{\" }" << false; // as Tcl counts it
        QTest::newRow("semicolon") << "set a 1; set b {" << false;
        QTest::newRow("expand") << "list {*}[list a b]" << true;
    }
    void completeCommands() {
        QFETCH(QString, script);
        QFETCH(bool, complete);
        QCOMPARE(tclCommandComplete(script), complete);
    }

    void splitIntoCommands() {
        QCOMPARE(splitTopLevelCommands("run 10 ns\nexamine /cnt\n"),
                 (QStringList{"run 10 ns", "examine /cnt"}));
        QCOMPARE(splitTopLevelCommands("proc f {} {\n  puts hi\n}\nf"),
                 (QStringList{"proc f {} {\n  puts hi\n}", "f"}));
        QCOMPARE(splitTopLevelCommands("\n  \nrun\r\n\n"), (QStringList{"run"}));
        QCOMPARE(splitTopLevelCommands("add wave \\\n  /a /b"), (QStringList{"add wave \\\n  /a /b"}));
        QCOMPARE(splitTopLevelCommands("set x {"), (QStringList{"set x {"})); // unfinished rest: last
    }

    void oneLineEncoding() {
        QCOMPARE(oneLineCommand("run 10 ns"), QString("run 10 ns"));
        QCOMPARE(oneLineCommand("run 10 ns\n"), QString("run 10 ns"));
        QCOMPARE(oneLineCommand("proc f {} {\n  puts \"$a [b] \\\\x\"\n}"),
                 QString("eval \"proc f {} {\\n  puts \\\"\\$a \\[b\\] \\\\\\\\x\\\"\\n}\""));
        QVERIFY(!oneLineCommand("a\nb\r\nc").contains('\n'));
        QVERIFY(!oneLineCommand("a\nb\r\nc").contains('\r'));
    }

    void prompts() {
        QCOMPARE(stripPrompts("% run 10 ns\n** Note: x\n% % examine /a\n%"),
                 QString("run 10 ns\n** Note: x\nexamine /a\n"));
        QCOMPARE(stripPrompts("100% done"), QString("100% done"));
    }

    void severities() {
        QCOMPARE(lineSeverity("** Error: x"), LineSeverity::Error);
        QCOMPARE(lineSeverity("% ** Error: invalid command"), LineSeverity::Error);
        QCOMPARE(lineSeverity("** Fatal: x"), LineSeverity::Error);
        QCOMPARE(lineSeverity("** Failure: 2us+0: stop"), LineSeverity::Error);
        QCOMPARE(lineSeverity("** Warning: 20 us+1: w"), LineSeverity::Warning);
        QCOMPARE(lineSeverity("** Note: 100ns+0: n"), LineSeverity::Note);
        QCOMPARE(lineSeverity("   Process :t:_p1 at t.vhd:17"), LineSeverity::None);
        QCOMPARE(lineSeverity("Error: no stars"), LineSeverity::None);
        QCOMPARE(lineSeverity("**Error"), LineSeverity::None);
    }

    void sourceReferences() {
        auto refs = findSourceRefs("   Process :t:_p1 at t.vhd:17");
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs[0].file, QString("t.vhd"));
        QCOMPARE(refs[0].line, 17);
        QCOMPARE(refs[0].start, 21);
        QCOMPARE(refs[0].length, 8);

        refs = findSourceRefs("    > D:/work/src/top level.vhd:3"); // a blank in the path: the part after it
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs[0].file, QString("level.vhd"));

        refs = findSourceRefs("    > D:/work/src/counter.vhdl:12:7 and C:\\x\\b.sv:4.");
        QCOMPARE(refs.size(), 2);
        QCOMPARE(refs[0].file, QString("D:/work/src/counter.vhdl"));
        QCOMPARE(refs[0].line, 12);
        QCOMPARE(refs[0].length, QString("D:/work/src/counter.vhdl:12:7").size());
        QCOMPARE(refs[1].file, QString("C:\\x\\b.sv"));
        QCOMPARE(refs[1].line, 4);

        refs = findSourceRefs("    (file \"run.do\" line 3)");
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs[0].file, QString("run.do"));
        QCOMPARE(refs[0].line, 3);
        QCOMPARE(refs[0].start, 11);

        QVERIFY(findSourceRefs("** Note: 35ns+0: counter passed three").isEmpty());
        QVERIFY(findSourceRefs("time 10:20 and a.b:3 and x.vhd:0 and y.vhdx:3").isEmpty());
        QVERIFY(findSourceRefs("plain text").isEmpty());
    }

    void fileWords() {
        QCOMPARE(tclFileWord("C:/a/run.do"), QString("C:/a/run.do"));
        QCOMPARE(tclFileWord("C:/a b/run.do"), QString("{C:/a b/run.do}"));
        QCOMPARE(tclFileWord("a$b.do"), QString("{a$b.do}"));
    }

    // ---- history -----------------------------------------------------------------------------------------
    void historyKeepsUniqueNonBlankCommandsUpToTheLimit() {
        ConsoleHistory h(3);
        QVERIFY(h.add("run"));
        QVERIFY(!h.add("   "));
        QVERIFY(h.add("examine /a"));
        QVERIFY(h.add("run")); // moves to the end
        QCOMPARE(h.entries(), (QStringList{"examine /a", "run"}));
        h.add("a");
        h.add("b");
        QCOMPARE(h.entries(), (QStringList{"run", "a", "b"})); // the oldest went
        h.setLimit(2);
        QCOMPARE(h.entries(), (QStringList{"a", "b"}));
        h.setEntries({"x", "y", "x", "", "z"});
        QCOMPARE(h.entries(), (QStringList{"x", "z"})); // "x" again moved after "y", then the limit
    }

    void historySearchGoesBackwards() {
        ConsoleHistory h;
        h.setEntries({"run 10 ns", "examine /cnt", "add wave /cnt", "run -all"});
        QCOMPARE(h.searchBackward("cnt", h.size()), 2);
        QCOMPARE(h.searchBackward("cnt", 2), 1);
        QCOMPARE(h.searchBackward("cnt", 1), -1);
        QCOMPARE(h.searchBackward("RUN", h.size()), 3); // case-insensitive
        QCOMPARE(h.searchBackward("", h.size()), 3);
        QCOMPARE(h.searchBackward("nothing", h.size()), -1);
    }

    // ---- completion --------------------------------------------------------------------------------------
    void completesCommands() {
        ConsoleCompleter c;
        Completion r = c.complete("exa", 3);
        QCOMPARE(r.kind, Completion::Kind::Command);
        QCOMPARE(r.candidates, (QStringList{"exa", "examine"}));
        QCOMPARE(r.commonPrefix(), QString("exa"));
        QVERIFY(c.complete("my", 2).candidates.isEmpty());
        c.setShellCommands({"myProc", "::ns::hidden", "run"});
        QVERIFY(c.hasShellCommands());
        QCOMPARE(c.complete("my", 2).candidates, QStringList{"myProc"});
        c.invalidateShellCommands();
        QVERIFY(!c.hasShellCommands());
        QCOMPARE(c.complete("my", 2).candidates, QStringList{"myProc"}); // kept until the next list
        // the command of the current statement: after ; and inside [ ]
        QCOMPARE(c.complete("run; exa", 8).kind, Completion::Kind::Command);
        QCOMPARE(c.complete("set x [exa", 10).candidates, (QStringList{"exa", "examine"}));
    }

    void completesSubcommandsOptionsAndValues() {
        ConsoleCompleter c;
        Completion r = c.complete("add w", 5);
        QCOMPARE(r.kind, Completion::Kind::Subcommand);
        QCOMPARE(r.candidates, QStringList{"wave"});
        QCOMPARE(r.start, 4);
        r = c.complete("add wave -ra", 12);
        QCOMPARE(r.kind, Completion::Kind::Option);
        QCOMPARE(r.candidates, QStringList{"-radix"});
        r = c.complete("run -", 5);
        QCOMPARE(r.candidates, (QStringList{"-all", "-continue", "-json"}));
        r = c.complete("examine -radix h", 16);
        QCOMPARE(r.kind, Completion::Kind::Value);
        QCOMPARE(r.candidates, QStringList{"hexadecimal"});
        r = c.complete("step -o", 7);
        QCOMPARE(r.candidates, (QStringList{"-out", "-over"}));
        QCOMPARE(r.commonPrefix(), QString("-o"));
        QVERIFY(c.complete("unknowncmd -", 12).candidates.isEmpty());
    }

    void completesDesignPathsLikeTheShellNamesThem() {
        auto st = makeStore();
        ConsoleCompleter c;
        QVERIFY(c.complete("examine /c", 10).candidates.isEmpty()); // no design yet
        c.setStore(st.get());
        Completion r = c.complete("examine /c", 10);
        QCOMPARE(r.kind, Completion::Kind::Path);
        QCOMPARE(r.candidates, (QStringList{"/clk", "/cnt", "/count_en"}));
        QCOMPARE(r.labels, (QStringList{"clk", "cnt", "count_en"}));
        r = c.complete("examine /co", 11);
        QCOMPARE(r.candidates, QStringList{"/count_en"});
        r = c.complete("add wave /u", 11);
        QCOMPARE(r.candidates, (QStringList{"/u_sub/", "/u_sub2/"}));
        QCOMPARE(r.commonPrefix(), QString("/u_sub"));
        QVERIFY(Completion::isContainer(r.candidates.first()));
        r = c.complete("add wave /U_SUB/", 16); // VHDL names: any case, the design's spelling comes back
        QCOMPARE(r.candidates, (QStringList{"/u_sub/a", "/u_sub/y"}));
        r = c.complete("force ", 6); // a command that takes paths: the top level
        QCOMPARE(r.kind, Completion::Kind::Path);
        QCOMPARE(r.candidates.size(), 5);
        QVERIFY(c.complete("examine /nothing/x", 18).candidates.isEmpty());
        QVERIFY(c.complete("puts ", 5).candidates.isEmpty()); // not a path command, no slash
    }

    void completesFileNames() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char* f : {"run.do", "setup.tcl", "notes.txt", "rtl.vhd"})
            QVERIFY(touch(dir.filePath(f)));
        QDir(dir.path()).mkdir("scripts");
        QVERIFY(touch(dir.filePath("scripts/wave.do")));
        ConsoleCompleter c;
        c.setWorkingDir(dir.path());
        Completion r = c.complete("do ", 3);
        QCOMPARE(r.kind, Completion::Kind::File);
        QCOMPARE(r.candidates, (QStringList{"scripts/", "run.do", "setup.tcl"})); // directories first
        r = c.complete("do scripts/", 11);
        QCOMPARE(r.candidates, QStringList{"scripts/wave.do"});
        QCOMPARE(r.labels, QStringList{"wave.do"});
        r = c.complete("vcom r", 6);
        QCOMPARE(r.candidates, QStringList{"rtl.vhd"});
        r = c.complete("do {sc", 6); // inside a brace: the part after it
        QCOMPARE(r.start, 4);
        QCOMPARE(r.candidates, QStringList{"scripts/"});
    }
};

QTEST_MAIN(TestConsoleText)
#include "test_console_text.moc"
