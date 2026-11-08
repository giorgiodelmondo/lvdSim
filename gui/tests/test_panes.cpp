// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/library_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/transcript_widget.hpp"

#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

#include <memory>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

WaveStore makeStore() {
    WaveStore st;
    const ScopeId tb = st.addScope(kRootScope, "tb");
    st.addSignal(tb, "clk", SignalKind::Logic, 1);
    st.addSignal(tb, "data", SignalKind::Logic, 8);
    const ScopeId dut = st.addScope(tb, "dut");
    st.addSignal(dut, "count", SignalKind::Logic, 16);
    st.addSignal(dut, "volt", SignalKind::Real, 64);
    st.addSignal(dut, "state", SignalKind::String, 0);
    return st;
}

QTreeWidgetItem* find(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i)
        if (parent->child(i)->text(0) == name) return parent->child(i);
    return nullptr;
}

}  // namespace

class TestPanes : public QObject {
    Q_OBJECT
private slots:
    // ---- objects ---------------------------------------------------------------------------------
    void treeShowsScopesAndSignals() {
        const WaveStore st = makeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        QCOMPARE(pane.scopeCount(), 2);
        QCOMPARE(pane.signalCount(), 5);
        QCOMPARE(pane.tree()->topLevelItemCount(), 1);

        QTreeWidgetItem* tb = pane.tree()->topLevelItem(0);
        QCOMPARE(tb->text(0), QString("tb"));
        QTreeWidgetItem* data = find(tb, "data");
        QVERIFY(data != nullptr);
        QCOMPARE(data->text(1), QString("logic"));
        QCOMPARE(data->text(2), QString("8"));
        QCOMPARE(find(find(tb, "dut"), "volt")->text(1), QString("real"));
        QCOMPARE(find(find(tb, "dut"), "state")->text(1), QString("text"));

        pane.setStore(nullptr);
        QCOMPARE(pane.tree()->topLevelItemCount(), 0);
    }

    void selectionGivesStorePaths() {
        const WaveStore st = makeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        QTreeWidgetItem* tb = pane.tree()->topLevelItem(0);

        pane.tree()->setCurrentItem(find(tb, "clk"));
        QCOMPARE(pane.selectedSignalPaths(), (QStringList{"tb.clk"}));

        pane.tree()->clearSelection();
        find(tb, "dut")->setSelected(true);  // a scope stands for everything in it
        QCOMPARE(pane.selectedSignalPaths(), (QStringList{"tb.dut.count", "tb.dut.volt", "tb.dut.state"}));

        find(find(tb, "dut"), "count")->setSelected(true);  // already included: not listed twice
        QCOMPARE(pane.selectedSignalPaths().size(), 3);

        pane.tree()->clearSelection();
        tb->setSelected(true);
        QCOMPARE(pane.selectedSignalPaths().size(), 5);
    }

    void activatingAddsToWave() {
        const WaveStore st = makeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        QSignalSpy spy(&pane, &ObjectsPane::addToWave);

        QTreeWidgetItem* clk = find(pane.tree()->topLevelItem(0), "clk");
        pane.tree()->setCurrentItem(clk);
        emit pane.tree()->itemActivated(clk, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toStringList(), (QStringList{"tb.clk"}));

        pane.tree()->clearSelection();
        emit pane.tree()->itemActivated(clk, 0);  // nothing selected: nothing added
        QCOMPARE(spy.count(), 1);
    }

    void filterHidesOtherSignals() {
        const WaveStore st = makeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        QCOMPARE(pane.visibleSignalCount(), 5);

        pane.setFilter("c*");
        QCOMPARE(pane.visibleSignalCount(), 2);  // clk, count
        pane.setFilter("COUNT");
        QCOMPARE(pane.visibleSignalCount(), 1);  // case-insensitive
        QTreeWidgetItem* tb = pane.tree()->topLevelItem(0);
        QVERIFY(!tb->isHidden());
        QVERIFY(find(tb, "data")->isHidden());

        // hidden signals are not added even when their scope is selected
        tb->setSelected(true);
        QCOMPARE(pane.selectedSignalPaths(), (QStringList{"tb.dut.count"}));

        pane.setFilter("nomatch");
        QCOMPARE(pane.visibleSignalCount(), 0);
        QVERIFY(tb->isHidden());  // scopes without a match disappear too

        pane.setFilter({});
        QCOMPARE(pane.visibleSignalCount(), 5);
    }

