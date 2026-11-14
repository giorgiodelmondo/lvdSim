// SPDX-License-Identifier: GPL-3.0-or-later
// Source navigation without a simulator: store paths to describe paths, the declaration cache and the kernel
// support probe, the Source pane jumps (selection, flash, Back/Forward), and the View Declaration / View
// Definition / View Instantiation / View Source entries of the Objects, Sim, Processes, Library, Locals,
// Wave and List views.
#include "app/declaration_locator.hpp"
#include "app/library_pane.hpp"
#include "app/locals_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/processes_pane.hpp"
#include "app/source_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/unit_source.hpp"
#include "wave/list_window.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/wave_commands.hpp>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

#include <memory>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& text) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(text);
    return path;
}

const char* kSub = "entity sub is\n"                       // 1
                   "  generic ( width : natural := 4 );\n" // 2
                   "  port ( clk : in bit;\n"              // 3
                   "         q   : out bit );\n"           // 4
                   "end entity;\n"                         // 5
                   "\n"                                    // 6
                   "architecture rtl of sub is\n"          // 7
                   "\tsignal r : bit;\n"                   // 8 (a tab before the declaration)
                   "begin\n"                               // 9
                   "  q <= r;\n"                           // 10
                   "end architecture;\n";                  // 11

/// A shell that answers from a table: path -> reply line, or no entry for a failing command.
struct FakeShell {
    QHash<QString, QString> replies;
    QStringList sent;
    bool deferred = false;
    QVector<std::function<void()>> later;

    DeclarationLocator::Request request() {
        return [this](const QString& command, std::function<void(const QString&)> ok,
                      std::function<void(const QString&)> failed) {
            sent << command;
            const QString path = command.section(QLatin1Char('{'), 1).chopped(1);
            auto answer = [this, path, ok, failed] {
                if (replies.contains(path))
                    ok(replies.value(path));
                else
                    failed(QStringLiteral("cannot find name '%1'").arg(path));
            };
            if (deferred)
                later.push_back(answer);
            else
                answer();
        };
    }
};

const char* kTopReply = R"j({"v":1,"objects":[{"name":"t","path":"/","kind":"region","unit":"work.t(test)",)j"
                        R"j("unit_file":"t.vhd","unit_line":"20"}]})j";
const char* kOldTopReply = R"({"v":1,"objects":[{"name":"t","path":"/","kind":"region"}]})";
const char* kClkReply =
    R"({"v":1,"objects":[{"name":"clk","path":"/uut/clk","kind":"signal","type":"BIT","mode":"in","file":"sub.vhd","line":"3","column":"10"}]})";

std::shared_ptr<WaveStore> smallStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const ScopeId dut = st->addScope(top, "dut");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(dut, "cnt", SignalKind::Logic, 8);
    st->mutableSignal(clk).appendLogic(0, "0");
    st->mutableSignal(cnt).appendLogic(0, "00000000");
    st->extendTimeRange(0);
    st->extendTimeRange(50000000);
    return st;
}

WaveStore treeStore() {
    WaveStore st;
    const ScopeId t = st.addScope(kRootScope, "t");
    st.addSignal(t, "clk", SignalKind::Logic, 1);
    const ScopeId uut = st.addScope(t, "uut");
    st.addSignal(uut, "clk", SignalKind::Logic, 1);
    st.addSignal(uut, "r", SignalKind::Logic, 8);
    return st;
}

QTreeWidgetItem* findItem(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name)
            return parent->child(i);
        if (QTreeWidgetItem* deeper = findItem(parent->child(i), name))
            return deeper;
    }
    return nullptr;
}

QAction* entry(QMenu* menu, const char* name) {
    return menu->findChild<QAction*>(name);
}

} // namespace

