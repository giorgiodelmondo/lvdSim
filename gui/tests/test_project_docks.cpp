// SPDX-License-Identifier: GPL-3.0-or-later
// Project, Problems and Regression docks: editing a project in the tree, status icons, the Problems list and
// (with the real nvc) incremental analysis, a regression with a passing, a failing and a timing-out test, and
// starting a test from the tree.
#include "app/problems_pane.hpp"
#include "app/project_pane.hpp"
#include "app/project_session.hpp"
#include "app/regression_pane.hpp"
#include "app/sim_window.hpp"
#include "nvc_test_design.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace lvd::gui;
using lvd::gui::testing::findNvc;

namespace {

void writeFile(const QString& path, const QByteArray& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

const char* kProject = R"([project]
name = demo
timeout = 30
[library util]
file = src/util_pkg.vhd
file = src/unrelated.vhd
[library core_lib]
file = src/core.vhd
[library work]
file = src/tb.vhd
[test pass]
top = work.tb
expect = TEST PASSED
[test fail]
top = work.tb
generic = MODE=1
[test hang]
top = work.tb
generic = MODE=2
timeout = 3
)";

void writeDesign(const QString& dir) {
    writeFile(dir + "/src/util_pkg.vhd",
              "package util_pkg is\n  function double(x : integer) return integer;\nend package;\n"
              "package body util_pkg is\n  function double(x : integer) return integer is\n"
              "  begin return 2 * x; end function;\nend package body;\n");
    writeFile(dir + "/src/unrelated.vhd", "package unrelated is constant K : integer := 1; end package;\n");
    writeFile(dir + "/src/core.vhd",
              "library util; use util.util_pkg.all;\nentity core is port (a : in integer; "
              "y : out integer);\nend entity;\narchitecture rtl of core is begin y <= double(a); "
              "end architecture;\n");
    writeFile(dir + "/src/tb.vhd", R"(library core_lib;
entity tb is generic (MODE : integer := 0); end entity;
architecture sim of tb is
  signal a, y : integer := 0;
begin
  dut : entity core_lib.core port map (a => a, y => y);
  process begin
    a <= 21;
    wait for 10 ns;
    if MODE = 0 then
      assert y = 42 report "wrong result" severity error;
      report "TEST PASSED";
    elsif MODE = 1 then
      assert y = 43 report "y is not 43" severity error;
    else
      loop wait for 1 ns; end loop;
    end if;
    wait;
  end process;
end architecture;
)");
    writeFile(dir + "/demo.lvdproj", kProject);
}

lvd::tools::Environment environment() {
    const auto n = findNvc();
    lvd::tools::Environment e = lvd::tools::discoverEnvironment();
    e.nvc = n.exe.toStdString();
    e.nvcDllDir = n.dllDir.toStdString();
    e.ctx.nvcGlobalArgs = {"-L", n.libDir.toStdString()};
    return e;
}

QTreeWidgetItem* fileItem(QTreeWidget* tree, const QString& path) {
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->text(0) == path)
            return *it;
    return nullptr;
}

QStringList fileOrder(const ProjectSession& s, int lib) {
    QStringList l;
    for (const auto& f : s.project().libraries[static_cast<std::size_t>(lib)].files)
        l << QString::fromStdString(f.path);
    return l;
}

} // namespace

class TestProjectDocks : public QObject {
    Q_OBJECT
private slots:
    void treeShowsLibrariesFilesAndTests();
    void editsThroughTheToolBar();
    void problemsListOpensSource();
    void incrementalCompileWithNvc();
    void regressionWithNvc();
    void simulateTestFromProject();
};

void TestProjectDocks::treeShowsLibrariesFilesAndTests() {
    QTemporaryDir dir;
    writeDesign(dir.path());
    ProjectSession s(environment());
    ProjectPane pane(&s);
    QVERIFY(s.open(dir.filePath("demo.lvdproj")));
    QCOMPARE(pane.tree()->topLevelItemCount(), 4); // three libraries and Tests
    QCOMPARE(pane.tree()->topLevelItem(0)->text(0), QString("util"));
    QCOMPARE(pane.tree()->topLevelItem(0)->childCount(), 2);
    QCOMPARE(pane.tree()->topLevelItem(3)->childCount(), 3);
    // Nothing analysed yet: every file is out of date with its reason.
    QTreeWidgetItem* core = fileItem(pane.tree(), "src/core.vhd");
    QVERIFY(core);
    QCOMPARE(core->text(1), QString("library missing")); // no library directory yet
    QVERIFY(!core->icon(0).isNull());
    QCOMPARE(pane.tree()->topLevelItem(1)->text(1), QString("1 of 1 to compile"));
    QVERIFY(pane.selectTest("hang"));
    QCOMPARE(pane.selectedTest(), QString("hang"));
    QVERIFY(pane.action("simulateTest")->isEnabled());
    QSignalSpy sim(&pane, &ProjectPane::simulateRequested);
    pane.action("simulateTest")->trigger();
    QCOMPARE(sim.count(), 1);
    QCOMPARE(sim.at(0).at(0).toString(), QString("hang"));
    QCOMPARE(s.vsimArgsFor("fail"), QStringList({"-gMODE=1", "work.tb"}));
}

