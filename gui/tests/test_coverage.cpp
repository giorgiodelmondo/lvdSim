// SPDX-License-Identifier: GPL-3.0-or-later
// The Coverage dock, the coverage annotations of the Source gutter and, with the real nvc, loading, merging
// and reporting coverage from the simulation window.
#include "app/coverage_pane.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"

#include <QCheckBox>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

#include <coverage.hpp>
#include <process.hpp>

using namespace lvd::gui;
namespace cov = lvd::tools::cov;

namespace {

QByteArray readData(const QString& name) {
    QFile f(QStringLiteral(LVD_COVERAGE_DATA) + "/" + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::shared_ptr<const cov::Database> fixtureDb() {
    const QByteArray x = readData("cov_tb.xml");
    cov::ParseResult r =
        cov::parseExportXml(std::string_view(x.constData(), static_cast<std::size_t>(x.size())));
    if (!r.ok())
        return nullptr;
    return std::make_shared<const cov::Database>(std::move(r.db));
}

// TOP with U_FULL (everything covered) and U_PART (one statement missed).
const char* kTwoInstances = R"(<?xml version="1.0"?>
<scope name="WORK">
  <scope name="TOP" block_name="TOP-A" file="top.vhd" line="1">
    <scope name="U_FULL" block_name="SUB-RTL" line="5">
      <scope name="_P0" file="sub.vhd" line="10"><statement hier="WORK.TOP.U_FULL._P0" data="3"/></scope>
    </scope>
    <scope name="U_PART" block_name="SUB-RTL" line="6">
      <scope name="_P0" file="sub.vhd" line="10"><statement hier="WORK.TOP.U_PART._P0" data="3"/></scope>
      <scope name="_P1" file="sub.vhd" line="12"><statement hier="WORK.TOP.U_PART._P1" data="0"/></scope>
    </scope>
  </scope>
</scope>
)";

QTreeWidgetItem* childNamed(QTreeWidgetItem* parent, const QString& prefix) {
    for (int i = 0; i < parent->childCount(); ++i)
        if (parent->child(i)->text(0).startsWith(prefix))
            return parent->child(i);
    return nullptr;
}

QTreeWidgetItem* topNamed(QTreeWidget* t, const QString& prefix) {
    for (int i = 0; i < t->topLevelItemCount(); ++i)
        if (t->topLevelItem(i)->text(0).startsWith(prefix))
            return t->topLevelItem(i);
    return nullptr;
}

/// The y position in the gutter of a line of the view (it must be visible).
int yOfLine(SourceView* v, int line) {
    for (int y = 0; y < v->viewport()->height(); ++y)
        if (v->lineAt(y) == line)
            return y + 2;
    return -1;
}

} // namespace

class TestCoverage : public QObject {
    Q_OBJECT
private slots:
    void namesOfUnitsFieldsAndEmptyDatabases() {
        QCOMPARE(CoveragePane::unitText("COUNTER-RTL"), QString("counter(rtl)"));
        QCOMPARE(CoveragePane::unitText(R"(\my-unit\-RTL)"), QString(R"(\my-unit\(rtl))"));
        QCOMPARE(CoveragePane::unitText("G"), QString("g")); // a generate block
        // A record field is shown as signal.field in the toggle details.
        const char* json = R"({"v":1,"mask":4,"files":["a.vhd"],"scopes":[{"name":"WORK","parent":-1},)"
                           R"({"name":"TOP","parent":0,"block":"TOP-A","file":0,"line":1},)"
                           R"({"name":"R","parent":1,"file":0,"line":2}],"bins":[)"
                           R"({"scope":2,"kind":"toggle","hier":"WORK.TOP.R.A.BIN_0_TO_1","hits":1},)"
                           R"({"scope":2,"kind":"toggle","hier":"WORK.TOP.R.A.BIN_1_TO_0","hits":0}]})";
        cov::ParseResult r = cov::parseExport(json);
        QVERIFY(r.ok());
        CoveragePane pane;
        pane.setDatabase(std::make_shared<const cov::Database>(std::move(r.db)), "r.ncdb");
        QVERIFY(pane.selectInstance("/top"));
        QCOMPARE(pane.selectedInstancePath(), QString("/top"));
        QTreeWidgetItem* toggles = topNamed(pane.details(), "Toggles");
        QVERIFY(toggles);
        QTreeWidgetItem* sig = childNamed(toggles, "r");
        QVERIFY(sig);
        QVERIFY(childNamed(sig, "r.a"));
        // A database without items (a Verilog design: nvc instruments VHDL only) says why.
        cov::ParseResult empty =
            cov::parseExport(R"({"v":1,"files":[],"scopes":[{"name":"","parent":-1}],"bins":[]})");
        QVERIFY(empty.ok());
        pane.setDatabase(std::make_shared<const cov::Database>(std::move(empty.db)), "v.ncdb");
        QVERIFY(pane.summary()->text().contains("VHDL only"));
        QCOMPARE(pane.selectedInstancePath(), QString());
    }