class TestSourceNavigation : public QObject {
    Q_OBJECT
private slots:
    // ---- paths ---------------------------------------------------------------------------------------
    void storePathsBecomeDescribePaths() {
        QCOMPARE(shellPathOfStoreSignal("t.clk"), QString("/clk"));
        QCOMPARE(shellPathOfStoreSignal("t.uut.g(0).t"), QString("/uut/g(0)/t"));
        QCOMPARE(shellPathOfStoreSignal("t.mem[3]"), QString("/mem"));
        QCOMPARE(shellPathOfStoreSignal("t.u.d[3:0]"), QString("/u/d"));
        QCOMPARE(shellPathOfStoreSignal("t"), QString()); // a scope, not a signal
        QCOMPARE(shellPathOfStoreScope("t"), QString("/"));
        QCOMPARE(shellPathOfStoreScope("t.uut"), QString("/uut/"));
        QCOMPARE(shellPathOfStoreScope("t.uut.g(1)"), QString("/uut/g(1)/"));
        QCOMPARE(shellPathOfStoreScope(""), QString());
        QCOMPARE(describeLocationCommand("/uut/g(0)/t"), QString("describe -json -location {/uut/g(0)/t}"));
    }

    // ---- the cache and the probe ---------------------------------------------------------------------
    void locatorAsksOncePerPathAndSession() {
        FakeShell sh;
        sh.replies.insert("/", kTopReply);
        sh.replies.insert("/uut/clk", kClkReply);
        DeclarationLocator loc;
        loc.setRequest(sh.request());
        QCOMPARE(loc.support(), DeclarationLocator::Support::Unknown);
        QVERIFY(loc.unavailableReason(true).isEmpty()); // unknown: asked when used
        QVERIFY(!loc.unavailableReason(false).isEmpty());
        loc.probe();
        QCOMPARE(loc.support(), DeclarationLocator::Support::Yes);
        QCOMPARE(sh.sent, QStringList{"describe -json -location {/}"});

        int calls = 0;
        DescribedObject got;
        auto done = [&](const lvd::Result<DescribedObject>& r) {
            ++calls;
            QVERIFY(r.ok());
            got = r.value();
        };
        loc.describe("/uut/clk", done);
        loc.describe("/uut/clk", done);
        QCOMPARE(calls, 2);
        QCOMPARE(sh.sent.size(), 2); // the second answer came from the cache
        QCOMPARE(got.line, 3);
        QCOMPARE(got.column, 10);
        QVERIFY(loc.isCached("/uut/clk"));
        loc.describe("/", done); // the probe's answer is cached too
        QCOMPARE(sh.sent.size(), 2);
        QCOMPARE(got.unitLine, 20);

        // an unknown name fails, and is asked again next time
        bool failed = false;
        loc.describe("/nothing", [&](const lvd::Result<DescribedObject>& r) { failed = !r.ok(); });
        QVERIFY(failed);
        QVERIFY(!loc.isCached("/nothing"));

        loc.reset(); // a new session or a restart
        QCOMPARE(loc.cachedCount(), 0);
        QCOMPARE(loc.support(), DeclarationLocator::Support::Unknown);
        loc.describe("/uut/clk", done);
        QCOMPARE(sh.sent.size(), 4);
    }

    void locatorWaitsForOneReplyPerPath() {
        FakeShell sh;
        sh.deferred = true;
        sh.replies.insert("/uut/clk", kClkReply);
        DeclarationLocator loc;
        loc.setRequest(sh.request());
        int calls = 0;
        loc.describe("/uut/clk", [&](const lvd::Result<DescribedObject>&) { ++calls; });
        loc.describe("/uut/clk", [&](const lvd::Result<DescribedObject>&) { ++calls; });
        QCOMPARE(sh.sent.size(), 1); // the second waits for the first reply
        sh.later.takeFirst()();
        QCOMPARE(calls, 2);
    }

    void locatorDropsAnswersOfAnEarlierSession() {
        FakeShell sh;
        sh.deferred = true;
        sh.replies.insert("/uut/clk", kClkReply);
        DeclarationLocator loc;
        loc.setRequest(sh.request());
        int calls = 0;
        loc.describe("/uut/clk", [&](const lvd::Result<DescribedObject>&) { ++calls; });
        loc.reset();
        sh.later.takeFirst()();
        QCOMPARE(calls, 0);
        QCOMPARE(loc.cachedCount(), 0);
    }

