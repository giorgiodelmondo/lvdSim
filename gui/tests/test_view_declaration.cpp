// SPDX-License-Identifier: GPL-3.0-or-later
// View Declaration / Definition / Instantiation / Source end to end: a real nvc simulation (describe -json
// -location, kernel PR 10) driven through SimWindow, the Source pane at the right line, Back and Forward,
// a source file that is not found, and the Library pane without a simulation (nvc --print-deps).
#include "app/library_pane.hpp"
#include "app/locals_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/processes_pane.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/transcript_widget.hpp"
#include "nvc_test_design.hpp"
#include "wave/list_window.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/wave_commands.hpp>

#include <QAction>
#include <QMenu>
#include <QTableWidget>
#include <QTreeWidget>
#include <QtTest>

#include <memory>

using namespace lvd::gui;
using lvd::gui::testing::AnalysedDesign;

namespace {

// Line numbers matter: the tests check them.
const char* kDesign = "entity sub is\n"                                        // 1
                      "  generic ( width : natural := 4 );\n"                  // 2
                      "  port ( clk : in bit;\n"                               // 3
                      "         q   : out bit_vector(width - 1 downto 0) );\n" // 4
                      "end entity;\n"                                          // 5
                      "\n"                                                     // 6
                      "architecture rtl of sub is\n"                           // 7
                      "  signal r : bit_vector(width - 1 downto 0);\n"         // 8
                      "begin\n"                                                // 9
                      "  reg: process (clk) is\n"                              // 10
                      "    variable count : natural := 0;\n"                   // 11
                      "  begin\n"                                              // 12
                      "    if clk'event and clk = '1' then\n"                  // 13
                      "      count := count + 1;\n"                            // 14
                      "      r <= not r;\n"                                    // 15
                      "    end if;\n"                                          // 16
                      "  end process;\n"                                       // 17
                      "  q <= r;\n"                                            // 18
                      "  g: for i in 0 to 1 generate\n"                        // 19
                      "    signal t : bit;\n"                                  // 20
                      "  begin\n"                                              // 21
                      "    t <= r(i);\n"                                       // 22
                      "  end generate;\n"                                      // 23
                      "end architecture;\n"                                    // 24
                      "\n"                                                     // 25
                      "entity t is\n"                                          // 26
                      "end entity;\n"                                          // 27
                      "\n"                                                     // 28
                      "architecture test of t is\n"                            // 29
                      "  signal clk : bit := '0';\n"                           // 30
                      "  signal q   : bit_vector(3 downto 0);\n"               // 31
                      "begin\n"                                                // 32
                      "  clk <= not clk after 5 ns when now < 100 ns;\n"       // 33
                      "  uut: entity work.sub\n"                               // 34
                      "    generic map ( width => 4 )\n"                       // 35
                      "    port map ( clk => clk, q => q );\n"                 // 36
                      "end architecture;\n";                                   // 37

const char* kVerilog = "module sub(input clk, output reg [3:0] q);\n" // 1
                       "  wire n;\n"                                  // 2
                       "  assign n = clk;\n"                          // 3
                       "  initial q = 0;\n"                           // 4
                       "  always @(posedge clk) q <= q + 1;\n"        // 5
                       "endmodule\n"                                  // 6
                       "\n"                                           // 7
                       "module t;\n"                                  // 8
                       "  reg clk = 0;\n"                             // 9
                       "  wire [3:0] q;\n"                            // 10
                       "  always #5 clk = ~clk;\n"                    // 11
                       "  sub u (.clk(clk), .q(q));\n"                // 12
                       "endmodule\n";                                 // 13

QTreeWidgetItem* findItem(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name)
            return parent->child(i);
        if (QTreeWidgetItem* deeper = findItem(parent->child(i), name))
            return deeper;
    }
    return nullptr;
}

