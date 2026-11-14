// SPDX-License-Identifier: GPL-3.0-or-later
// Source debugger parts without a simulator: aggregate values, the line breakpoint list, the Source gutter,
// the Breakpoints, Call Stack and Locals panes.
#include "app/breakpoint_pane.hpp"
#include "app/call_stack_pane.hpp"
#include "app/line_breakpoints.hpp"
#include "app/locals_pane.hpp"
#include "app/source_pane.hpp"
#include "sim/parsers/vhdl_aggregate.hpp"

#include <QFont>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

using namespace lvd::gui;

namespace {

QString writeFile(QTemporaryDir& dir, const QString& name, const QByteArray& text) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(text);
    return path;
}

const char* kSource = "entity e is end;\n"          // 1
                      "architecture a of e is\n"    // 2
                      "begin\n"                     // 3
                      "  p: process\n"              // 4
                      "    variable n : integer;\n" // 5
                      "  begin\n"                   // 6
                      "    n := n + 1;\n"           // 7
                      "\n"                          // 8
                      "    -- only a comment\n"     // 9
                      "    wait for 5 ns;\n"        // 10
                      "  end process;\n"            // 11
                      "end;\n";                     // 12

QStringList elements(const QString& text) {
    QStringList out;
    const auto parts = splitAggregate(text);
    if (!parts)
        return {QStringLiteral("<none>")};
    for (const AggregateElement& e : *parts)
        out << (e.name.isEmpty() ? e.value : e.name + "=" + e.value);
    return out;
}

LineBreakpointInfo kernelBp(int id, const QString& file, int line, int hits = 0) {
    LineBreakpointInfo b;
    b.id = id;
    b.file = file;
    b.line = line;
    b.hits = hits;
    return b;
}

/// y of the middle of a line in the gutter.
int gutterY(SourceView* v, int line) {
    for (int y = 0; y < v->viewport()->height(); ++y)
        if (v->lineAt(y) == line)
            return y + v->fontMetrics().height() / 2;
    return -1;
}

bool hasColour(const QImage& img, int x0, int x1, int y0, int y1, const std::function<bool(QColor)>& match) {
    for (int y = qMax(0, y0); y < qMin(img.height(), y1); ++y)
        for (int x = qMax(0, x0); x < qMin(img.width(), x1); ++x)
            if (match(img.pixelColor(x, y)))
                return true;
    return false;
}

LineBreakpoint lineBp(const QString& file, int line, bool enabled = true,
                      LineBreakpoint::State state = LineBreakpoint::State::Verified) {
    LineBreakpoint b;
    b.file = file;
    b.line = line;
    b.enabled = enabled;
    b.state = state;
    if (state == LineBreakpoint::State::Verified)
        b.ids = {line};
    return b;
}

VariableInfo var(const QString& name, const QString& value, const QString& type = "INTEGER",
                 bool hasValue = true) {
    VariableInfo v;
    v.name = name;
    v.kind = "variable";
    v.type = type;
    v.value = value;
    v.hasValue = hasValue;
    return v;
}

} // namespace

class TestSourceDebugger : public QObject {
    Q_OBJECT
private slots:
    // ---- aggregate values --------------------------------------------------------------------------
    void aggregatesSplitAtTopLevelCommas() {
        QCOMPARE(elements("(a => 7, b => '1')"), (QStringList{"a=7", "b='1'"}));
        QCOMPARE(elements("(3, 4, 5)"), (QStringList{"3", "4", "5"}));
        QCOMPARE(elements("(\"01\", \"10\")"), (QStringList{"\"01\"", "\"10\""}));
        QCOMPARE(elements("((a => 1, b => '0'), (a => 2, b => '1'))"),
                 (QStringList{"(a => 1, b => '0')", "(a => 2, b => '1')"}));
        QCOMPARE(elements("(s => \"a, b => c\", c => ',', d => ')')"),
                 (QStringList{"s=\"a, b => c\"", "c=','", "d=')'"}));
        QCOMPARE(elements("(q => \"say \"\"x, y\"\"\", r => 1)"),
                 (QStringList{"q=\"say \"\"x, y\"\"\"", "r=1"}));
        QCOMPARE(elements("  (1, 2, ...)  "), (QStringList{"1", "2", "..."}));
    }