    void hierarchyAndPercentages() {
        auto db = fixtureDb();
        QVERIFY(db);
        CoveragePane pane;
        pane.setDatabase(db, "cov_tb.ncdb");
        QTreeWidget* t = pane.hierarchy();
        QCOMPARE(t->topLevelItemCount(), 1);
        QTreeWidgetItem* top = t->topLevelItem(0);
        QCOMPARE(top->text(CoveragePane::kInstanceColumn), QString("cov_tb"));
        QCOMPARE(top->text(CoveragePane::kUnitColumn), QString("cov_tb(test)"));
        QCOMPARE(top->childCount(), 2);
        const int stmt = CoveragePane::kFirstKindColumn + static_cast<int>(cov::Kind::Statement);
        QCOMPARE(top->data(stmt, CoveragePane::kPercentRole).toDouble(), 100.0 * 31 / 41);
        QCOMPARE(top->text(stmt), QString("75.6%"));
        QCOMPARE(top->text(CoveragePane::kTotalColumn), QString("59.4%"));
        QVERIFY(top->toolTip(stmt).contains("31 of 41"));
        // No functional coverage in the design: no column for it.
        QVERIFY(t->isColumnHidden(CoveragePane::kFirstKindColumn + static_cast<int>(cov::Kind::Functional)));
        QVERIFY(!t->isColumnHidden(CoveragePane::kFirstKindColumn + static_cast<int>(cov::Kind::State)));
        QVERIFY(pane.summary()->text().contains("59.4%"));
        QVERIFY(pane.summary()->text().contains("cov_tb.ncdb"));
        QCOMPARE(pane.visibleInstancePaths(), QStringList({"/cov_tb", "/cov_tb/u_busy", "/cov_tb/u_idle"}));

        QCOMPARE(CoveragePane::percentText(-1), QString("-"));
        QCOMPARE(CoveragePane::percentText(100), QString("100%"));
        QCOMPARE(CoveragePane::percentText(99.97), QString("99.9%"));
        QCOMPARE(CoveragePane::percentText(0), QString("0.0%"));

        pane.clear();
        QCOMPARE(t->topLevelItemCount(), 0);
        QVERIFY(pane.database() == nullptr);
    }