    void oldKernelsDisableViewDeclaration() {
        // A kernel without the location members answers the probe without them
        FakeShell sh;
        sh.replies.insert("/", kOldTopReply);
        DeclarationLocator loc;
        loc.setRequest(sh.request());
        loc.probe();
        QCOMPARE(loc.support(), DeclarationLocator::Support::No);
        const QString why = loc.unavailableReason(true);
        QVERIFY(why.contains("does not report source locations"));

        // and the Objects pane shows the entry greyed out with that reason
        const WaveStore st = treeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        pane.setDeclarationGate([&] { return loc.unavailableReason(true); });
        QSignalSpy views(&pane, &ObjectsPane::viewDeclarationRequested);
        QTreeWidgetItem* clk = findItem(pane.tree()->topLevelItem(0), "r");
        pane.tree()->setCurrentItem(clk);
        std::unique_ptr<QMenu> menu(pane.createContextMenu());
        QAction* decl = entry(menu.get(), "viewDeclaration");
        QVERIFY(decl != nullptr);
        QVERIFY(!decl->isEnabled());
        QCOMPARE(decl->toolTip(), why);
        pane.itemDoubleClicked(clk);
        QCOMPARE(views.count(), 0);

        // a kernel that refuses the option is old as well
        FakeShell refusing;
        DeclarationLocator loc2;
        loc2.setRequest(refusing.request());
        loc2.probe();
        QCOMPARE(loc2.support(), DeclarationLocator::Support::No);
    }

    // ---- the Source pane -----------------------------------------------------------------------------
    void openAtSelectsTheIdentifierAndFlashes() {
        QTemporaryDir dir;
        const QString sub = writeFile(dir, "sub.vhd", kSub);
        SourcePane pane;
        pane.resize(600, 400);
        QVERIFY(pane.openAt({sub, 3, 10, "clk"}));
        QCOMPARE(pane.cursorLine(), 3);
        QCOMPARE(pane.view()->textCursor().selectedText(), QString("clk"));
        QCOMPARE(pane.view()->flashingLine(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(pane.view()->flashingLine(), 0, 3000);

        // a column that does not match (a tab counted differently): the word on the line
        QVERIFY(pane.openAt({sub, 8, 9, "r"}));
        QCOMPARE(pane.cursorLine(), 8);
        QCOMPARE(pane.view()->textCursor().selectedText(), QString("r"));
        QCOMPARE(pane.view()->textCursor().selectionStart() - pane.view()->textCursor().block().position(),
                 8);

        // the architecture name of a definition, without a column
        QVERIFY(pane.openAt({sub, 7, 0, "rtl"}));
        QCOMPARE(pane.view()->textCursor().selectedText(), QString("rtl"));

        // a file that cannot be read keeps the history and says why
        const int before = pane.historySize();
        QVERIFY(!pane.openAt({dir.filePath("missing.vhd"), 1, 0, {}}));
        QVERIFY(!pane.lastError().isEmpty());
        QCOMPARE(pane.historySize(), before);
    }

    void backAndForwardWalkTheJumps() {
        QTemporaryDir dir;
        const QString sub = writeFile(dir, "sub.vhd", kSub);
        const QString top =
            writeFile(dir, "top.vhd", "entity top is\nend;\n\narchitecture a of top is\nbegin\nend;\n");
        SourcePane pane;
        pane.resize(600, 400);
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));
        QVERIFY(!pane.backButton()->isEnabled());
        QVERIFY(!pane.forwardButton()->isEnabled());
        QVERIFY(pane.openAt({top, 4, 0, "a"}));
        QVERIFY(pane.openAt({sub, 3, 10, "clk"}));
        QCOMPARE(pane.historySize(), 2);
        QVERIFY(pane.backButton()->isEnabled());
        QVERIFY(!pane.forwardButton()->isEnabled());

        pane.backButton()->click();
        QCOMPARE(QFileInfo(pane.filePath()).fileName(), QString("top.vhd"));
        QCOMPARE(pane.cursorLine(), 4);
        QVERIFY(pane.forwardButton()->isEnabled());

        // Alt+Right and Alt+Left as shortcuts of the pane
        pane.view()->setFocus();
        QTest::keyClick(pane.view(), Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(QFileInfo(pane.filePath()).fileName(), QString("sub.vhd"));
        QCOMPARE(pane.cursorLine(), 3);
        QTest::keyClick(pane.view(), Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(QFileInfo(pane.filePath()).fileName(), QString("top.vhd"));

        // where the cursor went before leaving is where Back returns to
        pane.view()->showLine(6);
        QVERIFY(pane.goForward());
        QVERIFY(pane.goBack());
        QCOMPARE(pane.cursorLine(), 6);

        // a new jump drops the forward entries
        QVERIFY(pane.openAt({sub, 7, 0, "rtl"}));
        QVERIFY(!pane.canGoForward());
        QCOMPARE(pane.historySize(), 2);

        // a file opened another way (a stop at a line) is a place Back returns to
        QVERIFY(pane.openFile(top, 2));
        QVERIFY(pane.openAt({sub, 3, 10, "clk"}));
        QVERIFY(pane.goBack());
        QCOMPARE(QFileInfo(pane.filePath()).fileName(), QString("top.vhd"));
        QCOMPARE(pane.cursorLine(), 2);
    }

    // ---- Objects -------------------------------------------------------------------------------------
    void objectsDoubleClickViewsAndReturnAdds() {
        const WaveStore st = treeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        pane.resize(400, 400);
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));
        QSignalSpy views(&pane, &ObjectsPane::viewDeclarationRequested);
        QSignalSpy adds(&pane, &ObjectsPane::addToWave);
        QTreeWidgetItem* uut = findItem(pane.tree()->invisibleRootItem(), "uut");
        QVERIFY(uut != nullptr);
        uut->setExpanded(true);
        QTreeWidgetItem* r = findItem(uut, "r");
        QVERIFY(r != nullptr);
        pane.tree()->scrollToItem(r);
        QCoreApplication::processEvents();
        const QRect rect = pane.tree()->visualItemRect(r);
        QCOMPARE(pane.tree()->itemAt(rect.center()), r);
        // a double click is press, release, double-click event, release
        QTest::mouseClick(pane.tree()->viewport(), Qt::LeftButton, {}, rect.center());
        QTest::mouseDClick(pane.tree()->viewport(), Qt::LeftButton, {}, rect.center());
        QCOMPARE(views.count(), 1);
        QCOMPARE(views.at(0).at(0).toString(), QString("t.uut.r"));
        QCOMPARE(adds.count(), 0); // double-click no longer adds to the wave

        // a scope only expands or collapses
        pane.itemDoubleClicked(uut);
        QCOMPARE(views.count(), 1);

        pane.tree()->setCurrentItem(r);
        QTest::keyClick(pane.tree(), Qt::Key_Return);
        QCOMPARE(adds.count(), 1);
        QCOMPARE(adds.at(0).at(0).toStringList(), QStringList{"t.uut.r"});
    }