    void nonAggregatesAreNotSplit() {
        for (const char* t :
             {"3", "'1'", "\"0101\"", "1.5", "()", "(1, 2", "(1) (2)", "(1,, 2)", "(\"abc)", ""})
            QVERIFY2(!splitAggregate(t).has_value(), t);
    }

    // ---- line breakpoint list -------------------------------------------------------------------
    void listAddsRemovesAndComparesFiles() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        LineBreakpointList l;
        QCOMPARE(l.add(f, 7), 0);
        QCOMPARE(l.add(f, 7), 0); // already there
        QCOMPARE(l.add(f, 10, false), 1);
        QCOMPARE(l.size(), 2);
        QVERIFY(!l[1].enabled);
        QCOMPARE(l[0].state, LineBreakpoint::State::Planned);
        QVERIFY(QFileInfo(l[0].file).isAbsolute());
        QCOMPARE(l.indexOf(dir.path() + "/./t.vhd", 10), 1); // the same file written differently
#ifdef Q_OS_WIN
        QCOMPARE(l.indexOf(f.toUpper(), 7), 0);
        QVERIFY(sameSourceFile(QDir::toNativeSeparators(f), f));
#endif
        QCOMPARE(l.inFile(f).size(), 2);
        QCOMPARE(l.inFile(dir.filePath("other.vhd")).size(), 0);
        QVERIFY(l.remove(f, 7));
        QVERIFY(!l.remove(f, 7));
        QCOMPARE(l.size(), 1);
    }

    void listSurvivesTheSettings() {
        LineBreakpointList l;
        l.add("C:/x/a b/t.vhd", 12);
        l.add("/src/u.vhd", 3, false);
        const QStringList s = l.serialize();
        QCOMPARE(s.size(), 2);
        const LineBreakpointList back =
            LineBreakpointList::deserialize(s + QStringList{"garbage", "1|x|f", "1|0|f"});
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[0].line, 12);
        QVERIFY(back[0].file.endsWith("a b/t.vhd"));
        QVERIFY(back[0].enabled);
        QVERIFY(!back[1].enabled);
        QCOMPARE(back[1].line, 3);
    }

    void mergeTakesShellChanges() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        LineBreakpointList l;
        l.add(f, 7);
        l[0].state = LineBreakpoint::State::Verified;
        l[0].ids = {1};
        l.add(f, 10, false); // disabled: the kernel does not know it
        l.add(f, 11);        // pending: the reply has not come
        l[2].state = LineBreakpoint::State::Pending;
        auto resolve = [&](const QString& name) { return dir.filePath(name); };

        // hits of a known id, and one typed in the shell (a new line), and one for the line already set
        l.mergeKernelList(
            {kernelBp(1, "t.vhd", 7, 2), kernelBp(4, "t.vhd", 5, 0), kernelBp(5, "t.vhd", 7, 1)}, resolve);
        QCOMPARE(l.size(), 4);
        QCOMPARE(l[0].hits, 3);
        QCOMPARE(l[0].ids, (QVector<int>{1, 5}));
        QCOMPARE(l[3].line, 5);
        QCOMPARE(l[3].state, LineBreakpoint::State::Verified);
        QVERIFY(sameSourceFile(l[3].file, f));
        QVERIFY(!l[1].enabled);
        QCOMPARE(l[2].state, LineBreakpoint::State::Pending);

        // deleted in the shell: 1 and 5 gone (line 7 removed), 4 stays
        l.mergeKernelList({kernelBp(4, "t.vhd", 5, 1)}, resolve);
        QCOMPARE(l.indexOf(f, 7), -1);
        QCOMPARE(l.indexOf(f, 10), 0); // disabled one untouched
        QCOMPARE(l[l.indexOf(f, 5)].hits, 1);
        QVERIFY(l.indexOf(f, 11) >= 0); // pending one untouched

        l.resetKernelState();
        for (const LineBreakpoint& b : l.items()) {
            QCOMPARE(b.state, LineBreakpoint::State::Planned);
            QVERIFY(b.ids.isEmpty());
            QCOMPARE(b.hits, 0);
        }
    }

    // ---- Source gutter --------------------------------------------------------------------------
    void gutterClickAsksForAToggle() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        SourcePane pane;
        pane.resize(500, 400);
        pane.show();
        QVERIFY(pane.openFile(f));
        QSignalSpy toggle(&pane, &SourcePane::breakpointToggleRequested);
        const int y = gutterY(pane.view(), 7);
        QVERIFY(y > 0);
        QTest::mouseClick(pane.view()->gutter(), Qt::LeftButton, {}, QPoint(6, y));
        QCOMPARE(toggle.count(), 1);
        QCOMPARE(toggle.at(0).at(0).toString(), pane.filePath());
        QCOMPARE(toggle.at(0).at(1).toInt(), 7);
        QTest::mouseClick(pane.view()->gutter(), Qt::RightButton, {}, QPoint(6, y));
        QCOMPARE(toggle.count(), 1); // only the left button
        QVERIFY(pane.view()->gutterToolTip(y).contains("set a breakpoint"));
        QCOMPARE(pane.view()->lineAt(100000), 0);
    }

    void gutterShowsTheBreakpointsOfTheFileOnView() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        const QString g = writeFile(dir, "u.vhd", kSource);
        SourcePane pane;
        pane.resize(500, 400);
        pane.show();
        QVERIFY(pane.openFile(f));
        LineBreakpoint rejected = lineBp(f, 4, true, LineBreakpoint::State::Rejected);
        rejected.note = "no such file";
        pane.setBreakpoints(
            {lineBp(f, 7), lineBp(f, 10, false), rejected, lineBp(f, 8), lineBp(f, 9), lineBp(g, 2)});
        const auto marks = pane.view()->breakpointMarks();
        QCOMPARE(marks.size(), 5);
        QCOMPARE(marks.value(7), BreakpointMark::Enabled);
        QCOMPARE(marks.value(10), BreakpointMark::Disabled);
        QCOMPARE(marks.value(4), BreakpointMark::Unverified);
        QCOMPARE(marks.value(8), BreakpointMark::Unverified); // empty line
        QCOMPARE(marks.value(9), BreakpointMark::Unverified); // only a comment
        QVERIFY(!marks.contains(2));
        QVERIFY(pane.view()->gutterToolTip(gutterY(pane.view(), 4)).contains("no such file"));

        QVERIFY(pane.openFile(g));
        QCOMPARE(pane.view()->breakpointMarks().keys(), QList<int>{2});

        // drawn: a red dot in the gutter
        QVERIFY(pane.openFile(f));
        const QImage img = pane.view()->gutter()->grab().toImage();
        const int y = gutterY(pane.view(), 7) - pane.view()->gutter()->y() + pane.view()->viewport()->y();
        QVERIFY(hasColour(img, 0, 20, y - 8, y + 8,
                          [](QColor c) { return c.red() > 180 && c.green() < 90 && c.blue() < 90; }));
    }

    void executionPointFollowsTheFile() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        const QString g = writeFile(dir, "u.vhd", kSource);
        SourcePane pane;
        pane.resize(500, 300);
        pane.show();
        QVERIFY(pane.openFile(f));
        pane.setExecutionPoint(f, 10, ExecutionMark::Innermost);
        QCOMPARE(pane.executionLine(), 10);
        QCOMPARE(pane.view()->executionMark(), ExecutionMark::Innermost);
        QCOMPARE(pane.view()->extraSelections().size(), 1);
        QCOMPARE(pane.view()->textCursor().blockNumber(), 9); // scrolled there
        const QImage img = pane.view()->gutter()->grab().toImage();
        QVERIFY(hasColour(img, 0, 20, 0, img.height(),
                          [](QColor c) { return c.red() > 230 && c.green() > 180 && c.blue() < 60; }));

        QVERIFY(pane.openFile(g)); // another file: no marker
        QCOMPARE(pane.executionLine(), 0);
        QVERIFY(pane.openFile(f)); // back: the marker is there again
        QCOMPARE(pane.executionLine(), 10);

        pane.setExecutionPoint(f, 7, ExecutionMark::Caller);
        QCOMPARE(pane.executionLine(), 7);
        QCOMPARE(pane.view()->executionMark(), ExecutionMark::Caller);
        const QImage green = pane.view()->gutter()->grab().toImage();
        QVERIFY(hasColour(green, 0, 20, 0, green.height(),
                          [](QColor c) { return c.green() > 150 && c.red() < 100 && c.blue() < 120; }));
        pane.clearExecutionPoint();
        QCOMPARE(pane.executionLine(), 0);
        QCOMPARE(pane.view()->extraSelections().size(), 0);
    }

    // ---- Breakpoints pane ---------------------------------------------------------------------------
    void breakpointsPaneListsBothKinds() {
        BreakpointPane pane;
        pane.setBreakpoints({{1, 50000000}});
        LineBreakpoint rejected = lineBp("C:/x/u.vhd", 3, true, LineBreakpoint::State::Rejected);
        rejected.note = "bad name";
        LineBreakpoint hit = lineBp("C:/x/t.vhd", 12);
        hit.hits = 4;
        pane.setLineBreakpoints(
            {hit, lineBp("C:/x/t.vhd", 20, false, LineBreakpoint::State::Planned), rejected});
        QCOMPARE(pane.table()->rowCount(), 4);
        QCOMPARE(pane.breakpointCount(), 1);
        QCOMPARE(pane.lineBreakpointCount(), 3);
        QCOMPARE(pane.table()->item(0, 1)->text(), QString("50 ns"));
        QCOMPARE(pane.table()->item(1, 1)->text(), QString("t.vhd:12"));
        QCOMPARE(pane.table()->item(1, 2)->text(), QString("4"));
        QCOMPARE(pane.table()->item(1, 0)->checkState(), Qt::Checked);
        QCOMPARE(pane.table()->item(2, 0)->checkState(), Qt::Unchecked);
        QCOMPARE(pane.table()->item(2, 3)->text(), QString("disabled"));
        QVERIFY(pane.table()->item(3, 3)->text().contains("bad name"));
        QCOMPARE(pane.rowOfLine("C:/x/t.vhd", 20), 2);

        QSignalSpy enable(&pane, &BreakpointPane::lineEnableRequested);
        pane.table()->item(2, 0)->setCheckState(Qt::Checked);
        QCOMPARE(enable.count(), 1);
        QCOMPARE(enable.at(0).at(1).toInt(), 20);
        QVERIFY(enable.at(0).at(2).toBool());

        QSignalSpy del(&pane, &BreakpointPane::lineDeleteRequested);
        QSignalSpy delTime(&pane, &BreakpointPane::deleteRequested);
        pane.table()->selectRow(1);
        QCOMPARE(pane.selectedId(), 0); // not a time breakpoint
        pane.requestDeleteSelected();
        QCOMPARE(del.count(), 1);
        QCOMPARE(del.at(0).at(1).toInt(), 12);
        QCOMPARE(delTime.count(), 0);

        QSignalSpy activated(&pane, &BreakpointPane::lineActivated);
        emit pane.table()->cellActivated(1, 1);
        QCOMPARE(activated.count(), 1);

        // the selection stays on the same breakpoint when the list is refreshed
        pane.setLineBreakpoints({lineBp("C:/x/a.vhd", 1), hit});
        QCOMPARE(pane.table()->selectionModel()->selectedRows().first().row(), 2);

        // only line breakpoints: Delete all is still possible
        pane.setBreakpoints({});
        bool deleteAllEnabled = false;
        for (QPushButton* b : pane.findChildren<QPushButton*>())
            if (b->text() == "Delete all")
                deleteAllEnabled = b->isEnabled();
        QVERIFY(deleteAllEnabled);
    }

    // ---- Call Stack ---------------------------------------------------------------------------------
    void callStackSelectsFrames() {
        CallStackPane pane;
        QSignalSpy sel(&pane, &CallStackPane::frameSelected);
        SourceFrame f0{0, "function", "double", "WORK.T.DOUBLE(I)I", "t.vhd", 11};
        SourceFrame f1{1, "process", "/counter", "WORK.T.COUNTER", "/src/t.vhd", 50};
        pane.setFrames("/counter", {f0, f1});
        QCOMPARE(pane.frameCount(), 2);
        QCOMPARE(pane.selectedLevel(), 0);
        QCOMPARE(sel.count(), 0); // filling does not count as a selection
        QCOMPARE(pane.tree()->topLevelItem(0)->text(1), QString("function double"));
        QCOMPARE(pane.tree()->topLevelItem(1)->text(2), QString("t.vhd:50"));
        pane.selectLevel(1);
        QCOMPARE(sel.count(), 1);
        QCOMPARE(sel.at(0).at(0).toInt(), 1);
        pane.clear("Not stopped.");
        QCOMPARE(pane.frameCount(), 0);
        QCOMPARE(pane.selectedLevel(), -1);
    }

    // ---- Locals -------------------------------------------------------------------------------------
    void localsShowRecordsAndChanges() {
        LocalsPane pane;
        LocalsReply r;
        r.variables = {var("total", "0"), var("both", "(a => 7, b => '1')", "PAIR"),
                       var("ints", "(1, 2)", "INT_ARRAY"), var("ptr", {}, "INT_PTR", false),
                       var("name", "\"abc\"", "STRING")};
        pane.setLocals("/counter|P", r, 1);
        QCOMPARE(pane.variableCount(), 5);
        QCOMPARE(pane.valueOf("total"), QString("0"));
        QCOMPARE(pane.valueOf("both.a"), QString("7"));
        QCOMPARE(pane.valueOf("both.b"), QString("'1'"));
        QCOMPARE(pane.valueOf("ints#2"), QString("2"));
        QCOMPARE(pane.itemOf("both")->childCount(), 2);
        QCOMPARE(pane.itemOf("name")->childCount(), 0); // a string stays flat
        QCOMPARE(pane.valueOf("ptr"), QString("(not shown)"));
        QVERIFY(pane.changedPaths().isEmpty()); // the first stop has nothing to compare with

        pane.itemOf("both")->setExpanded(true);
        r.variables[0].value = "2";
        r.variables[1].value = "(a => 7, b => '0')";
        pane.setLocals("/counter|P", r, 2);
        QCOMPARE(pane.changedPaths(), (QStringList{"both", "both.b", "total"}));
        QVERIFY(pane.itemOf("total")->font(2).bold());
        QVERIFY(pane.itemOf("both")->isExpanded()); // kept across a stop
        QVERIFY(!pane.itemOf("ints")->font(2).bold());

        // the same stop shown again (another frame selected and back): same highlight
        LocalsReply other;
        pane.setLocals("/counter|F", other, 2, "No variables here.");
        QCOMPARE(pane.variableCount(), 0);
        QCOMPARE(pane.note(), QString("No variables here."));
        pane.setLocals("/counter|P", r, 2);
        QCOMPARE(pane.changedPaths(), (QStringList{"both", "both.b", "total"}));

        // next stop with the same values: nothing changed
        pane.setLocals("/counter|P", r, 3);
        QVERIFY(pane.changedPaths().isEmpty());

        pane.forgetHistory();
        r.variables[0].value = "9";
        pane.setLocals("/counter|P", r, 4);
        QVERIFY(pane.changedPaths().isEmpty());
    }
};

QTEST_MAIN(TestSourceDebugger)
#include "test_source_debugger.moc"