    void filterAndUncoveredOnly() {
        CoveragePane pane;
        pane.setDatabase(fixtureDb(), {});
        pane.setFilter("idle");
        QCOMPARE(pane.visibleInstancePaths(), QStringList({"/cov_tb", "/cov_tb/u_idle"}));
        pane.setFilter("COV_COUNTER"); // the design unit matches too
        QCOMPARE(pane.visibleInstancePaths(), QStringList({"/cov_tb", "/cov_tb/u_busy", "/cov_tb/u_idle"}));
        pane.setFilter("nothing");
        QVERIFY(pane.visibleInstancePaths().isEmpty());
        QVERIFY(!pane.selectInstance("/cov_tb/u_busy")); // filtered out
        pane.setFilter({});

        const cov::ParseResult r = cov::parseExportXml(kTwoInstances);
        QVERIFY(r.ok());
        pane.setDatabase(std::make_shared<const cov::Database>(r.db), {});
        QCOMPARE(pane.visibleInstancePaths(), QStringList({"/top", "/top/u_full", "/top/u_part"}));
        pane.uncoveredCheck()->setChecked(true);
        QVERIFY(pane.uncoveredOnly());
        QCOMPARE(pane.visibleInstancePaths(), QStringList({"/top", "/top/u_part"}));
        pane.setUncoveredOnly(false);
        QCOMPARE(pane.visibleInstancePaths().size(), 3);
    }

    void selectionShowsSourceAndDetails() {
        CoveragePane pane;
        pane.setDatabase(fixtureDb(), {});
        QSignalSpy source(&pane, &CoveragePane::sourceRequested);
        QSignalSpy selected(&pane, &CoveragePane::instanceSelected);
        QVERIFY(pane.selectInstance("/cov_tb/u_busy"));
        QCOMPARE(selected.count(), 1);
        QCOMPARE(pane.selectedInstance(), pane.database()->findScope("/cov_tb/u_busy"));
        QCOMPARE(source.count(), 1);
        QCOMPARE(source.at(0).at(0).toString(), QString("cov_design.vhd"));
        QCOMPARE(source.at(0).at(1).toInt(), 41); // when ERROR_STATE => : the first code not covered

        QTreeWidget* d = pane.details();
        QTreeWidgetItem* lines = topNamed(d, "Lines");
        QTreeWidgetItem* toggles = topNamed(d, "Toggles");
        QTreeWidgetItem* states = topNamed(d, "FSM states");
        QVERIFY(lines && toggles && states);
        QCOMPARE(states->text(0), QString("FSM states (3 of 4)"));
        QTreeWidgetItem* stateVar = childNamed(states, "state");
        QVERIFY(stateVar);
        QTreeWidgetItem* err = childNamed(stateVar, "error_state");
        QVERIFY(err);
        QCOMPARE(err->text(1), QString("0"));
        QTreeWidgetItem* cnt = childNamed(toggles, "cnt");
        QVERIFY(cnt);
        QCOMPARE(cnt->childCount(), 4);
        QCOMPARE(cnt->child(0)->text(0), QString("cnt(3)"));
        QCOMPARE(cnt->child(0)->text(1), QString("1 / 1"));
        QTreeWidgetItem* en = childNamed(toggles, "en");
        QVERIFY(en);
        QCOMPARE(en->text(1), QString("1 / 0"));
        QTreeWidgetItem* l42 = childNamed(lines, "line 42");
        QVERIFY(l42);
        QVERIFY(l42->text(0).contains("1 statement"));

        // A detail asks for its line.
        d->setCurrentItem(l42);
        QCOMPARE(source.count(), 2);
        QCOMPARE(source.last().at(1).toInt(), 42);
        d->setCurrentItem(err);
        QCOMPARE(source.last().at(1).toInt(), 19); // the state signal's declaration

        // Uncovered only: fully toggled signals and covered lines go away.
        pane.setUncoveredOnly(true);
        toggles = topNamed(d, "Toggles");
        QVERIFY(toggles);
        QVERIFY(childNamed(toggles, "cnt") == nullptr);
        QVERIFY(childNamed(toggles, "en") != nullptr);
        lines = topNamed(d, "Lines not fully covered");
        QVERIFY(lines);
        QVERIFY(childNamed(lines, "line 35") == nullptr);
        QVERIFY(childNamed(lines, "line 42") != nullptr);

        // The idle instance: its enable is tied to '0', it cannot toggle.
        QVERIFY(pane.selectInstance("/cov_tb/u_idle"));
        toggles = topNamed(d, "Toggles");
        QVERIFY(toggles);
        QTreeWidgetItem* ien = childNamed(toggles, "en");
        QVERIFY(ien);
        QCOMPARE(ien->text(1), QString("cannot toggle"));
    }