/// The Objects item of a dotted store path ("t.uut.clk").
QTreeWidgetItem* objectItem(ObjectsPane* pane, const QString& dotted) {
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) -> QTreeWidgetItem* {
        if (pane->declarationPathOf(it) == dotted)
            return it;
        for (int i = 0; i < it->childCount(); ++i)
            if (QTreeWidgetItem* f = walk(it->child(i)))
                return f;
        return nullptr;
    };
    return walk(pane->tree()->invisibleRootItem());
}

/// Double-clicks an Objects item with the mouse, as the user does.
void doubleClick(ObjectsPane* pane, QTreeWidgetItem* item) {
    for (QTreeWidgetItem* p = item->parent(); p != nullptr; p = p->parent())
        p->setExpanded(true);
    pane->tree()->scrollToItem(item);
    QCoreApplication::processEvents();
    const QRect r = pane->tree()->visualItemRect(item);
    // a double click is press, release, double-click event, release
    QTest::mouseClick(pane->tree()->viewport(), Qt::LeftButton, {}, r.center());
    QTest::mouseDClick(pane->tree()->viewport(), Qt::LeftButton, {}, r.center());
}

QString shown(SimWindow& w) {
    return QStringLiteral("%1:%2")
        .arg(QFileInfo(w.source()->filePath()).fileName())
        .arg(w.source()->cursorLine());
}

QString selected(SimWindow& w) {
    return w.source()->view()->textCursor().selectedText();
}

QAction* entry(QMenu* menu, const char* name) {
    return menu->findChild<QAction*>(name);
}

#define PREPARE(d, src, file)                                                                                \
    if (!(d).nvc.found)                                                                                      \
        QSKIP("nvc not found (set LVDSIM_NVC)");                                                             \
    QVERIFY2((d).prepare(src, file), "could not analyse the test design")

/// Starts the simulation and runs until the store and the panes are filled.
bool start(SimWindow& w, const QString& top) {
    if (!w.startSimulation({top}))
        return false;
    if (!w.waitUntilIdle())
        return false;
    w.sendCommand("run 20 ns");
    return w.waitUntilIdle() && w.store() != nullptr;
}

} // namespace