void TestProjectDocks::editsThroughTheToolBar() {
    QTemporaryDir dir;
    writeDesign(dir.path());
    ProjectSession s(environment());
    ProjectPane pane(&s);
    pane.setDialogsEnabled(false);
    QVERIFY(s.open(dir.filePath("demo.lvdproj")));
    QVERIFY(!s.modified());

    QVERIFY(pane.selectFile(0, 1));
    QVERIFY(pane.action("moveUp")->isEnabled());
    QVERIFY(!pane.action("moveDown")->isEnabled());
    pane.action("moveUp")->trigger();
    QCOMPARE(fileOrder(s, 0), QStringList({"src/unrelated.vhd", "src/util_pkg.vhd"}));
    QCOMPARE(pane.selectedFile(), std::make_pair(0, 0)); // the selection follows the file
    pane.action("moveDown")->trigger();
    QCOMPARE(fileOrder(s, 0), QStringList({"src/util_pkg.vhd", "src/unrelated.vhd"}));
    QVERIFY(s.modified());

    writeFile(dir.filePath("src/extra.vhd"), "package extra is end package;\n");
    QCOMPARE(s.addFiles(2, {dir.filePath("src/extra.vhd"), dir.filePath("src/tb.vhd")}),
             1); // tb.vhd is known
    QVERIFY(fileItem(pane.tree(), "src/extra.vhd"));
    QVERIFY(pane.selectFile(2, 1));
    pane.action("removeItem")->trigger();
    QVERIFY(!fileItem(pane.tree(), "src/extra.vhd"));
    QVERIFY(pane.selectTest("hang"));
    pane.action("removeItem")->trigger();
    QCOMPARE(s.project().tests.size(), std::size_t(2));

    QVERIFY(s.save());
    QFile f(dir.filePath("demo.lvdproj"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    QVERIFY(!text.contains("[test hang]"));
    QVERIFY(text.contains("file = src/util_pkg.vhd\nfile = src/unrelated.vhd"));
    QVERIFY(s.addLibrary("extra_lib"));
    QVERIFY(!s.addLibrary("bad name"));
}

void TestProjectDocks::problemsListOpensSource() {
    ProblemsPane pane;
    QVector<Problem> ps(2);
    ps[0].severity = lvd::tools::diag::Severity::Error;
    ps[0].message = "no visible declaration for FOO";
    ps[0].file = "C:/p/src/a.vhd";
    ps[0].line = 12;
    ps[1].severity = lvd::tools::diag::Severity::Warning;
    ps[1].message = "something";
    pane.setProblems(ps);
    QCOMPARE(pane.count(), 2);
    QCOMPARE(pane.errorCount(), 1);
    QCOMPARE(pane.tree()->topLevelItem(0)->text(2), QString("a.vhd"));
    QCOMPARE(pane.tree()->topLevelItem(0)->text(3), QString("12"));
    QSignalSpy spy(&pane, &ProblemsPane::sourceRequested);
    pane.activateRow(0);
    pane.activateRow(1); // no file: nothing to open
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("C:/p/src/a.vhd"));
    QCOMPARE(spy.at(0).at(1).toInt(), 12);
}

void TestProjectDocks::incrementalCompileWithNvc() {
    if (!findNvc().found)
        QSKIP("nvc not found (set LVDSIM_NVC)");
    QTemporaryDir dir;
    writeDesign(dir.path());
    ProjectSession s(environment());
    ProjectPane pane(&s);
    ProblemsPane problems;
    QVERIFY(s.open(dir.filePath("demo.lvdproj")));
    QSignalSpy progress(&s, &ProjectSession::compileProgress);
    QVERIFY(s.compile(lvd::tools::build::Mode::OutOfDate));
    QVERIFY(s.waitForCompile());
    QCOMPARE(s.lastCompileFailures(), 0);
    QCOMPARE(progress.count(), 8); // start and end of four files
    for (const auto& st : s.statuses())
        QCOMPARE(int(st.state), int(lvd::tools::build::FileState::UpToDate));
    QCOMPARE(fileItem(pane.tree(), "src/tb.vhd")->text(1), QString("up to date"));

    // Change the package: it and its two dependents are out of date, the unrelated package is not.
    {
        QFile f(dir.filePath("src/util_pkg.vhd"));
        QVERIFY(f.open(QIODevice::Append));
        f.write("-- edit\n");
    }
    s.refreshStatus();
    QCOMPARE(fileItem(pane.tree(), "src/util_pkg.vhd")->text(1), QString("changed"));
    QCOMPARE(fileItem(pane.tree(), "src/core.vhd")->text(1), QString("depends on util_pkg.vhd"));
    QCOMPARE(fileItem(pane.tree(), "src/unrelated.vhd")->text(1), QString("up to date"));
    progress.clear();
    QVERIFY(s.compile(lvd::tools::build::Mode::OutOfDate));
    QVERIFY(s.waitForCompile());
    QCOMPARE(progress.count(), 6); // three files

    // An error: listed in Problems with its absolute file and line, the file shows failed.
    writeFile(dir.filePath("src/core.vhd"),
              "entity core is end;\narchitecture rtl of core is\nbegin\n  x <= 1;\nend;\n");
    QVERIFY(s.compile(lvd::tools::build::Mode::OutOfDate));
    QVERIFY(s.waitForCompile());
    QVERIFY(s.lastCompileFailures() > 0);
    problems.setProblems(s.problems());
    QVERIFY(problems.errorCount() >= 1);
    QCOMPARE(QFileInfo(s.problems().front().file), QFileInfo(dir.filePath("src/core.vhd")));
    QCOMPARE(s.problems().front().line, 4);
    QCOMPARE(fileItem(pane.tree(), "src/core.vhd")->text(1), QString("failed"));
}

void TestProjectDocks::regressionWithNvc() {
    if (!findNvc().found)
        QSKIP("nvc not found (set LVDSIM_NVC)");
    QTemporaryDir dir;
    writeDesign(dir.path());
    ProjectSession s(environment());
    RegressionPane pane(&s);
    QVERIFY(s.open(dir.filePath("demo.lvdproj")));
    pane.jobsBox()->setValue(3);
    QCOMPARE(s.jobs(), 3);
    QVERIFY(pane.action("regressAll")->isEnabled());
    pane.action("regressAll")->trigger(); // analyses the project first, then runs the three tests
    QVERIFY(s.waitForRegression(120000));
    QCOMPARE(pane.table()->rowCount(), 3);
    QCOMPARE(pane.table()->item(0, RegressionPane::ColStatus)->text(), QString("passed"));
    QCOMPARE(pane.table()->item(1, RegressionPane::ColStatus)->text(), QString("failed"));
    QVERIFY(pane.table()->item(1, RegressionPane::ColMessage)->text().contains("y is not 43"));
    QCOMPARE(pane.table()->item(2, RegressionPane::ColStatus)->text(), QString("timeout"));
    QCOMPARE(pane.summaryText(), QString("1 of 3 passed, 2 failed"));
    QCOMPARE(s.failedIds(), QStringList({"fail", "hang"}));
    QVERIFY(QFile::exists(dir.filePath("lvdsim_regression/results.txt")));

    QSignalSpy log(&pane, &RegressionPane::logRequested);
    pane.table()->selectRow(1);
    QVERIFY(pane.action("regressLog")->isEnabled());
    pane.action("regressLog")->trigger();
    QCOMPARE(log.count(), 1);
    QVERIFY(log.at(0).at(0).toString().endsWith("fail/run.log"));

    QVERIFY(pane.action("regressFailed")->isEnabled());
    pane.action("regressFailed")->trigger();
    QVERIFY(s.waitForRegression(120000));
    QCOMPARE(pane.table()->rowCount(), 2);
}

void TestProjectDocks::simulateTestFromProject() {
    if (!findNvc().found)
        QSKIP("nvc not found (set LVDSIM_NVC)");
    QTemporaryDir dir;
    writeDesign(dir.path());
    const QString cwd = QDir::currentPath();
    {
        SimWindow w(environment(), dir.filePath("settings.ini"));
        w.setDialogsEnabled(false);
        QVERIFY(w.openProject(dir.filePath("demo.lvdproj")));
        QCOMPARE(QFileInfo(QDir::currentPath()), QFileInfo(dir.path()));
        QSignalSpy started(&w, &SimWindow::simulationStarted);
        QVERIFY(w.simulateTest("pass"));
        QVERIFY(w.project()->waitForCompile());
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 20000);
        QVERIFY(w.waitUntilIdle(30000));
        QVERIFY(w.sessionArguments().contains("tb"));
        w.stopSimulation();
    }
    QDir::setCurrent(cwd);
}

QTEST_MAIN(TestProjectDocks)
#include "test_project_docks.moc"