    void lineMarksOfTheSelection() {
        CoveragePane pane;
        pane.setDatabase(fixtureDb(), {});
        QVERIFY(pane.lineMarks().isEmpty());
        QVERIFY(pane.selectInstance("/cov_tb/u_busy"));
        const auto marks = pane.lineMarks();
        QCOMPARE(marks.keys(), QStringList({"cov_design.vhd"}));
        const auto& m = marks.value("cov_design.vhd");
        QVERIFY(m.value(42).uncovered());
        QCOMPARE(m.value(42).hits, 0);
        QVERIFY(m.value(42).tip.contains("1 statement"));
        QVERIFY(m.value(35).covered());
        QCOMPARE(m.value(35).hits, 18);
        QVERIFY(!m.value(48).covered() && !m.value(48).uncovered());
        QVERIFY(!m.contains(78)); // test bench lines belong to the top
    }

    void sourceGutter() {
        QTemporaryDir dir;
        const QString file = dir.filePath("cov_design.vhd");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(readData("cov_design.vhd"));
        }
        SourcePane pane;
        pane.resize(700, 1200);
        pane.show();
        QVERIFY(pane.openFile(file, 1));
        const int plain = pane.view()->gutterWidth();
        QCOMPARE(pane.view()->coverageColumnWidth(), 0);

        CoveragePane cp;
        cp.setDatabase(fixtureDb(), {});
        QVERIFY(cp.selectInstance("/cov_tb/u_busy"));
        pane.setCoverage({{file, cp.lineMarks().value("cov_design.vhd")}});
        QVERIFY(pane.hasCoverage());
        SourceView* v = pane.view();
        QVERIFY(v->coverageColumnWidth() > 0);
        QCOMPARE(v->gutterWidth(), plain + v->coverageColumnWidth());
        QVERIFY(v->coverageMarks().value(42).uncovered());
        // The lines with something not covered are tinted.
        QVERIFY(v->extraSelections().size() >= 5);

        // Breakpoints and coverage together: the tool tip has both.
        LineBreakpoint bp;
        bp.file = file;
        bp.line = 35;
        pane.setBreakpoints({bp});
        QVERIFY(v->breakpointMarks().contains(35));
        QVERIFY(v->coverageMarks().contains(35));
        v->showLine(40);
        QCoreApplication::processEvents();
        const int y35 = yOfLine(v, 35);
        const int y42 = yOfLine(v, 42);
        QVERIFY(y35 >= 0 && y42 >= 0);
        QVERIFY(v->gutterToolTip(y35).contains("Breakpoint"));
        QVERIFY(v->gutterToolTip(y35).contains("Coverage: all 1 item(s) covered") ||
                v->gutterToolTip(y35).contains("Coverage: all"));
        const QString tip42 = v->gutterToolTip(y42);
        QVERIFY2(tip42.contains("0 of 1") && tip42.contains("statement") && tip42.contains("Click to set"),
                 qPrintable(tip42));
        // A click in the gutter still toggles a breakpoint.
        QSignalSpy toggle(&pane, &SourcePane::breakpointToggleRequested);
        v->clickGutter(y42);
        QCOMPARE(toggle.count(), 1);
        QCOMPARE(toggle.at(0).at(1).toInt(), 42);
        // The gutter paints without trouble.
        QImage img(v->gutter()->size(), QImage::Format_ARGB32);
        v->gutter()->render(&img);