    void objectsContextMenu() {
        const WaveStore st = treeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        QSignalSpy views(&pane, &ObjectsPane::viewDeclarationRequested);
        QSignalSpy lists(&pane, &ObjectsPane::addToList);
        QTreeWidgetItem* r = findItem(pane.tree()->invisibleRootItem(), "r");
        QVERIFY(r != nullptr);
        pane.tree()->clearSelection();
        r->setSelected(true);
        pane.tree()->setCurrentItem(r);
        std::unique_ptr<QMenu> menu(pane.createContextMenu());
        QStringList texts;
        for (QAction* a : menu->actions())
            if (!a->isSeparator())
                texts << a->text();
        QCOMPARE(texts.mid(0, 5), (QStringList{"View Declaration", "Add to Wave", "Add to List",
                                               "Add to Memory", "Copy Path"}));
        QVERIFY(entry(menu.get(), "viewDeclaration")->isEnabled()); // no gate: enabled
        QVERIFY(!entry(menu.get(), "openMemory")->isEnabled());     // not a memory
        entry(menu.get(), "viewDeclaration")->trigger();
        QCOMPARE(views.count(), 1);
        entry(menu.get(), "addToList")->trigger();
        QCOMPARE(lists.count(), 1);
        QCOMPARE(lists.at(0).at(0).toStringList(), QStringList{"t.uut.r"});
        entry(menu.get(), "copyPath")->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QString("/t/uut/r"));

