// SPDX-License-Identifier: GPL-3.0-or-later
// Processes, Breakpoints and Source panes and the VHDL highlighter, without a simulator.
#include "app/breakpoint_pane.hpp"
#include "app/processes_pane.hpp"
#include "app/source_pane.hpp"
#include "app/vhdl_highlighter.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace lvd::gui;

namespace {

ProcessInfo proc(const QString& name, const QString& state, const QString& file = {}, int line = 0) {
    ProcessInfo p;
    p.name = name;
    p.path = "/" + name;
    p.state = state;
    p.file = file;
    p.line = line;
    return p;
}

QString writeFile(QTemporaryDir& dir, const QString& name, const QByteArray& text) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(text);
    return path;
}

const char* kSource = "entity e is end;\n"
                      "architecture a of e is\n"
                      "  signal clk : bit;\n"
                      "begin\n"
                      "  p: process (clk) begin\n"
                      "    wait for 5 ns; -- tick\n"
                      "  end process;\n"
                      "end;\n";

// Text of the spans, for readable comparisons: "Keyword:entity"
QStringList spansOf(const QString& line, bool inBlock = false, bool* endsInBlock = nullptr) {
    QStringList out;
    bool state = inBlock;
    for (const VhdlSpan& s : tokenizeVhdlLine(line, state)) {
        static const char* names[] = {"Keyword",   "Type",   "Comment",  "String",
                                      "Character", "Number", "Attribute"};
        out << QStringLiteral("%1:%2").arg(QLatin1String(names[static_cast<int>(s.kind)]),
                                           line.mid(s.start, s.length));
    }
    if (endsInBlock)
        *endsInBlock = state;
    return out;
}

} // namespace

class TestDebugPanes : public QObject {
    Q_OBJECT
private slots:
    // ---- processes -------------------------------------------------------------------------------
    void processesTableShowsTheList() {
        ProcessesPane pane;
        pane.setProcesses({proc("counter", "waiting", "C:/x/t.vhd", 7), proc("_p0", "ready"),
                           proc("d", "delayed", "t.vhd", 3)});
        QCOMPARE(pane.processCount(), 3);
        QCOMPARE(pane.table()->rowCount(), 3);
        QCOMPARE(pane.table()->item(0, 0)->text(), QString("counter"));
        QCOMPARE(pane.table()->item(0, 1)->text(), QString("/counter"));
        QCOMPARE(pane.table()->item(0, 2)->text(), QString("waiting"));
        QCOMPARE(pane.table()->item(0, 3)->text(), QString("t.vhd:7")); // file name and line
        QCOMPARE(pane.table()->item(1, 3)->text(), QString());          // location unknown
        pane.setProcesses({});
        QCOMPARE(pane.table()->rowCount(), 0);
    }

    void doubleClickOpensTheSource() {
        ProcessesPane pane;
        pane.setProcesses({proc("counter", "waiting", "C:/x/t.vhd", 7), proc("_p0", "ready")});
        QSignalSpy open(&pane, &ProcessesPane::openRequested);
        pane.table()->cellActivated(0, 0);
        QCOMPARE(open.count(), 1);
        QCOMPARE(open.at(0).at(0).toString(), QString("C:/x/t.vhd"));
        QCOMPARE(open.at(0).at(1).toInt(), 7);
        pane.table()->cellActivated(1, 0); // unknown location is still reported, with an empty file
        QCOMPARE(open.count(), 2);
        QCOMPARE(open.at(1).at(0).toString(), QString());
    }

    void selectionSurvivesARefreshAndReportsTheLocation() {
        ProcessesPane pane;
        pane.setProcesses({proc("a", "waiting", "t.vhd", 3), proc("b", "waiting", "t.vhd", 9)});
        QSignalSpy selected(&pane, &ProcessesPane::processSelected);
        pane.table()->selectRow(1);
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.last().at(1).toInt(), 9);
        QCOMPARE(pane.selectedProcess().name, QString("b"));