        // Another file has no marks; coming back shows them again.
        const QString other = dir.filePath("other.vhd");
        {
            QFile f(other);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("entity other is end;\n");
        }
        QVERIFY(pane.openFile(other));
        QVERIFY(v->coverageMarks().isEmpty());
        QCOMPARE(v->coverageColumnWidth(), 0);
        QVERIFY(pane.openFile(file));
        QVERIFY(!v->coverageMarks().isEmpty());
        pane.clearCoverage();
        QVERIFY(v->coverageMarks().isEmpty());
        QCOMPARE(v->gutterWidth(), plain);

        QCOMPARE(SourceView::hitText(7), QString("7"));
        QCOMPARE(SourceView::hitText(9999), QString("9999"));
        QCOMPARE(SourceView::hitText(12345), QString("12.3k"));
        QCOMPARE(SourceView::hitText(1234567), QString("1.2M"));
    }

    // ---- with the real nvc
    // ---------------------------------------------------------------------------------------

    void simWindowLoadsMergesAndReports() {
        lvd::gui::testing::AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY2(d.prepare(readData("cov_design.vhd").constData()), "could not analyse the test design");
        // A batch run with coverage (nvc -r writes the counts).
        auto batch = [&](const QString& db, const QString& stop) {
            std::vector<std::string> args = {d.nvc.exe.toStdString(),
                                             "-L",
                                             d.nvc.libDir.toStdString(),
                                             "--std=08",
                                             "--work=work:work",
                                             "-e",
                                             "cov_tb",
                                             "-r"};
            if (!stop.isEmpty())
                args.push_back("--stop-time=" + stop.toStdString());
            if (!cov::addElaborationOptions(args, {}, d.file(db).toStdString()))
                return false;
            lvd::tools::ProcessOptions po;
            po.capture = true;
            po.env = {{"NVC_MAX_THREADS", "1"}};
            if (!d.nvc.dllDir.isEmpty())
                po.pathPrepend.push_back(d.nvc.dllDir.toStdString());
            const auto r = lvd::tools::runProcess(args, po);
            if (r.exitCode != 0)
                qWarning("%s", r.output.c_str());
            return r.exitCode == 0;
        };
        QVERIFY(batch("a.ncdb", {}));
        QVERIFY(batch("b.ncdb", "40ns"));

        SimWindow w(d.environment(), d.file("cov.ini"));
        w.setDialogsEnabled(false);
        w.show();
        QSignalSpy loaded(&w, &SimWindow::coverageLoaded);
        QVERIFY(w.openCoverage({d.file("a.ncdb")}));
        QVERIFY(w.waitForCoverageTool());
        QCOMPARE(loaded.count(), 1);
        QVERIFY(w.coverage()->database() != nullptr);
        QCOMPARE(w.coverage()->database()->totals().allTotal(), 175);
        QVERIFY(w.findChild<QDockWidget*>("coverageDock")->isVisible());
        // The top is selected: its source is on view with the coverage of its own lines.
        QCOMPARE(QFileInfo(w.source()->filePath()).fileName(), QString("t.vhd"));
        QVERIFY(w.source()->hasCoverage());
        QVERIFY(w.coverage()->selectInstance("/cov_tb/u_busy"));
        QVERIFY(w.source()->view()->coverageMarks().value(42).uncovered());
        QVERIFY(w.transcript()->text().contains("104 of 175"));

        // Two databases: merged, the counts add up.
        QVERIFY(w.openCoverage({d.file("a.ncdb"), d.file("b.ncdb")}));
        QVERIFY(w.waitForCoverageTool());
        QCOMPARE(loaded.count(), 2);
        QCOMPARE(w.coverageDatabases().size(), 2);
        QVERIFY(w.coverage()->summary()->text().contains("2 databases merged"));
        QVERIFY(w.saveMergedCoverage(d.file("merged.ncdb")));
        QVERIFY(w.waitForCoverageTool());
        QVERIFY(QFileInfo::exists(d.file("merged.ncdb")));

        // HTML report.
        QSignalSpy written(&w, &SimWindow::coverageReportWritten);
        QVERIFY(w.exportCoverageReport(d.file("html")));
        QVERIFY(w.waitForCoverageTool());
        QCOMPARE(written.count(), 1);
        QVERIFY(QFileInfo::exists(written.at(0).at(0).toString()));

        // A missing database is refused at once.
        QVERIFY(!w.openCoverage({d.file("missing.ncdb")}));
        QVERIFY(w.lastError().contains("not found"));
    }