class TestViewDeclaration : public QObject {
    Q_OBJECT
private slots:
    void vhdlObjectsRegionsAndHistory() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("session.ini"));
        w.setDialogsEnabled(false);
        w.resize(1400, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        // no simulation: the entries say why
        QVERIFY(w.declarationBlocker().contains("running simulation"));
        QVERIFY(start(w, "work.t"));
        QCOMPARE(w.declarationLocator().support(),
                 DeclarationLocator::Support::Yes); // the first prompt asked
        QVERIFY(w.declarationBlocker().isEmpty());
        QVERIFY(!w.transcript()->text().contains("Unreadable"));

        ObjectsPane* objects = w.objects();
        objects->setScope({}); // the whole hierarchy

        // an internal signal of the top level
        QTreeWidgetItem* clk = objectItem(objects, "t.clk");
        QVERIFY(clk != nullptr);
        doubleClick(objects, clk);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:30"));
        QCOMPARE(selected(w), QString("clk"));
        QVERIFY(w.wave()->rowCount() == 0); // double-click does not add to the wave

        // a port collapsed into the signal of the parent: the port declaration of the instance
        QTreeWidgetItem* port = objectItem(objects, "t.uut.clk");
        QVERIFY(port != nullptr);
        doubleClick(objects, port);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:3"));
        QCOMPARE(selected(w), QString("clk"));

        // a signal of the architecture and one inside a generate
        w.viewDeclaration("/uut/r");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:8"));
        QTreeWidgetItem* gen = objectItem(objects, "t.uut.g(1).t");
        QVERIFY2(gen != nullptr, "no generate signal in the Objects pane");
        doubleClick(objects, gen);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:20"));
        QCOMPARE(selected(w), QString("t"));

        // a generic (no Objects row: asked by path, as the console does)
        w.viewDeclaration("/uut/width");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:2"));
        QCOMPARE(selected(w), QString("width"));

        // the same path again comes from the cache
        const int cached = w.declarationLocator().requestsSent();
        w.viewDeclaration("/uut/width");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(w.declarationLocator().requestsSent(), cached);

        // Sim: an instance shows its architecture, View Instantiation the statement in the parent
        StructurePane* sim = w.structure();
        QTreeWidgetItem* uut = findItem(sim->tree()->topLevelItem(0), "uut");
        QVERIFY(uut != nullptr);
        emit sim->tree()->itemDoubleClicked(uut, 0);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:7"));
        QCOMPARE(selected(w), QString("rtl"));
        std::unique_ptr<QMenu> simMenu(sim->createContextMenu(uut));
        entry(simMenu.get(), "viewInstantiation")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:34"));
        QCOMPARE(selected(w), QString("uut"));
        QTreeWidgetItem* g1 = findItem(sim->tree()->topLevelItem(0), "g(1)");
        QVERIFY(g1 != nullptr);
        std::unique_ptr<QMenu> genMenu(sim->createContextMenu(g1));
        entry(genMenu.get(), "viewDefinition")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:19"));
        QTreeWidgetItem* top = findItem(sim->tree()->topLevelItem(0), "t");
        std::unique_ptr<QMenu> topMenu(sim->createContextMenu(top));
        QVERIFY(!entry(topMenu.get(), "viewInstantiation")->isEnabled());
        entry(topMenu.get(), "viewDefinition")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:29"));

        // Processes: View Source opens the process statement
        int row = -1;
        for (int i = 0; i < w.processes()->processCount(); ++i)
            if (w.processes()->processAt(i).path == QLatin1String("/uut/reg"))
                row = i;
        QVERIFY(row >= 0);
        std::unique_ptr<QMenu> procMenu(w.processes()->createContextMenu(row));
        entry(procMenu.get(), "viewSource")->trigger();
        QCOMPARE(shown(w), QString("t.vhd:10"));

        // Back and Forward walk the jumps
        QVERIFY(w.source()->goBack());
        QCOMPARE(shown(w), QString("t.vhd:29"));
        QVERIFY(w.source()->goBack());
        QCOMPARE(shown(w), QString("t.vhd:19"));
        QVERIFY(w.source()->goForward());
        QCOMPARE(shown(w), QString("t.vhd:29"));

        // Wave and List: the context menus of a signal
        lvd::wave::AddWaveRequest req;
        req.patterns = {"/t/uut/r"};
        w.wave()->addWaveSignals(req);
        QCOMPARE(w.wave()->rowCount(), 1);
        std::unique_ptr<QMenu> waveMenu(w.wave()->createContextMenu(0));
        QVERIFY2(entry(waveMenu.get(), "viewDeclaration")->isEnabled(),
                 qPrintable(entry(waveMenu.get(), "viewDeclaration")->toolTip()));
        entry(waveMenu.get(), "viewDeclaration")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:8"));
        QCOMPARE(w.list()->addPaths({"/t/q"}), 1);
        std::unique_ptr<QMenu> listMenu(w.list()->createHeaderMenu(1));
        entry(listMenu.get(), "viewDeclaration")->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:31"));

        // Objects: Return still adds to the wave
        objects->tree()->clearSelection();
        port->setSelected(true);
        objects->tree()->setCurrentItem(port);
        QTest::keyClick(objects->tree(), Qt::Key_Return);
        QCOMPARE(w.wave()->rowCount(), 2);

        // an unknown name and a source file that is not there are reported in the transcript
        w.viewDeclaration("/uut/nothing");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("View Declaration: /uut/nothing"));
        QVERIFY(QFile::rename(d.file("t.vhd"), d.file("t.vhd.moved")));
        w.viewDeclaration("/uut/q");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("Source file t.vhd not found"));
        QVERIFY(QFile::rename(d.file("t.vhd.moved"), d.file("t.vhd")));
        w.viewDeclaration("/uut/q"); // cached, and found again
        QCOMPARE(shown(w), QString("t.vhd:4"));

        // a restart forgets the answers (and asks the kernel again)
        w.restartSimulation();
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.declarationLocator().isCached("/uut/q"));
        QCOMPARE(w.declarationLocator().support(), DeclarationLocator::Support::Yes);
        w.stopSimulation();
        QVERIFY(w.declarationBlocker().contains("running simulation"));
    }

    void processVariableFromLocals() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("session.ini"));
        w.setDialogsEnabled(false);
        w.resize(1400, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.startSimulation({"work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.toggleLineBreakpoint(d.file("t.vhd"), 14));
        QVERIFY(w.waitUntilIdle());
        w.sendCommand("run 50 ns");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.stoppedAtSource());
        QTreeWidgetItem* count = w.locals()->itemOf("count");
        QVERIFY(count != nullptr);
        std::unique_ptr<QMenu> menu(w.locals()->createContextMenu(count));
        QAction* decl = entry(menu.get(), "viewDeclaration");
        QVERIFY2(decl->isEnabled(), qPrintable(decl->toolTip()));
        decl->trigger();
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.vhd:11"));
        QCOMPARE(selected(w), QString("count"));
        QCOMPARE(w.source()->executionLine(), 14); // the stop marker stays
        w.stopSimulation();
    }

    void verilogNetsAndInstances() {
        AnalysedDesign d;
        PREPARE(d, kVerilog, "t.v");
        SimWindow w(d.environment(), d.file("session.ini"));
        w.setDialogsEnabled(false);
        w.resize(1400, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(start(w, "work.t"));
        w.objects()->setScope({});
        QTreeWidgetItem* n = objectItem(w.objects(), "t.u.n");
        QVERIFY(n != nullptr);
        doubleClick(w.objects(), n);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.v:2"));
        QCOMPARE(selected(w), QString("n"));
        QTreeWidgetItem* clk = objectItem(w.objects(), "t.clk");
        QVERIFY(clk != nullptr);
        doubleClick(w.objects(), clk);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.v:9"));
        w.viewRegion("/u/", false);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.v:1"));
        QCOMPARE(selected(w), QString("sub"));
        w.viewRegion("/u/", true);
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(shown(w), QString("t.v:12"));
        QCOMPARE(selected(w), QString("u"));
        w.stopSimulation();
    }

    void librarySourceWithoutSimulation() {
        AnalysedDesign d;
        PREPARE(d, kDesign, "t.vhd");
        SimWindow w(d.environment(), d.file("session.ini"));
        w.setDialogsEnabled(false);
        w.refreshLibraries();
        LibraryPane* lib = w.library();
        QTreeWidgetItem* work = nullptr;
        for (int i = 0; i < lib->tree()->topLevelItemCount(); ++i)
            if (lib->tree()->topLevelItem(i)->text(0) == QLatin1String("work"))
                work = lib->tree()->topLevelItem(i);
        QVERIFY(work != nullptr);
        QTreeWidgetItem* sub = findItem(work, "sub");
        QVERIFY(sub != nullptr);
        QSignalSpy sims(lib, &LibraryPane::simulateRequested);
        lib->itemDoubleClicked(sub);
        QVERIFY(w.waitForLibraryTool());
        QCoreApplication::processEvents();
        QCOMPARE(shown(w), QString("t.vhd:1"));
        QCOMPARE(selected(w), QString("sub"));
        QTreeWidgetItem* rtl = findItem(sub, "rtl");
        QVERIFY(rtl != nullptr);
        std::unique_ptr<QMenu> menu(lib->createContextMenu(rtl));
        entry(menu.get(), "viewSource")->trigger();
        QVERIFY(w.waitForLibraryTool());
        QCoreApplication::processEvents();
        QCOMPARE(shown(w), QString("t.vhd:7"));
        QCOMPARE(sims.count(), 0);
        QVERIFY(!w.simulationRunning()); // never a simulation
    }
};

QTEST_MAIN(TestViewDeclaration)
#include "test_view_declaration.moc"