    // ---- scope selection ---------------------------------------------------------------------------
    void objectsCanShowOneScope() {
        const WaveStore st = makeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        QCOMPARE(pane.tree()->topLevelItemCount(), 1);  // the whole hierarchy: tb

        pane.setScope("tb.dut");
        QCOMPARE(pane.scope(), QString("tb.dut"));
        QCOMPARE(pane.tree()->topLevelItemCount(), 3);  // count, volt, state directly at the top
        QCOMPARE(pane.signalCount(), 3);
        QCOMPARE(pane.tree()->topLevelItem(0)->text(0), QString("count"));
        QCOMPARE(pane.selectedSignalPaths().size(), 0);
        pane.tree()->topLevelItem(0)->setSelected(true);
        QCOMPARE(pane.selectedSignalPaths(), (QStringList{"tb.dut.count"}));  // full store path

        pane.setScope("tb");  // own signals first-class, the sub-scope below
        QCOMPARE(pane.signalCount(), 5);
        QCOMPARE(pane.scopeCount(), 1);

        pane.setScope("nonexistent");  // unknown: everything
        QCOMPARE(pane.tree()->topLevelItemCount(), 1);
        pane.setScope("tb.dut");
        pane.setScope({});  // empty: everything
        QCOMPARE(pane.tree()->topLevelItemCount(), 1);
        QCOMPARE(pane.signalCount(), 5);
    }

    void theFilterStillWorksInAScope() {
        const WaveStore st = makeStore();
        ObjectsPane pane;
        pane.setStore(&st);
        pane.setScope("tb.dut");
        pane.setFilter("c*");
        QCOMPARE(pane.visibleSignalCount(), 1);
        pane.setScope("tb");  // the filter is kept
        QCOMPARE(pane.visibleSignalCount(), 2);  // clk, count
    }

    void structureShowsScopesOnly() {
        const WaveStore st = makeStore();
        StructurePane pane;
        QSignalSpy spy(&pane, &StructurePane::scopeSelected);
        pane.setStore(&st);
        QCOMPARE(pane.scopeCount(), 2);
        QCOMPARE(pane.tree()->topLevelItemCount(), 1);
        QTreeWidgetItem* root = pane.tree()->topLevelItem(0);
        QCOMPARE(root->text(0), QString("design"));
        QCOMPARE(root->childCount(), 1);  // tb
        QCOMPARE(root->child(0)->childCount(), 1);  // dut
        QCOMPARE(root->child(0)->child(0)->childCount(), 0);  // no signals in this tree

        QVERIFY(spy.count() >= 1);  // the design row is selected when the store is set
        QCOMPARE(spy.last().at(0).toString(), QString());

        pane.tree()->setCurrentItem(root->child(0)->child(0));
        QCOMPARE(spy.last().at(0).toString(), QString("tb.dut"));
        pane.tree()->setCurrentItem(root->child(0));
        QCOMPARE(spy.last().at(0).toString(), QString("tb"));
        pane.tree()->setCurrentItem(root);
        QCOMPARE(spy.last().at(0).toString(), QString());

        pane.setStore(nullptr);
        QCOMPARE(pane.tree()->topLevelItemCount(), 0);
    }

    void structureNamesTheKindOfScope() {
        WaveStore st;
        const ScopeId top = st.addScope(kRootScope, "top", 12);
        st.addScope(top, "blk", 17);
        st.addScope(top, "gen", 18);
        st.addScope(top, "rec", 15);
        st.addScope(kRootScope, "pkg", 21);
        StructurePane pane;
        pane.setStore(&st);
        QTreeWidgetItem* root = pane.tree()->topLevelItem(0);
        QCOMPARE(root->child(0)->text(1), QString("instance"));
        QCOMPARE(root->child(0)->child(0)->text(1), QString("block"));
        QCOMPARE(root->child(0)->child(1)->text(1), QString("generate"));
        QCOMPARE(root->child(0)->child(2)->text(1), QString("record"));
        QCOMPARE(root->child(1)->text(1), QString("package"));
    }

    // ---- libraries ---------------------------------------------------------------------------------
    void libraryPaneListsUnits() {
        lvd::tools::LibraryInfo work;
        work.name = "work";
        work.path = "work";
        work.units.push_back({"alu", {"a"}, false});
        work.units.push_back({"counter", {"fast", "rtl"}, true});
        lvd::tools::LibraryInfo other;
        other.name = "unisim";
        other.path = "libs/unisim";

        LibraryPane pane;
        pane.setLibraries({work, other});
        QCOMPARE(pane.libraryCount(), 2);
        QCOMPARE(pane.unitCount(), 2);
        QTreeWidgetItem* w = pane.tree()->topLevelItem(0);
        QCOMPARE(w->text(0), QString("work"));
        QCOMPARE(w->toolTip(0), QString("work"));
        QCOMPARE(w->childCount(), 2);
        QCOMPARE(w->child(1)->text(1), QString("elaborated"));
        QCOMPARE(w->child(1)->childCount(), 2);  // two architectures
        QCOMPARE(pane.tree()->topLevelItem(1)->childCount(), 0);

        pane.setLibraries({});
        QCOMPARE(pane.libraryCount(), 0);
    }