    void collectCoverageOption() {
        lvd::gui::testing::AnalysedDesign d;
        if (!d.nvc.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY2(d.prepare(readData("cov_design.vhd").constData()), "could not analyse the test design");
        const QString ini = d.file("collect.ini");
        {
            SimWindow w(d.environment(), ini);
            w.setDialogsEnabled(false);
            QAction* a = w.findChild<QAction*>("collectCoverage");
            QVERIFY(a && a->isCheckable() && !a->isChecked());
            a->setChecked(true); // the menu entry
            QVERIFY(w.coverageEnabled());
            QVERIFY(w.startSimulation({"work.cov_tb"}));
            const QStringList args = w.sessionArguments();
            const int e = static_cast<int>(args.indexOf("-e"));
            QVERIFY(e >= 0);
            QCOMPARE(args.value(e + 1),
                     QString("--cover=statement,branch,expression,toggle,fsm-state,functional"));
            QVERIFY(args.value(e + 2).startsWith("--cover-file="));
            QCOMPARE(QFileInfo(w.sessionCoverageFile()).fileName(), QString("cov_tb.ncdb"));
            QVERIFY(w.waitUntilIdle());
            QVERIFY(QFileInfo::exists(w.sessionCoverageFile())); // written by the elaboration
            w.sendCommand("run 300 ns");
            QVERIFY(w.waitUntilIdle());
            QSignalSpy loaded(&w, &SimWindow::coverageLoaded);
            w.stopSimulation();
            QTRY_VERIFY_WITH_TIMEOUT(!w.simulationRunning(), 20000);
            QVERIFY(w.waitForCoverageTool());
            // The shell writes the counts when it ends (and the end of the simulation at 225 ns already
            // loaded them): the dock shows real counts, not those of the elaboration.
            QVERIFY2(loaded.count() >= 1, qPrintable(w.transcript()->text().right(600)));
            QVERIFY(!w.transcript()->text().contains("may have ended without"));
            QVERIFY(w.coverage()->database()->totals().allHit() > 0);
        }
        {
            // The setting is kept.
            SimWindow w(d.environment(), ini);
            w.setDialogsEnabled(false);
            QVERIFY(w.coverageEnabled());
            w.setCoverageEnabled(false);
            QVERIFY(w.startSimulation({"work.cov_tb"}));
            QVERIFY(!w.sessionArguments().join(' ').contains("--cover"));
            QVERIFY(w.sessionCoverageFile().isEmpty());
            w.stopSimulation();
            // The vsim options on the command line take the same path (planVsim) as the menu setting.
            QVERIFY(w.startSimulation(
                {"-coverage", "-lvdcover", "sb", "-lvdcoverfile", "mine.ncdb", "work.cov_tb"}));
            const QStringList args = w.sessionArguments();
            const int e = static_cast<int>(args.indexOf("-e"));
            QVERIFY(e >= 0);
            QCOMPARE(args.value(e + 1), QString("--cover=statement,branch"));
            // Absolute: nvc's replies name it so, and a later change of the working directory does not move
            // it.
            QCOMPARE(args.value(e + 2), "--cover-file=" + QDir::current().absoluteFilePath("mine.ncdb"));
            QCOMPARE(w.sessionCoverageFile(), QDir::current().absoluteFilePath("mine.ncdb"));
            w.stopSimulation();
        }
    }
};

QTEST_MAIN(TestCoverage)
#include "test_coverage.moc"