        pane.setProcesses({proc("a", "ready", "t.vhd", 3), proc("b", "delayed", "t.vhd", 9)});
        QCOMPARE(pane.selectedProcess().name, QString("b"));
        QCOMPARE(pane.table()->item(1, 2)->text(), QString("delayed"));
    }

    void readyLinesOfAFile() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        ProcessesPane pane;
        pane.setProcesses(
            {proc("a", "ready", f, 5), proc("b", "waiting", f, 6), proc("c", "ready", "other.vhd", 2)});
        QCOMPARE(pane.readyLines(f), (QSet<int>{5}));
    }

    void refreshButton() {
        ProcessesPane pane;
        QSignalSpy refresh(&pane, &ProcessesPane::refreshRequested);
        for (QPushButton* b : pane.findChildren<QPushButton*>())
            b->click();
        QCOMPARE(refresh.count(), 1);
    }

    // ---- breakpoints -----------------------------------------------------------------------------
    void breakpointListShowsTimes() {
        BreakpointPane pane;
        pane.setBreakpoints({{1, 50000000}, {2, 1500000}});
        QCOMPARE(pane.table()->rowCount(), 2);
        QCOMPARE(pane.table()->item(0, 0)->text(), QString("1"));
        QCOMPARE(pane.table()->item(0, 1)->text(), QString("50 ns"));
        QCOMPARE(pane.table()->item(1, 1)->text(), QString("1500 ps"));
    }

    void addNeedsAPositiveTime() {
        BreakpointPane pane;
        QSignalSpy add(&pane, &BreakpointPane::addRequested);
        for (const char* bad : {"", "abc", "-5", "0", "1e3", "1 2"}) {
            pane.setTimeText(bad, "ns");
            QVERIFY2(!pane.submitAdd(), bad);
        }
        QCOMPARE(add.count(), 0);
        pane.setTimeText(" 2.5 ", "us");
        QVERIFY(pane.submitAdd());
        QCOMPARE(add.count(), 1);
        QCOMPARE(add.at(0).at(0).toString(), QString("2.5"));
        QCOMPARE(add.at(0).at(1).toString(), QString("us"));
    }

    void deleteSelectedAndAll() {
        BreakpointPane pane;
        pane.setBreakpoints({{4, 1000}, {9, 2000}});
        QSignalSpy del(&pane, &BreakpointPane::deleteRequested);
        QSignalSpy all(&pane, &BreakpointPane::deleteAllRequested);
        pane.requestDeleteSelected(); // nothing selected
        QCOMPARE(del.count(), 0);
        pane.table()->selectRow(1);
        QCOMPARE(pane.selectedId(), 9);
        pane.requestDeleteSelected();
        QCOMPARE(del.count(), 1);
        QCOMPARE(del.at(0).at(0).toInt(), 9);
        for (QPushButton* b : pane.findChildren<QPushButton*>())
            if (b->text() == "Delete all")
                b->click();
        QCOMPARE(all.count(), 1);
    }

    // ---- highlighter -----------------------------------------------------------------------------
    void tokenizerKnowsTheBasics() {
        QCOMPARE(spansOf("entity e is end;"), (QStringList{"Keyword:entity", "Keyword:is", "Keyword:end"}));
        QCOMPARE(spansOf("ENTITY"), (QStringList{"Keyword:ENTITY"})); // case insensitive
        QCOMPARE(spansOf("signal s : std_logic_vector(7 downto 0);"),
                 (QStringList{"Keyword:signal", "Type:std_logic_vector", "Number:7", "Keyword:downto",
                              "Number:0"}));
        QCOMPARE(spansOf("x <= y; -- note \"q\""), (QStringList{"Comment:-- note \"q\""}));
    }

    void tokenizerStringsAndCharacters() {
        QCOMPARE(spansOf("a <= \"10\" & '1';"), (QStringList{"String:\"10\"", "Character:'1'"}));
        QCOMPARE(spansOf("report \"say \"\"hi\"\"\";"),
                 (QStringList{"Keyword:report", "String:\"say \"\"hi\"\"\""}));
        QCOMPARE(spansOf("x <= x\"FF\";"), (QStringList{"String:x\"FF\""}));
        QCOMPARE(spansOf("s <= \"-- not a comment\";"), (QStringList{"String:\"-- not a comment\""}));
    }

    void tokenizerAttributesAreNotCharacters() {
        QCOMPARE(
            spansOf("if clk'event and clk = '1' then"),
            (QStringList{"Keyword:if", "Attribute:'event", "Keyword:and", "Character:'1'", "Keyword:then"}));
        QCOMPARE(spansOf("v'length"), (QStringList{"Attribute:'length"}));
    }

    void tokenizerNumbers() {
        QCOMPARE(spansOf("16#FF# 1_000 2.5e-3 10"),
                 (QStringList{"Number:16#FF#", "Number:1_000", "Number:2.5e-3", "Number:10"}));
        QCOMPARE(spansOf("wait for 5 ns;"), (QStringList{"Keyword:wait", "Keyword:for", "Number:5"}));
        // digits inside identifiers are not numbers
        QCOMPARE(spansOf("sig1 sig_2"), QStringList());
    }

    void tokenizerBlockComments() {
        bool open = false;
        QCOMPARE(spansOf("a /* x", false, &open), (QStringList{"Comment:/* x"}));
        QVERIFY(open);
        QCOMPARE(spansOf("still inside", true, &open), (QStringList{"Comment:still inside"}));
        QVERIFY(open);
        QCOMPARE(spansOf("end */ signal", true, &open), (QStringList{"Comment:end */", "Keyword:signal"}));
        QVERIFY(!open);
        QCOMPARE(spansOf("a /* b */ c", false, &open), (QStringList{"Comment:/* b */"}));
        QVERIFY(!open);
    }

    // ---- source pane -----------------------------------------------------------------------------
    void sourceOpensAFileAtALine() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        SourcePane pane;
        pane.resize(500, 300);
        QVERIFY(pane.openFile(f, 5));
        QCOMPARE(pane.lineCount(), 9); // the trailing newline gives an empty last line
        QCOMPARE(pane.filePath(), QFileInfo(f).absoluteFilePath());
        QCOMPARE(pane.view()->textCursor().blockNumber(), 4);
        QVERIFY(pane.view()->isReadOnly());

        pane.setCurrentLine(6);
        QCOMPARE(pane.currentLine(), 6);
        QCOMPARE(pane.view()->extraSelections().size(), 1);
        pane.setReadyLines({5});
        QCOMPARE(pane.view()->extraSelections().size(), 2);
        pane.setCurrentLine(0);
        QCOMPARE(pane.view()->extraSelections().size(), 1);
    }

    void sourceReportsAMissingFileAndKeepsTheOldText() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        SourcePane pane;
        QVERIFY(pane.openFile(f));
        QVERIFY(!pane.openFile(dir.filePath("nope.vhd"), 3));
        QVERIFY(pane.lastError().contains("nope.vhd"));
        QCOMPARE(pane.filePath(), QFileInfo(f).absoluteFilePath());
        QVERIFY(pane.view()->toPlainText().startsWith("entity"));
        QVERIFY(!pane.openFile(QString()));
    }

    void sourceRelativeNamesUseTheCurrentDirectory() {
        QTemporaryDir dir;
        writeFile(dir, "rel.vhd", kSource);
        const QString old = QDir::currentPath();
        QDir::setCurrent(dir.path());
        SourcePane pane;
        const bool ok = pane.openFile("rel.vhd", 2);
        QDir::setCurrent(old);
        QVERIFY(ok);
    }

    void findWrapsAndIsCaseInsensitive() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        SourcePane pane;
        QVERIFY(pane.openFile(f));
        QVERIFY(pane.find("PROCESS"));
        QCOMPARE(pane.view()->textCursor().selectedText(), QString("process"));
        QCOMPARE(pane.view()->textCursor().blockNumber(), 4);
        QVERIFY(pane.find("process")); // next one
        QCOMPARE(pane.view()->textCursor().blockNumber(), 6);
        QVERIFY(pane.find("process")); // wraps to the first
        QCOMPARE(pane.view()->textCursor().blockNumber(), 4);
        QVERIFY(pane.find("process", true)); // backwards wraps to the last
        QCOMPARE(pane.view()->textCursor().blockNumber(), 6);
        QVERIFY(!pane.find("no such text"));
        QVERIFY(!pane.find(QString()));
    }

    void sourceScreenshotIsNotBlank() {
        QTemporaryDir dir;
        const QString f = writeFile(dir, "t.vhd", kSource);
        SourcePane pane;
        pane.resize(500, 300);
        QVERIFY(pane.openFile(f, 5));
        pane.setCurrentLine(5);
        pane.show();
        const QImage img = pane.grab().toImage();
        QVERIFY(!img.isNull());
        // the amber current line marker is somewhere in the gutter
        bool amber = false;
        for (int y = 0; y < img.height() && !amber; ++y)
            for (int x = 0; x < 30 && !amber; ++x)
                amber = img.pixelColor(x, y).red() > 200 && img.pixelColor(x, y).green() > 130 &&
                        img.pixelColor(x, y).blue() < 80;
        QVERIFY(amber);
    }
};

QTEST_MAIN(TestDebugPanes)
#include "test_debug_panes.moc"