    void activatingItemsDoesNotSimulate() {
        lvd::tools::LibraryInfo work;
        work.name = "work";
        work.units.push_back({"counter", {"rtl"}, true});
        LibraryPane pane;
        pane.setLibraries({work});
        QSignalSpy spy(&pane, &LibraryPane::simulateRequested);

        QTreeWidgetItem* lib = pane.tree()->topLevelItem(0);
        emit pane.tree()->itemActivated(lib, 0);
        emit pane.tree()->itemActivated(lib->child(0), 0);
        emit pane.tree()->itemActivated(lib->child(0)->child(0), 0);
        emit pane.tree()->itemDoubleClicked(lib->child(0), 0);
        QCOMPARE(spy.count(), 0);
    }

    void contextMenuOffersSimulateOnUnitsOnly() {
        lvd::tools::LibraryInfo work;
        work.name = "work";
        work.units.push_back({"counter", {"rtl"}, true});
        LibraryPane pane;
        pane.setLibraries({work});
        QSignalSpy spy(&pane, &LibraryPane::simulateRequested);
        QTreeWidgetItem* lib = pane.tree()->topLevelItem(0);

        std::unique_ptr<QMenu> onLib(pane.createContextMenu(lib));
        QVERIFY(!onLib->findChild<QAction*>("simulate")->isEnabled());
        onLib->findChild<QAction*>("simulate")->trigger();
        QCOMPARE(spy.count(), 0);

        std::unique_ptr<QMenu> onUnit(pane.createContextMenu(lib->child(0)));
        onUnit->findChild<QAction*>("simulate")->trigger();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("work.counter"));

        std::unique_ptr<QMenu> onArch(pane.createContextMenu(lib->child(0)->child(0)));
        onArch->findChild<QAction*>("simulate")->trigger();
        QCOMPARE(spy.at(1).at(0).toString(), QString("work.counter"));

        QSignalSpy refresh(&pane, &LibraryPane::refreshRequested);
        onUnit->findChild<QAction*>("refresh")->trigger();
        QCOMPARE(refresh.count(), 1);
    }

    // ---- transcript ------------------------------------------------------------------------------
    void transcriptAppendsPartialLines() {
        TranscriptWidget t;
        t.append("line one\nsecond ");
        t.append("half\n% ");
        QCOMPARE(t.text(), QString("line one\nsecond half\n% "));
        t.appendNote("** note");
        QVERIFY(t.text().endsWith("\n** note\n"));  // a note always starts on a fresh line
        t.clear();
        QVERIFY(t.text().isEmpty());
    }

    void transcriptCommandsAndHistory() {
        TranscriptWidget t;
        QSignalSpy spy(&t, &TranscriptWidget::commandEntered);
        auto* edit = t.findChild<QLineEdit*>();
        QVERIFY(edit != nullptr);

        QTest::keyClicks(edit, "run 10 ns");
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::keyClicks(edit, "examine /cnt");
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QString("run 10 ns"));
        QCOMPARE(spy.at(1).at(0).toString(), QString("examine /cnt"));
        QVERIFY(edit->text().isEmpty());

        QTest::keyClick(edit, Qt::Key_Up);
        QCOMPARE(edit->text(), QString("examine /cnt"));
        QTest::keyClick(edit, Qt::Key_Up);
        QCOMPARE(edit->text(), QString("run 10 ns"));
        QTest::keyClick(edit, Qt::Key_Up);  // stays at the oldest
        QCOMPARE(edit->text(), QString("run 10 ns"));
        QTest::keyClick(edit, Qt::Key_Down);
        QCOMPARE(edit->text(), QString("examine /cnt"));
        QTest::keyClick(edit, Qt::Key_Down);
        QVERIFY(edit->text().isEmpty());

        QTest::keyClicks(edit, "half typed");  // the line being typed survives a trip through the history
        QTest::keyClick(edit, Qt::Key_Up);
        QTest::keyClick(edit, Qt::Key_Down);
        QCOMPARE(edit->text(), QString("half typed"));
    }

    void historyIgnoresRepeatsAndBlankLines() {
        TranscriptWidget t;
        auto* edit = t.findChild<QLineEdit*>();
        QSignalSpy spy(&t, &TranscriptWidget::commandEntered);
        for (const char* c : {"run", "run", "   ", "run"}) {
            QTest::keyClicks(edit, c);
            QTest::keyClick(edit, Qt::Key_Return);
        }
        QCOMPARE(t.history(), (QStringList{"run"}));
        QCOMPARE(spy.count(), 4);  // every Return is a command; an empty line brings the prompt back
    }

    void inputCanBeDisabled() {
        TranscriptWidget t;
        auto* edit = t.findChild<QLineEdit*>();
        t.setInputEnabled(false);
        QVERIFY(!edit->isEnabled());
        t.setInputEnabled(true);
        QVERIFY(edit->isEnabled());
    }
};

QTEST_MAIN(TestPanes)
#include "test_panes.moc"