        // a scope has no declaration to show
        QTreeWidgetItem* uut = findItem(pane.tree()->invisibleRootItem(), "uut");
        pane.tree()->setCurrentItem(uut);
        std::unique_ptr<QMenu> onScope(pane.createContextMenu());
        QVERIFY(!entry(onScope.get(), "viewDeclaration")->isEnabled());

        // the gate's reason is the tool tip
        pane.setDeclarationGate([] { return QStringLiteral("Needs a running simulation"); });
        pane.tree()->setCurrentItem(r);
        std::unique_ptr<QMenu> gated(pane.createContextMenu());
        QVERIFY(!entry(gated.get(), "viewDeclaration")->isEnabled());
        QCOMPARE(entry(gated.get(), "viewDeclaration")->toolTip(), QString("Needs a running simulation"));
    }

    // ---- Sim -----------------------------------------------------------------------------------------
    void structureDefinitionAndInstantiation() {
        const WaveStore st = treeStore();
        StructurePane pane;
        pane.setStore(&st);
        QSignalSpy defs(&pane, &StructurePane::viewDefinitionRequested);
        QSignalSpy insts(&pane, &StructurePane::viewInstantiationRequested);
        QSignalSpy selected(&pane, &StructurePane::scopeSelected);
        QTreeWidgetItem* design = pane.tree()->topLevelItem(0);
        QTreeWidgetItem* t = findItem(design, "t");
        QTreeWidgetItem* uut = findItem(design, "uut");
        QVERIFY(t && uut);

        emit pane.tree()->itemDoubleClicked(uut, 0);
        QCOMPARE(defs.count(), 1);
        QCOMPARE(defs.at(0).at(0).toString(), QString("t.uut"));
        emit pane.tree()->itemDoubleClicked(design, 0); // "design" is no scope of the simulator
        QCOMPARE(defs.count(), 1);

        pane.tree()->setCurrentItem(uut); // selecting still chooses the scope of the Objects pane
        QCOMPARE(selected.last().at(0).toString(), QString("t.uut"));

        std::unique_ptr<QMenu> onUut(pane.createContextMenu(uut));
        QVERIFY(entry(onUut.get(), "viewDefinition")->isEnabled());
        QVERIFY(entry(onUut.get(), "viewInstantiation")->isEnabled());
        entry(onUut.get(), "viewInstantiation")->trigger();
        QCOMPARE(insts.count(), 1);
        QCOMPARE(insts.at(0).at(0).toString(), QString("t.uut"));

        std::unique_ptr<QMenu> onTop(pane.createContextMenu(t));
        QVERIFY(entry(onTop.get(), "viewDefinition")->isEnabled());
        QVERIFY(!entry(onTop.get(), "viewInstantiation")->isEnabled()); // nothing instantiates the top

        pane.setDefinitionGate([] { return QStringLiteral("old kernel"); });
        std::unique_ptr<QMenu> gated(pane.createContextMenu(uut));
        QVERIFY(!entry(gated.get(), "viewDefinition")->isEnabled());
        QCOMPARE(entry(gated.get(), "viewDefinition")->toolTip(), QString("old kernel"));
        emit pane.tree()->itemDoubleClicked(uut, 0);
        QCOMPARE(defs.count(), 1);
    }

    // ---- Processes -----------------------------------------------------------------------------------
    void processesViewSource() {
        ProcessesPane pane;
        ProcessInfo p;
        p.name = "reg";
        p.path = "/uut/reg";
        p.state = "waiting";
        p.file = "sub.vhd";
        p.line = 12;
        pane.setProcesses({p});
        QSignalSpy opens(&pane, &ProcessesPane::openRequested);
        std::unique_ptr<QMenu> menu(pane.createContextMenu(0));
        entry(menu.get(), "viewSource")->trigger();
        QCOMPARE(opens.count(), 1);
        QCOMPARE(opens.at(0).at(0).toString(), QString("sub.vhd"));
        QCOMPARE(opens.at(0).at(1).toInt(), 12);
        std::unique_ptr<QMenu> none(pane.createContextMenu(-1));
        QVERIFY(!entry(none.get(), "viewSource")->isEnabled());
    }

    // ---- Library -------------------------------------------------------------------------------------
    void libraryDoubleClickViewsSourceAndNeverSimulates() {
        lvd::tools::LibraryInfo work;
        work.name = "work";
        work.units.push_back({"counter", {"rtl"}, true});
        LibraryPane pane;
        pane.setLibraries({work});
        QSignalSpy sims(&pane, &LibraryPane::simulateRequested);
        QSignalSpy views(&pane, &LibraryPane::viewSourceRequested);
        QTreeWidgetItem* lib = pane.tree()->topLevelItem(0);
        QTreeWidgetItem* unit = lib->child(0);
        QTreeWidgetItem* arch = unit->child(0);

        const bool expanded = lib->isExpanded();
        pane.itemDoubleClicked(lib);
        QCOMPARE(lib->isExpanded(), !expanded); // a library expands or collapses
        QCOMPARE(views.count(), 0);
        pane.itemDoubleClicked(unit);
        pane.itemDoubleClicked(arch);
        QCOMPARE(views.count(), 2);
        QCOMPARE(views.at(0).at(0).toString(), QString("work.counter"));
        QCOMPARE(views.at(0).at(1).toString(), QString());
        QCOMPARE(views.at(1).at(1).toString(), QString("rtl"));
        QCOMPARE(sims.count(), 0);

        std::unique_ptr<QMenu> menu(pane.createContextMenu(unit));
        QVERIFY(entry(menu.get(), "viewSource")->isEnabled());
        QVERIFY(entry(menu.get(), "simulate")->isEnabled());
        std::unique_ptr<QMenu> onLib(pane.createContextMenu(lib));
        QVERIFY(!entry(onLib.get(), "viewSource")->isEnabled());

        pane.setSourceGate([](const QString&, const QString&) { return QStringLiteral("not known"); });
        std::unique_ptr<QMenu> gated(pane.createContextMenu(unit));
        QVERIFY(!entry(gated.get(), "viewSource")->isEnabled());
        QCOMPARE(entry(gated.get(), "viewSource")->toolTip(), QString("not known"));
        pane.itemDoubleClicked(unit);
        QCOMPARE(views.count(), 2);
        QCOMPARE(sims.count(), 0);
    }

    void printDepsNamesTheSource() {
        const QString out = "# Generated by nvc 1.24-devel\n\n"
                            "/tmp/pd/work/WORK.SUB-RTL: src/sub.vhd /opt/lib/std.08/STD.STANDARD\n\n"
                            "/tmp/pd/work/WORK.SUB: src/sub_ent.vhd /opt/lib/std.08/STD.STANDARD\n"
                            "C:/x/work/WORK.TOP: top.vhd\n";
        QCOMPARE(sourceFromPrintDeps(out, "work", "sub", {}), QString("src/sub_ent.vhd"));
        QCOMPARE(sourceFromPrintDeps(out, "work", "sub", "rtl"), QString("src/sub.vhd"));
        QCOMPARE(sourceFromPrintDeps(out, "WORK", "top", {}), QString("top.vhd")); // drive colon in the path
        QCOMPARE(sourceFromPrintDeps(out, "work", "other", {}), QString());
        QCOMPARE(sourceFromPrintDeps("** Fatal: WORK.M is not a VHDL design unit\n", "work", "m", {}),
                 QString());
    }

    void projectFilesNameTheUnit() {
        QTemporaryDir dir;
        writeFile(dir, "sub.vhd", kSub);
        writeFile(dir, "m.v", "// a module\n\nmodule m(input a);\nendmodule\n");
        lvd::tools::proj::Project p;
        lvd::tools::proj::Library lib;
        lib.name = "work";
        lvd::tools::proj::SourceFile f;
        f.path = "sub.vhd";
        lib.files.push_back(f);
        f.path = "m.v";
        lib.files.push_back(f);
        p.libraries.push_back(lib);
        const auto abs = [&dir](const QString& f) { return dir.filePath(f); };
        const auto e = findUnitInProject(p, abs, "WORK", "sub", {});
        QVERIFY(e.has_value());
        QCOMPARE(QFileInfo(e->file).fileName(), QString("sub.vhd"));
        QCOMPARE(e->line, 1);
        const auto a = findUnitInProject(p, abs, "work", "sub", "rtl");
        QVERIFY(a.has_value());
        QCOMPARE(a->line, 7);
        QCOMPARE(a->name, QString("rtl"));
        const auto m = findUnitInProject(p, abs, "work", "m", {});
        QVERIFY(m.has_value());
        QCOMPARE(m->line, 3);
        QVERIFY(!findUnitInProject(p, abs, "other", "sub", {}).has_value());
        QVERIFY(!findUnitInProject(p, abs, "work", "nothing", {}).has_value());
        QCOMPARE(unitLineInFile(dir.filePath("sub.vhd"), "sub", "rtl"), 7);
    }

    // ---- Locals --------------------------------------------------------------------------------------
    void localsViewDeclaration() {
        LocalsPane pane;
        LocalsReply reply;
        VariableInfo v;
        v.name = "count";
        v.kind = "variable";
        v.type = "NATURAL";
        v.value = "(a => 1, b => 2)";
        v.hasValue = true;
        reply.variables = {v};
        pane.setLocals("p|x", reply, 1);
        QSignalSpy views(&pane, &LocalsPane::viewDeclarationRequested);
        QTreeWidgetItem* count = pane.itemOf("count");
        QTreeWidgetItem* element = pane.itemOf("count.a");
        QVERIFY(count && element);
        QCOMPARE(LocalsPane::variableOf(element), QString("count"));
        std::unique_ptr<QMenu> menu(pane.createContextMenu(element));
        entry(menu.get(), "viewDeclaration")->trigger();
        QCOMPARE(views.count(), 1);
        QCOMPARE(views.at(0).at(0).toString(), QString("count"));
        pane.setDeclarationGate([](const QString&) { return QStringLiteral("a subprogram"); });
        std::unique_ptr<QMenu> gated(pane.createContextMenu(count));
        QVERIFY(!entry(gated.get(), "viewDeclaration")->isEnabled());
    }

    // ---- Wave and List -------------------------------------------------------------------------------
    void waveRowsOfferViewDeclaration() {
        WaveWidget w;
        w.resize(800, 300);
        w.setStore(smallStore());
        AddWaveRequest req;
        req.patterns = {"/top/clk", "/top/dut/cnt"};
        w.addWaveSignals(req);
        QVERIFY(w.rowCount() >= 2);
        // a bare widget has no simulator: no entry
        std::unique_ptr<QMenu> bare(w.createContextMenu(0));
        QVERIFY(entry(bare.get(), "viewDeclaration") == nullptr);

        w.setDeclarationGate([](const QString& path) {
            return path.contains(':') ? QStringLiteral("not of the simulation") : QString();
        });
        QSignalSpy views(&w, &WaveWidget::declarationRequested);
        std::unique_ptr<QMenu> menu(w.createContextMenu(1));
        QAction* decl = entry(menu.get(), "viewDeclaration");
        QVERIFY(decl && decl->isEnabled());
        decl->trigger();
        QCOMPARE(views.count(), 1);
        QCOMPARE(views.at(0).at(0).toString(), QString("top.dut.cnt"));
        std::unique_ptr<QMenu> nowhere(w.createContextMenu(-1));
        QVERIFY(!entry(nowhere.get(), "viewDeclaration")->isEnabled());
    }

    void listColumnsOfferViewDeclaration() {
        ListWindow lw;
        lw.setStore(smallStore());
        AddWaveRequest req;
        req.patterns = {"/top/clk"};
        QCOMPARE(lw.addList(req), 1);
        std::unique_ptr<QMenu> bare(lw.createHeaderMenu(1));
        QVERIFY(entry(bare.get(), "viewDeclaration") == nullptr);
        lw.setDeclarationGate([](const QString&) { return QString(); });
        QSignalSpy views(&lw, &ListWindow::declarationRequested);
        std::unique_ptr<QMenu> menu(lw.createHeaderMenu(1));
        entry(menu.get(), "viewDeclaration")->trigger();
        QCOMPARE(views.count(), 1);
        QCOMPARE(views.at(0).at(0).toString(), QString("top.clk"));
        std::unique_ptr<QMenu> time(lw.createHeaderMenu(0));
        QVERIFY(entry(time.get(), "viewDeclaration") == nullptr);
    }
};

QTEST_MAIN(TestSourceNavigation)
#include "test_source_navigation.moc"
