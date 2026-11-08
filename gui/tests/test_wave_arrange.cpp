// SPDX-License-Identifier: GPL-3.0-or-later
// Arranging wave rows: keyboard, rename editor, context menu, drag and drop, undo, caches, both backends.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/display_edit.hpp>

#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSignalSpy>
#include <QtTest>

#include <chrono>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(top, "cnt", SignalKind::Logic, 8);
    const SignalId lo = st->addSignal(top, "lo", SignalKind::Logic, 4);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 64);
    for (Time t = 0; t <= 1000; t += 50)
        st->mutableSignal(volt).appendReal(t, static_cast<double>(t) / 100.0);
    for (Time t = 0; t <= 1000; t += 50)
        st->mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
    st->mutableSignal(cnt).appendLogic(0, "00000000");
    st->mutableSignal(cnt).appendLogic(300, "10100101");
    st->mutableSignal(lo).appendLogic(0, "0001");
    st->extendTimeRange(0);
    st->extendTimeRange(1000);
    return st;
}

DisplayItem sig(const char* label, const char* path = "top.clk") {
    DisplayItem s = DisplayModel::makeSignal(path);
    s.label = label;
    return s;
}

// Clocks[ a b ]  c  --div--  d  e
DisplayModel makeModel() {
    DisplayModel m;
    DisplayItem clocks = DisplayModel::makeGroup("Clocks");
    clocks.children.push_back(sig("a"));
    clocks.children.push_back(sig("b", "top.cnt"));
    m.root().children.push_back(std::move(clocks));
    m.root().children.push_back(sig("c", "top.lo"));
    m.root().children.push_back(DisplayModel::makeDivider("div"));
    m.root().children.push_back(sig("d"));
    m.root().children.push_back(sig("e", "top.cnt"));
    return m;
}

} // namespace

class TestWaveArrange : public QObject {
    Q_OBJECT

    std::unique_ptr<WaveWidget> make(DisplayModel model = makeModel(), int height = 320) {
        auto w = std::make_unique<WaveWidget>();
        w->resize(900, height);
        w->setDisplayModel(std::move(model));
        w->setStore(makeStore());
        return w;
    }

    static QStringList names(const WaveWidget& w) {
        QStringList l;
        for (int i = 0; i < w.rowCount(); ++i)
            l << QString::fromStdString(w.rowName(i));
        return l;
    }

    static int rowNamed(const WaveWidget& w, const QString& name) {
        for (int i = 0; i < w.rowCount(); ++i)
            if (QString::fromStdString(w.rowName(i)) == name && w.rowBit(i) < 0)
                return i;
        return -1;
    }

    static void selectNames(WaveWidget& w, const QStringList& n) {
        bool first = true;
        for (const QString& s : n) {
            w.selectRow(rowNamed(w, s), !first);
            first = false;
        }
    }

    static QStringList selectedNames(const WaveWidget& w) {
        QStringList l;
        for (int i = 0; i < w.rowCount(); ++i)
            if (w.isRowSelected(i))
                l << QString::fromStdString(w.rowName(i));
        return l;
    }

    static QPoint nameAt(const WaveWidget& w, const QString& row, double frac = 0.5) {
        const int r = rowNamed(w, row);
        return QPoint(60, w.rowTop(r) + static_cast<int>(frac * w.rowHeight()));
    }

    static QAction* action(QMenu* menu, const char* name) { return menu->findChild<QAction*>(name); }

    // A real press, a move past the drag distance, a move to `to`, and optionally the release.
    static void dragTo(WaveWidget& w, const QPoint& from, const QPoint& to, bool release = true) {
        QTest::mousePress(&w, Qt::LeftButton, {}, from);
        QTest::mouseMove(&w, from + QPoint(0, 14));
        QTest::mouseMove(&w, (from + to) / 2);
        QTest::mouseMove(&w, to);
        if (release)
            QTest::mouseRelease(&w, Qt::LeftButton, {}, to);
    }

private slots:
    // ---- keyboard -------------------------------------------------------------------------------
    void ctrlUpAndDownMoveTheSelectedRow() {
        auto w = make();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "d", "e"}));
        QSignalSpy structure(w.get(), &WaveWidget::structureChanged);
        w->selectRow(rowNamed(*w, "c"));
        QTest::keyClick(w.get(), Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "div", "c", "d", "e"}));
        QCOMPARE(selectedNames(*w), QStringList{"c"}); // the moved row stays selected
        QTest::keyClick(w.get(), Qt::Key_Up, Qt::ControlModifier);
        QTest::keyClick(w.get(), Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(names(*w), (QStringList{"c", "Clocks", "a", "b", "div", "d", "e"}));
        QCOMPARE(structure.count(), 3);
        QTest::keyClick(w.get(), Qt::Key_Up, Qt::ControlModifier); // already on top: nothing happens
        QCOMPARE(structure.count(), 3);
        QVERIFY(w->lastEditError().isEmpty());
    }

    void ctrlShiftArrowsGoToTheEdges() {
        auto w = make();
        w->selectRow(rowNamed(*w, "d"));
        QTest::keyClick(w.get(), Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(names(*w), (QStringList{"d", "Clocks", "a", "b", "c", "div", "e"}));
        QTest::keyClick(w.get(), Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "e", "d"}));
    }

    void aGroupMovesWithItsRowsAndAChildStaysInItsGroup() {
        auto w = make();
        w->selectRow(rowNamed(*w, "Clocks"));
        QVERIFY(w->moveSelectedRows(1));
        QCOMPARE(names(*w), (QStringList{"c", "Clocks", "a", "b", "div", "d", "e"}));
        QVERIFY(w->moveSelectedRows(1));
        QCOMPARE(names(*w), (QStringList{"c", "div", "Clocks", "a", "b", "d", "e"}));
        w->selectRow(rowNamed(*w, "a"));
        QVERIFY(!w->moveSelectedRows(-1)); // first in its group
        QCOMPARE(names(*w), (QStringList{"c", "div", "Clocks", "a", "b", "d", "e"}));
        QVERIFY(w->moveSelectedRows(1));
        QCOMPARE(names(*w), (QStringList{"c", "div", "Clocks", "b", "a", "d", "e"}));
    }

    void movingSeveralRowsKeepsTheirOrderAndSelection() {
        auto w = make();
        selectNames(*w, {"e", "c"});
        QVERIFY(w->moveSelectedRows(-1));
        // c moved over the group, e moved over d: relative order c .. e kept
        QVERIFY(names(*w).indexOf("c") < names(*w).indexOf("e"));
        QCOMPARE(selectedNames(*w), (QStringList{"c", "e"}));
        QCOMPARE(names(*w), (QStringList{"c", "Clocks", "a", "b", "div", "e", "d"}));
    }

    void ctrlGAndCtrlShiftGGroupAndUngroup() {
        auto w = make();
        selectNames(*w, {"d", "e"});
        QTest::keyClick(w.get(), Qt::Key_G, Qt::ControlModifier);
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "Group", "d", "e"}));
        QCOMPARE(selectedNames(*w), QStringList{"Group"}); // the new group is selected, ready for F2

        selectNames(*w, {"c"});
        QTest::keyClick(w.get(), Qt::Key_G, Qt::ControlModifier);
        QVERIFY(names(*w).contains("Group 2")); // unique default names

        QTest::keyClick(w.get(), Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier); // ungroup "Group 2"
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "Group", "d", "e"}));
        QCOMPARE(selectedNames(*w), QStringList{"c"});
        w->selectRow(rowNamed(*w, "Group"));
        QTest::keyClick(w.get(), Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "d", "e"}));
        QCOMPARE(selectedNames(*w), (QStringList{"d", "e"}));
        w->selectRow(rowNamed(*w, "c"));
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        QVERIFY(!w->ungroupSelected()); // not a group
        QCOMPARE(rejected.count(), 1);
    }

    void groupingAcrossGroupsPutsTheGroupWhereTheFirstWas() {
        auto w = make();
        selectNames(*w, {"b", "e"});
        QVERIFY(w->groupSelected("Mix"));
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "Mix", "b", "e", "c", "div", "d"}));
    }

    void groupNameClashIsRefused() {
        auto w = make();
        w->selectRow(rowNamed(*w, "c"));
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        QVERIFY(!w->groupSelected("Clocks"));
        QCOMPARE(rejected.count(), 1);
        QVERIFY(w->lastEditError().contains("Clocks"));
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "d", "e"}));
        QVERIFY(!w->canUndo()); // a refused edit leaves no history
    }

    void plainArrowsWalkTheSelection() {
        auto w = make();
        QTest::keyClick(w.get(), Qt::Key_Down);
        QCOMPARE(selectedNames(*w), QStringList{"Clocks"});
        QTest::keyClick(w.get(), Qt::Key_Down);
        QCOMPARE(selectedNames(*w), QStringList{"a"});
        QTest::keyClick(w.get(), Qt::Key_Up);
        QTest::keyClick(w.get(), Qt::Key_Up);
        QCOMPARE(selectedNames(*w), QStringList{"Clocks"});
        QTest::keyClick(w.get(), Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(selectedNames(*w), (QStringList{"Clocks", "a"}));
    }

    // ---- rename editor --------------------------------------------------------------------------
    void f2RenamesAGroupInPlace() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->selectRow(rowNamed(*w, "Clocks"));
        QTest::keyClick(w.get(), Qt::Key_F2);
        QVERIFY(w->renaming());
        QLineEdit* ed = w->renameEditor();
        QVERIFY(ed->isVisible());
        QCOMPARE(ed->text(), QString("Clocks"));
        // over the name of the row, inside the name column
        QCOMPARE(ed->geometry().top(), w->rowTop(0));
        QVERIFY(ed->geometry().left() >= 6);
        QVERIFY(ed->geometry().right() < 220); // inside the name column
        QVERIFY(ed->height() <= w->rowHeight());
        ed->setText("Clock tree");
        QTest::keyClick(ed, Qt::Key_Return);
        QVERIFY(!w->renaming());
        QCOMPARE(QString::fromStdString(w->rowName(0)), QString("Clock tree"));
        QCOMPARE(selectedNames(*w), QStringList{"Clock tree"});
        QVERIFY(w->canUndo());
        QCOMPARE(w->undoText(), QString("Rename"));
    }

    void escapeCancelsTheEditor() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->selectRow(rowNamed(*w, "Clocks"));
        QVERIFY(w->beginRename());
        w->renameEditor()->setText("Nope");
        QTest::keyClick(w->renameEditor(), Qt::Key_Escape);
        QVERIFY(!w->renaming());
        QCOMPARE(QString::fromStdString(w->rowName(0)), QString("Clocks"));
        QVERIFY(!w->canUndo());
    }

    void anEmptyGroupNameIsRejectedAndTheEditorStaysOpen() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->selectRow(rowNamed(*w, "Clocks"));
        QVERIFY(w->beginRename());
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        w->renameEditor()->setText("   ");
        QTest::keyClick(w->renameEditor(), Qt::Key_Return);
        QVERIFY(w->renaming());
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!w->renameEditor()->styleSheet().isEmpty()); // marked
        QCOMPARE(QString::fromStdString(w->rowName(0)), QString("Clocks"));
        QTest::keyClick(w->renameEditor(), Qt::Key_X); // typing clears the mark
        QVERIFY(w->renameEditor()->styleSheet().isEmpty());
        QTest::keyClick(w->renameEditor(), Qt::Key_Return); // "   x" trims to a valid name
        QVERIFY(!w->renaming());
        QCOMPARE(QString::fromStdString(w->rowName(0)), QString("x"));
    }

    void aSiblingGroupNameAndASlashAreRejected() {
        auto w = make();
        w->selectRow(rowNamed(*w, "d"));
        QVERIFY(w->groupSelected("Other"));
        QVERIFY(!w->renameItem(w->itemIdAtRow(rowNamed(*w, "Other")), "Clocks"));
        QVERIFY(!w->renameItem(w->itemIdAtRow(rowNamed(*w, "Other")), "a/b"));
        QVERIFY(w->renameItem(w->itemIdAtRow(rowNamed(*w, "Other")), "Other 2"));
    }

    void aDividerMayBeEmptyAndASignalLabelCanBeReset() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->selectRow(rowNamed(*w, "div"));
        QTest::keyClick(w.get(), Qt::Key_F2);
        QVERIFY(w->renaming());
        w->renameEditor()->setText("");
        QTest::keyClick(w->renameEditor(), Qt::Key_Return);
        QVERIFY(!w->renaming());
        QCOMPARE(QString::fromStdString(w->rowName(4)), QString(""));

        w->selectRow(rowNamed(*w, "c"));
        QTest::keyClick(w.get(), Qt::Key_F2);
        QCOMPARE(w->renameEditor()->text(), QString("c"));
        w->renameEditor()->setText("Counter");
        QTest::keyClick(w->renameEditor(), Qt::Key_Return);
        QCOMPARE(QString::fromStdString(w->rowName(3)), QString("Counter"));
        QTest::keyClick(w.get(), Qt::Key_F2);
        w->renameEditor()->setText(""); // an empty label shows the path again
        QTest::keyClick(w->renameEditor(), Qt::Key_Return);
        QCOMPARE(QString::fromStdString(w->rowName(3)), QString("top.lo"));
    }

    void f2NeedsASingleRowAndNotABitRow() {
        auto w = make();
        QVERIFY(!w->beginRename()); // nothing selected
        selectNames(*w, {"c", "d"});
        // two rows selected: the anchor row (the last clicked) is renamed
        QVERIFY(w->beginRename());
        w->cancelRename();
        DisplayModel m = makeModel();
        m.root().children[1].showBits = true; // c has 4 bits
        auto v = make(std::move(m));
        const int bitRow = rowNamed(*v, "c") + 1;
        QVERIFY(v->rowBit(bitRow) >= 0);
        QVERIFY(!v->beginRename(bitRow));
    }

    void theEditorFollowsScrollingAndHidesWhenOutOfView() {
        DisplayModel m;
        for (int i = 0; i < 40; ++i)
            m.root().children.push_back(sig(qPrintable(QString("row%1").arg(i))));
        auto w = make(std::move(m));
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QVERIFY(w->beginRename(3));
        QLineEdit* ed = w->renameEditor();
        QCOMPARE(ed->geometry().top(), w->rowTop(3));
        QScrollBar* bar = w->findChild<QScrollBar*>();
        bar->setValue(2 * w->rowHeight());
        QCOMPARE(ed->geometry().top(), w->rowTop(3));
        QVERIFY(ed->isVisible());
        bar->setValue(10 * w->rowHeight()); // row 3 is above the view
        QVERIFY(ed->isHidden());
        QVERIFY(w->renaming());
        bar->setValue(0);
        QVERIFY(ed->isVisible());
        QCOMPARE(ed->geometry().top(), w->rowTop(3));
        w->cancelRename();
    }

    void doubleClickingADividerRenamesIt() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::mouseDClick(w.get(), Qt::LeftButton, {}, nameAt(*w, "div"));
        QVERIFY(w->renaming());
        QCOMPARE(w->renameEditor()->text(), QString("div"));
        w->cancelRename();
    }

    // ---- context menu ---------------------------------------------------------------------------
    void theContextMenuMovesRows() {
        auto w = make();
        std::unique_ptr<QMenu> menu(w->createContextMenu(rowNamed(*w, "d")));
        QVERIFY(menu);
        QVERIFY(w->isRowSelected(rowNamed(*w, "d"))); // a click on an unselected row selects it
        for (const char* n :
             {"moveUp", "moveDown", "moveTop", "moveBottom", "groupSelected", "ungroup", "rename",
              "insertDividerAbove", "insertDividerBelow", "delete", "undo", "redo", "moveToGroupMenu"})
            QVERIFY2(action(menu.get(), n) != nullptr, n);
        QVERIFY(action(menu.get(), "moveUp")->isEnabled());
        QVERIFY(!action(menu.get(), "ungroup")->isEnabled()); // d is not a group
        QVERIFY(!action(menu.get(), "undo")->isEnabled());
        action(menu.get(), "moveUp")->trigger();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "d", "div", "e"}));
        action(menu.get(), "moveBottom")->trigger();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "e", "d"}));
        action(menu.get(), "moveTop")->trigger();
        QCOMPARE(names(*w), (QStringList{"d", "Clocks", "a", "b", "c", "div", "e"}));
        action(menu.get(), "moveDown")->trigger();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "d", "c", "div", "e"}));
    }

    void theMoveToGroupSubmenuListsGroupsAndTopLevel() {
        auto w = make();
        QVERIFY(w->groupSelected("Other") == false); // nothing selected yet
        w->selectRow(rowNamed(*w, "e"));
        QVERIFY(w->groupSelected("Other"));
        const ItemId clocks = w->itemIdAtRow(rowNamed(*w, "Clocks"));
        const ItemId other = w->itemIdAtRow(rowNamed(*w, "Other"));
        w->selectRow(rowNamed(*w, "c"));
        std::unique_ptr<QMenu> menu(w->createContextMenu(rowNamed(*w, "c")));
        QAction* sub = action(menu.get(), "moveToGroupMenu");
        QVERIFY(sub && sub->menu());
        QVERIFY(action(sub->menu(), "moveToTopLevel"));
        QVERIFY(action(sub->menu(), "moveToNewGroup"));
        QVERIFY(action(sub->menu(), QString("moveToGroup_%1").arg(clocks).toLatin1().constData()));
        QVERIFY(action(sub->menu(), QString("moveToGroup_%1").arg(other).toLatin1().constData()));

        action(sub->menu(), QString("moveToGroup_%1").arg(other).toLatin1().constData())->trigger();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "div", "d", "Other", "e", "c"}));
        QCOMPARE(selectedNames(*w), QStringList{"c"});

        menu.reset(w->createContextMenu(rowNamed(*w, "c")));
        sub = action(menu.get(), "moveToGroupMenu");
        action(sub->menu(), "moveToTopLevel")->trigger();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "div", "d", "Other", "e", "c"}));
        const DisplayModel& dm = w->displayModel();
        QCOMPARE(dm.root().children.back().label, std::string("c")); // at the end of the top level
    }

    void moveToNewGroupAsksForTheName() {
        auto w = make();
        QString seen;
        w->setTextPrompt(
            [&](const QString&, const QString&, const QString& initial) -> std::optional<QString> {
                seen = initial;
                return QString("Fresh");
            });
        w->selectRow(rowNamed(*w, "d"));
        std::unique_ptr<QMenu> menu(w->createContextMenu(rowNamed(*w, "d")));
        action(action(menu.get(), "moveToGroupMenu")->menu(), "moveToNewGroup")->trigger();
        QCOMPARE(seen, QString("Group"));
        QVERIFY(rowNamed(*w, "Fresh") >= 0);
        w->setTextPrompt([](const QString&, const QString&, const QString&) -> std::optional<QString> {
            return std::nullopt;
        });
        w->selectRow(rowNamed(*w, "e"));
        menu.reset(w->createContextMenu(rowNamed(*w, "e")));
        action(action(menu.get(), "moveToGroupMenu")->menu(), "moveToNewGroup")->trigger(); // cancelled
        QCOMPARE(w->displayModel().root().children.size(), std::size_t(5));
    }

    void invalidGroupTargetsAreDisabled() {
        auto w = make();
        w->selectRow(rowNamed(*w, "Clocks"));
        std::unique_ptr<QMenu> menu(w->createContextMenu(rowNamed(*w, "Clocks")));
        const ItemId clocks = w->itemIdAtRow(0);
        QAction* self = action(action(menu.get(), "moveToGroupMenu")->menu(),
                               QString("moveToGroup_%1").arg(clocks).toLatin1().constData());
        QVERIFY(self);
        QVERIFY(!self->isEnabled()); // a group cannot go into itself
        QVERIFY(!self->toolTip().isEmpty());
    }

    void groupUngroupRenameDividerDeleteFromTheMenu() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        selectNames(*w, {"d", "e"});
        std::unique_ptr<QMenu> menu(w->createContextMenu(rowNamed(*w, "d")));
        action(menu.get(), "groupSelected")->trigger();
        QVERIFY(rowNamed(*w, "Group") >= 0);

        menu.reset(w->createContextMenu(rowNamed(*w, "Group")));
        QVERIFY(action(menu.get(), "ungroup")->isEnabled());
        action(menu.get(), "rename")->trigger();
        QVERIFY(w->renaming());
        w->renameEditor()->setText("Tail");
        QTest::keyClick(w->renameEditor(), Qt::Key_Return);
        QVERIFY(rowNamed(*w, "Tail") >= 0);

        menu.reset(w->createContextMenu(rowNamed(*w, "Tail")));
        action(menu.get(), "insertDividerBelow")->trigger();
        QVERIFY(w->renaming()); // the new divider is named right away
        w->renameEditor()->setText("end");
        QTest::keyClick(w->renameEditor(), Qt::Key_Return);
        QCOMPARE(names(*w).last(), QString("end"));

        menu.reset(w->createContextMenu(rowNamed(*w, "Tail")));
        action(menu.get(), "insertDividerAbove")->trigger();
        QVERIFY(w->renaming());
        w->cancelRename();
        QCOMPARE(names(*w).mid(names(*w).indexOf("Tail") - 1, 1), QStringList{""});

        menu.reset(w->createContextMenu(rowNamed(*w, "Tail")));
        action(menu.get(), "delete")->trigger(); // a group takes its rows with it
        QVERIFY(!names(*w).contains("d"));
        QVERIFY(!names(*w).contains("Tail"));

        menu.reset(w->createContextMenu(rowNamed(*w, "c")));
        QVERIFY(action(menu.get(), "undo")->isEnabled());
        QVERIFY(action(menu.get(), "undo")->text().startsWith("Undo Delete"));
        action(menu.get(), "undo")->trigger();
        QVERIFY(names(*w).contains("Tail"));
        QVERIFY(names(*w).contains("d"));
        menu.reset(w->createContextMenu(rowNamed(*w, "c")));
        QVERIFY(action(menu.get(), "redo")->isEnabled());
        action(menu.get(), "redo")->trigger();
        QVERIFY(!names(*w).contains("Tail"));
    }

    void theArrangeMenuForTheMenuBarUsesTheSelection() {
        auto w = make();
        QMenu menu;
        w->populateArrangeMenu(&menu);
        QVERIFY(!action(&menu, "moveUp")->isEnabled()); // nothing selected
        QVERIFY(!action(&menu, "groupSelected")->isEnabled());
        QMenu menu2;
        w->selectRow(rowNamed(*w, "Clocks"));
        w->populateArrangeMenu(&menu2);
        QVERIFY(action(&menu2, "moveUp")->isEnabled());
        QVERIFY(action(&menu2, "ungroup")->isEnabled());
        QVERIFY(action(&menu2, "rename")->isEnabled());
        QVERIFY(!action(&menu2, "moveUp")->toolTip().isEmpty());
    }

    // ---- drag and drop --------------------------------------------------------------------------
    void draggingARowShowsALineAndDropsBetweenRows() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        const QPoint from = nameAt(*w, "e");
        QTest::mousePress(w.get(), Qt::LeftButton, {}, from);
        QVERIFY(!w->rowDragActive()); // a press alone starts nothing
        QTest::mouseMove(w.get(), from + QPoint(0, 3));
        QVERIFY(!w->rowDragActive()); // below the drag distance
        QTest::mouseMove(w.get(), nameAt(*w, "c", 0.1));
        QVERIFY(w->rowDragActive());
        const auto t = w->rowDropIndicator();
        QVERIFY(t.valid);
        QVERIFY(!t.intoGroup);
        QCOMPARE(t.parent, ItemId(0));
        QCOMPARE(t.index, std::size_t(1)); // before c: after the group
        QCOMPARE(t.lineY, w->rowTop(rowNamed(*w, "c")));
        QCOMPARE(w->cursor().shape(), Qt::DragMoveCursor);
        QVERIFY(!w->grab().isNull()); // painting the indicator works
        QTest::mouseRelease(w.get(), Qt::LeftButton, {}, nameAt(*w, "c", 0.1));
        QVERIFY(!w->rowDragActive());
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "e", "c", "div", "d"}));
        QCOMPARE(selectedNames(*w), QStringList{"e"});
        QCOMPARE(w->cursor().shape(), Qt::ArrowCursor);
    }

    void aDropOnTheLowerHalfGoesBelowTheRow() {
        auto w = make();
        dragTo(*w, nameAt(*w, "c"), nameAt(*w, "e", 0.9));
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "div", "d", "e", "c"}));
    }

    void droppingOnAGroupRowPutsTheRowsIntoTheGroup() {
        auto w = make();
        const QPoint to = nameAt(*w, "Clocks", 0.5);
        QTest::mousePress(w.get(), Qt::LeftButton, {}, nameAt(*w, "d"));
        QTest::mouseMove(w.get(), nameAt(*w, "d") + QPoint(0, -20));
        QTest::mouseMove(w.get(), to);
        QVERIFY(w->rowDropIndicator().intoGroup);
        QCOMPARE(w->rowDropIndicator().parent, w->itemIdAtRow(0));
        QVERIFY(!w->grab().isNull());
        QTest::mouseRelease(w.get(), Qt::LeftButton, {}, to);
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "d", "c", "div", "e"}));
        QCOMPARE(selectedNames(*w), QStringList{"d"});
    }

    void droppingIntoACollapsedGroupOpensIt() {
        auto w = make();
        w->toggleGroup(0);
        QCOMPARE(names(*w), (QStringList{"Clocks", "c", "div", "d", "e"}));
        dragTo(*w, nameAt(*w, "e"), nameAt(*w, "Clocks", 0.5));
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "e", "c", "div", "d"}));
        QCOMPARE(selectedNames(*w), QStringList{"e"});
    }

    void rowsDraggedOutOfAGroupToTheTopLevel() {
        auto w = make();
        dragTo(*w, nameAt(*w, "a"), nameAt(*w, "div", 0.1));
        QCOMPARE(names(*w), (QStringList{"Clocks", "b", "c", "a", "div", "d", "e"}));
        // below the last row: the end of the top level
        dragTo(*w, nameAt(*w, "b"), QPoint(60, w->rowTop(w->rowCount() - 1) + 2 * w->rowHeight()));
        QCOMPARE(names(*w), (QStringList{"Clocks", "c", "a", "div", "d", "e", "b"}));
    }

    void aWholeGroupAndADividerCanBeDragged() {
        auto w = make();
        dragTo(*w, nameAt(*w, "Clocks"), nameAt(*w, "d", 0.9));
        QCOMPARE(names(*w), (QStringList{"c", "div", "d", "Clocks", "a", "b", "e"}));
        QCOMPARE(selectedNames(*w), QStringList{"Clocks"});
        dragTo(*w, nameAt(*w, "div"), nameAt(*w, "c", 0.05));
        QCOMPARE(names(*w), (QStringList{"div", "c", "d", "Clocks", "a", "b", "e"}));
    }

    void draggingSeveralSelectedRowsMovesThemAll() {
        auto w = make();
        selectNames(*w, {"c", "e"});
        dragTo(*w, nameAt(*w, "c"), nameAt(*w, "a", 0.1));
        // c and e go into the group before a, in their display order
        QCOMPARE(names(*w), (QStringList{"Clocks", "c", "e", "a", "b", "div", "d"}));
        QCOMPARE(selectedNames(*w), (QStringList{"c", "e"}));
    }

    void aClickOnOneOfSeveralSelectedRowsSelectsOnlyIt() {
        auto w = make();
        selectNames(*w, {"c", "d"});
        QTest::mouseClick(w.get(), Qt::LeftButton, {}, nameAt(*w, "c"));
        QCOMPARE(selectedNames(*w), QStringList{"c"});
        QTest::mouseClick(w.get(), Qt::LeftButton, {}, nameAt(*w, "e"));
        QCOMPARE(selectedNames(*w), QStringList{"e"});
    }

    void droppingAGroupOntoItselfOrInsideItselfIsRefused() {
        auto w = make();
        const QStringList before = names(*w);
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        const QPoint from = nameAt(*w, "Clocks");
        QTest::mousePress(w.get(), Qt::LeftButton, {}, from);
        QTest::mouseMove(w.get(), from + QPoint(0, 30));
        QTest::mouseMove(w.get(), nameAt(*w, "Clocks", 0.5)); // onto itself
        QVERIFY(w->rowDragActive());
        QVERIFY(!w->rowDropIndicator().valid);
        QCOMPARE(w->cursor().shape(), Qt::ForbiddenCursor);
        QTest::mouseMove(w.get(), nameAt(*w, "b", 0.9)); // inside itself (below its child)
        QVERIFY(!w->rowDropIndicator().valid);
        QVERIFY(!w->rowDropIndicator().reason.isEmpty());
        QCOMPARE(w->cursor().shape(), Qt::ForbiddenCursor);
        QTest::mouseRelease(w.get(), Qt::LeftButton, {}, nameAt(*w, "b", 0.9));
        QCOMPARE(names(*w), before);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!w->canUndo());
        QCOMPARE(w->cursor().shape(), Qt::ArrowCursor);
    }

    void aDropOutsideTheNameColumnIsIgnored() {
        auto w = make();
        const QStringList before = names(*w);
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        dragTo(*w, nameAt(*w, "d"), QPoint(w->waveLeft() + 100, w->rowTop(1))); // over the waveforms
        QCOMPARE(names(*w), before);
        QCOMPARE(rejected.count(), 0);              // ignored silently
        dragTo(*w, nameAt(*w, "d"), QPoint(60, 5)); // over the ruler
        QCOMPARE(names(*w), before);
        dragTo(*w, nameAt(*w, "d"), QPoint(-30, w->rowTop(1))); // left of the widget
        QCOMPARE(names(*w), before);
        QVERIFY(!w->canUndo());
        QVERIFY(!w->rowDragActive());
    }

    void escapeCancelsADrag() {
        auto w = make();
        const QStringList before = names(*w);
        dragTo(*w, nameAt(*w, "e"), nameAt(*w, "c", 0.1), false);
        QVERIFY(w->rowDragActive());
        QTest::keyClick(w.get(), Qt::Key_Escape);
        QVERIFY(!w->rowDragActive());
        QTest::mouseRelease(w.get(), Qt::LeftButton, {}, nameAt(*w, "c", 0.1));
        QCOMPARE(names(*w), before);
        QCOMPARE(w->cursor().shape(), Qt::ArrowCursor);
    }

    void aGroupMovesToAnotherGroupAndAClashIsRefused() {
        DisplayModel m = makeModel();
        DisplayItem x1 = DisplayModel::makeGroup("X");
        x1.children.push_back(sig("p"));
        m.root().children[0].children.push_back(std::move(x1)); // Clocks/X
        DisplayItem other = DisplayModel::makeGroup("Other");
        other.children.push_back(DisplayModel::makeGroup("X"));
        m.root().children.push_back(std::move(other)); // Other/X
        auto w = make(std::move(m));
        // Clocks/X cannot go into Other (which has an X), Other/X cannot go into Clocks
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        const QStringList before = names(*w);
        const int x = w->rowCount() - 1; // Other/X
        QVERIFY(w->rowName(x) == "X");
        dragTo(*w, QPoint(60, w->rowTop(x) + 5), nameAt(*w, "Clocks", 0.5));
        QCOMPARE(names(*w), before);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(w->lastEditError().contains("X"));
    }

    void dragAutoScrollsNearTheEdges() {
        DisplayModel m;
        for (int i = 0; i < 60; ++i)
            m.root().children.push_back(sig(qPrintable(QString("row%1").arg(i))));
        auto w = make(std::move(m));
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QScrollBar* bar = w->findChild<QScrollBar*>();
        QVERIFY(bar->maximum() > 0);
        const QPoint from = nameAt(*w, "row3");
        QCOMPARE(w->autoScrollStep(), 0); // no drag, no scrolling
        QTest::mousePress(w.get(), Qt::LeftButton, {}, from);
        QTest::mouseMove(w.get(), from + QPoint(0, 12));
        QTest::mouseMove(w.get(), QPoint(60, w->height() - 4)); // near the bottom edge
        QVERIFY(w->rowDragActive());
        const int step = w->autoScrollStep();
        QVERIFY(step > 0);
        QCOMPARE(bar->value(), step);
        w->autoScrollStep();
        QVERIFY(bar->value() > step);
        // the drop target follows the content that scrolled under the pointer
        w->updateRowDrag(QPoint(60, w->height() - 4));
        QVERIFY(w->rowDropIndicator().row > 3);
        // timer driven scrolling as well
        const int before = bar->value();
        QTRY_VERIFY_WITH_TIMEOUT(bar->value() > before, 1000);
        // near the top
        QTest::mouseMove(w.get(), QPoint(60, w->rowsTop() + 3));
        const int upFrom = bar->value();
        QVERIFY(w->autoScrollStep() < 0);
        QVERIFY(bar->value() < upFrom);
        // the middle of the view: no scrolling
        QTest::mouseMove(w.get(), QPoint(60, w->height() / 2));
        QCOMPARE(w->autoScrollStep(), 0);
        QTest::keyClick(w.get(), Qt::Key_Escape);
        QVERIFY(!w->rowDragActive());
    }

    void aDragMovedByTheTestSeamOnlyDropsOnFinish() {
        auto w = make();
        w->selectRow(rowNamed(*w, "e"));
        QVERIFY(w->startRowDrag());
        w->updateRowDrag(nameAt(*w, "a", 0.1));
        QVERIFY(w->rowDropIndicator().valid);
        QCOMPARE(w->rowDropIndicator().parent, w->itemIdAtRow(0));
        QCOMPARE(w->rowDropIndicator().index, std::size_t(0));
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "d", "e"}));
        QVERIFY(w->finishRowDrag(nameAt(*w, "a", 0.1)));
        QCOMPARE(names(*w), (QStringList{"Clocks", "e", "a", "b", "c", "div", "d"}));
        QVERIFY(w->startRowDrag()); // still has a selection: a drag can start again
        w->cancelRowDrag();
        w->clearSelection();
        QVERIFY(!w->startRowDrag());
    }

    // ---- selection, scrolling, cursors ------------------------------------------------------------
    void movedRowsStayVisibleWhenTheyLeaveTheView() {
        DisplayModel m;
        for (int i = 0; i < 60; ++i)
            m.root().children.push_back(sig(qPrintable(QString("row%1").arg(i))));
        auto w = make(std::move(m));
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QScrollBar* bar = w->findChild<QScrollBar*>();
        const int visibleRows = (w->height() - w->rowsTop()) / w->rowHeight();
        w->selectRow(2);
        w->moveSelectedRowsToEdge(false); // to the bottom of 60 rows
        QVERIFY(bar->value() > 0);
        const int row = w->rowOfItem(w->selectedItemIds().front());
        QCOMPARE(row, 59);
        QVERIFY(w->rowTop(row) >= w->rowsTop());
        QVERIFY(w->rowTop(row) + w->rowHeight() <= w->height());
        w->moveSelectedRowsToEdge(true);
        QCOMPARE(bar->value(), 0);
        QVERIFY(visibleRows < 59);
        // one step at a time while scrolled
        bar->setValue(bar->maximum());
        w->selectRow(40);
        for (int i = 0; i < 25; ++i)
            QVERIFY(w->moveSelectedRows(-1));
        const int r2 = w->rowOfItem(w->selectedItemIds().front());
        QCOMPARE(r2, 15);
        QVERIFY(w->rowTop(r2) >= w->rowsTop());
        QVERIFY(w->rowTop(r2) + w->rowHeight() <= w->height());
    }

    void valuesAndCursorsFollowTheMovedRow() {
        auto w = make();
        w->setCursorTime(0, 400.0);
        const int c = rowNamed(*w, "c");
        const QString value = QString::fromStdString(w->rowValueText(c));
        QVERIFY(!value.isEmpty());
        w->selectRow(c);
        w->moveSelectedRows(1);
        const int c2 = rowNamed(*w, "c");
        QVERIFY(c2 != c);
        QCOMPARE(QString::fromStdString(w->rowValueText(c2)), value);
        QCOMPARE(w->cursorCount(), 1);
        QCOMPARE(*w->cursorTime(0), 400.0);
        QVERIFY(!w->rowValueText(rowNamed(*w, "a")).empty());
    }

    void selectionSurvivesGroupingMovingAndUndo() {
        auto w = make();
        selectNames(*w, {"c", "d"});
        QVERIFY(w->moveSelectedRows(1));
        QCOMPARE(selectedNames(*w), (QStringList{"c", "d"}));
        QVERIFY(w->undo());
        QCOMPARE(selectedNames(*w), (QStringList{"c", "d"}));
        QVERIFY(w->redo());
        QCOMPARE(selectedNames(*w), (QStringList{"c", "d"}));
    }

    void bitRowsAreNotIndependentItems() {
        auto w = make();
        const int cnt = rowNamed(*w, "b");
        w->toggleBits(cnt); // b = top.cnt, 8 bits
        QVERIFY(w->rowCount() > 7 + 7);
        const int bit = cnt + 1;
        QVERIFY(w->rowBit(bit) >= 0);
        w->selectRow(bit);
        QVERIFY(w->selectedItemIds().empty()); // a bit row selects no item
        QVERIFY(!w->moveSelectedRows(-1));
        QVERIFY(!w->groupSelected());
        QVERIFY(!w->canUndo());
        // moving the vector moves its bit rows with it
        w->selectRow(cnt);
        QVERIFY(w->moveSelectedRows(-1)); // b was last in Clocks
        const int moved = rowNamed(*w, "b");
        QCOMPARE(moved, cnt - 1);
        QVERIFY(w->rowBit(moved + 1) >= 0); // its bit rows came along
        QCOMPARE(w->rowBit(moved + 1), 7);
    }

    // ---- caches: identity based keys ------------------------------------------------------------
    void aMovedRowKeepsItsCacheKeyAndItsTiles() {
        auto w = make();
        w->setView(0.0, 1.0);
        w->grab(); // fill the tile cache
        const QSize size(w->waveWidth(), w->height() - w->rowsTop());
        const GpuFrame before = w->buildGpuFrame(size);

        // key of every row by y
        auto keyAtY = [](const GpuFrame& f) {
            QMap<int, std::pair<const void*, std::uint64_t>> m;
            for (const GpuTileDraw& t : f.tiles)
                m[static_cast<int>(t.y)] = {t.key.row, t.key.revision};
            return m;
        };
        const auto k0 = keyAtY(before);

        w->selectRow(rowNamed(*w, "c"));
        const std::size_t misses = w->tileCache().misses();
        QVERIFY(w->moveSelectedRows(1)); // c below div (a divider has no tiles)
        w->grab();
        QCOMPARE(w->tileCache().misses(), misses); // nothing was rendered again
        const GpuFrame after = w->buildGpuFrame(size);
        const auto k1 = keyAtY(after);

        // the same set of keys, in the new order
        QSet<const void*> a, b;
        for (const auto& v : k0)
            a.insert(v.first);
        for (const auto& v : k1)
            b.insert(v.first);
        QCOMPARE(a, b);
        QVERIFY(k0 != k1);                    // c really moved...
        QCOMPARE(k1.value(88), k0.value(66)); // ...and kept its key and revision: one row further down

        // the moved row's key is the same, it has just moved by one row
        w->moveSelectedRows(-1);
        w->grab();
        QCOMPARE(w->tileCache().misses(), misses);
        QCOMPARE(keyAtY(w->buildGpuFrame(size)), k0);
    }

    void movingRowsKeepsTheCacheKeyOfEveryRow() {
        auto w = make();
        w->setView(0.0, 1.0);
        const QSize size(w->waveWidth(), w->height() - w->rowsTop());
        // identity of a row: label -> key.row, taken from the frame via the row order
        auto keysByLabel = [&](WaveWidget& v) {
            QMap<QString, const void*> m;
            const GpuFrame f = v.buildGpuFrame(size);
            QMap<int, const void*> byY;
            for (const GpuTileDraw& t : f.tiles)
                byY[static_cast<int>(t.y)] = t.key.row;
            for (int i = 0; i < v.rowCount(); ++i) {
                const int y = v.rowTop(i) - v.rowsTop();
                if (byY.contains(y))
                    m[QString::fromStdString(v.rowName(i))] = byY[y];
            }
            return m;
        };
        const auto before = keysByLabel(*w);
        QCOMPARE(before.size(), 5); // a b c d e have signals, the group and the divider have none
        selectNames(*w, {"b", "e"});
        QVERIFY(w->moveSelectedRows(-1));
        QVERIFY(w->groupSelected("G"));
        QVERIFY(w->ungroupSelected());
        QVERIFY(w->moveSelectedRowsToEdge(false));
        QCOMPARE(keysByLabel(*w), before);
        w->undo();
        w->undo();
        QCOMPARE(keysByLabel(*w), before);
        // keys of different rows differ, also for two rows showing the same signal
        QSet<const void*> distinct;
        for (const void* k : before)
            distinct.insert(k);
        QCOMPARE(distinct.size(), 5);
    }

    void theGpuFrameFollowsTheNewOrder() {
        // rows with different heights: the frame must place each row's tiles at its new y
        DisplayModel m;
        m.root().children.push_back(sig("a"));
        m.root().children.push_back(sig("b", "top.cnt"));
        DisplayItem an = sig("c", "top.volt");
        an.analog.enabled = true;
        an.analog.heightRows = 3;
        m.root().children.push_back(std::move(an));
        auto w = make(std::move(m), 400);
        w->setView(0.0, 1.0);
        const QSize size(w->waveWidth(), w->height() - w->rowsTop());
        auto layout = [&] {
            QMap<int, int> heightAtY;
            const GpuFrame f = w->buildGpuFrame(size);
            for (const GpuTileDraw& t : f.tiles)
                heightAtY[static_cast<int>(t.y)] = t.heightPx;
            return heightAtY;
        };
        const auto before = layout();
        QCOMPARE(before.keys(), (QList<int>{0, 22, 44}));
        QCOMPARE(before.value(44), 66);
        w->selectRow(2);
        QVERIFY(w->moveSelectedRowsToEdge(true));
        const auto after = layout();
        QCOMPARE(after.keys(), (QList<int>{0, 66, 88}));
        QCOMPARE(after.value(0), 66);
        QCOMPARE(after.value(66), 22);
    }

    void bothBackendsAcceptTheEdits() {
        for (auto backend : {WaveWidget::Backend::Cpu, WaveWidget::Backend::Gpu}) {
            auto w = make();
            w->setRenderBackend(backend);
            w->show();
            QVERIFY(QTest::qWaitForWindowExposed(w.get()));
            QTest::qWait(50); // offscreen: the GPU request falls back to QPainter
            w->selectRow(rowNamed(*w, "d"));
            QVERIFY(w->moveSelectedRows(-2));
            QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "d", "c", "div", "e"}));
            QVERIFY(!w->snapshot().isNull());
            const QSize size(w->waveWidth(), w->height() - w->rowsTop());
            const GpuFrame f = w->buildGpuFrame(size);
            QVERIFY(!f.tiles.empty());
        }
    }

    // ---- undo and redo --------------------------------------------------------------------------
    void undoAndRedoRevertTheEdits() {
        auto w = make();
        const QStringList start = names(*w);
        QSignalSpy state(w.get(), &WaveWidget::undoStateChanged);
        QVERIFY(!w->canUndo());
        QVERIFY(!w->undo());
        QVERIFY(!w->redo());
        w->selectRow(rowNamed(*w, "e"));
        QVERIFY(w->moveSelectedRows(-1));
        QVERIFY(w->groupSelected("G"));
        QVERIFY(w->renameItem(w->itemIdAtRow(rowNamed(*w, "G")), "H"));
        const QStringList end = names(*w);
        QCOMPARE(w->undoText(), QString("Rename"));
        QTest::keyClick(w.get(), Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(rowNamed(*w, "G") >= 0);
        QCOMPARE(w->redoText(), QString("Rename"));
        QCOMPARE(w->undoText(), QString("Group"));
        QTest::keyClick(w.get(), Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(w.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(names(*w), start);
        QVERIFY(!w->canUndo());
        QTest::keyClick(w.get(), Qt::Key_Y, Qt::ControlModifier);
        QTest::keyClick(w.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(w.get(), Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(names(*w), end);
        QVERIFY(!w->canRedo());
        QVERIFY(state.count() >= 6);
    }

    void aNewEditAfterUndoDropsTheRedoHistory() {
        auto w = make();
        w->selectRow(rowNamed(*w, "e"));
        QVERIFY(w->moveSelectedRows(-1));
        QVERIFY(w->undo());
        QVERIFY(w->canRedo());
        QVERIFY(w->moveSelectedRows(-2));
        QVERIFY(!w->canRedo());
    }

    void undoRestoresRowsKeepingIdsAndViewState() {
        auto w = make();
        const ItemId d = w->itemIdAtRow(rowNamed(*w, "d"));
        const ItemId clocks = w->itemIdAtRow(0);
        w->selectRow(rowNamed(*w, "d"));
        QVERIFY(w->moveSelectedRows(-1));
        w->toggleGroup(0); // collapse Clocks after the move: view state, not part of the history
        QVERIFY(w->undo());
        QCOMPARE(w->itemIdAtRow(rowNamed(*w, "d")), d);
        QCOMPARE(w->itemIdAtRow(rowNamed(*w, "Clocks")), clocks);
        QCOMPARE(names(*w), (QStringList{"Clocks", "c", "div", "d", "e"})); // still collapsed
    }

    void removingRowsAndChangingRadixAreUndoable() {
        auto w = make();
        w->selectRow(rowNamed(*w, "b"));
        w->setRadixForSelection(Radix::Binary);
        QCOMPARE(w->undoText(), QString("Change radix"));
        w->undo();
        QCOMPARE(w->displayModel().root().children[0].children[1].radix, Radix::Default);
        w->selectRow(rowNamed(*w, "c"));
        w->removeSelection();
        QVERIFY(rowNamed(*w, "c") < 0);
        QVERIFY(w->undo());
        QVERIFY(rowNamed(*w, "c") >= 0);
        selectNames(*w, {"a", "b"});
        QVERIFY(w->busFromSelection("ab"));
        QCOMPARE(w->undoText(), QString("Make virtual bus"));
        QVERIFY(w->undo());
        QVERIFY(rowNamed(*w, "ab") < 0);
    }

    void theHistoryIsBounded() {
        auto w = make();
        w->selectRow(rowNamed(*w, "e"));
        for (int i = 0; i < 130; ++i)
            QVERIFY(w->moveSelectedRows(i % 2 == 0 ? -1 : 1));
        int undone = 0;
        while (w->undo())
            ++undone;
        QCOMPARE(undone, 100);
    }

    void addingSignalsStartsANewHistory() {
        auto w = make();
        w->selectRow(rowNamed(*w, "e"));
        QVERIFY(w->moveSelectedRows(-1));
        QVERIFY(w->canUndo());
        AddWaveRequest req;
        req.patterns = {"/clk"};
        QCOMPARE(w->addWaveSignals(req), std::size_t(1));
        QVERIFY(!w->canUndo());
        QVERIFY(!w->canRedo());
    }

    // ---- structure signal, restart ----------------------------------------------------------------
    void structureChangedIsEmittedByEveryEdit() {
        auto w = make();
        QSignalSpy spy(w.get(), &WaveWidget::structureChanged);
        w->selectRow(rowNamed(*w, "d"));
        QVERIFY(w->moveSelectedRows(-1));
        QVERIFY(w->groupSelected());
        QVERIFY(w->ungroupSelected());
        QVERIFY(w->renameItem(w->itemIdAtRow(rowNamed(*w, "div")), "x"));
        QVERIFY(w->insertDividerBelow("y"));
        w->removeSelection();
        QCOMPARE(spy.count(), 6);
        w->clearWave();
        QCOMPARE(spy.count(), 7);
    }

    void rowsKeepTheirOrderWhenTheSessionGetsANewStore() {
        auto w = make();
        selectNames(*w, {"d", "e"});
        QVERIFY(w->groupSelected("Tail"));
        w->selectRow(rowNamed(*w, "c"));
        QVERIFY(w->moveSelectedRowsToEdge(true));
        const std::string order = w->displayModel().serialize();
        const QStringList shown = names(*w);
        w->setStore(makeStore()); // what a restart does: a new run, the same rows
        QCOMPARE(w->displayModel().serialize(), order);
        QCOMPARE(names(*w), shown);
        QVERIFY(w->rowHasSource(rowNamed(*w, "c")));
        QVERIFY(w->selectedItemIds().empty());
        QVERIFY(w->canUndo()); // the history belongs to the rows, which are the same
        QVERIFY(w->undo());
    }

    // ---- large lists ------------------------------------------------------------------------------
    void movingARowAmong5000IsQuick() {
        DisplayModel m;
        for (int i = 0; i < 5000; ++i)
            m.root().children.push_back(
                sig(qPrintable(QString("r%1").arg(i)), i % 2 ? "top.cnt" : "top.clk"));
        auto w = make(std::move(m), 600);
        w->selectRow(2500);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i)
            QVERIFY(w->moveSelectedRows(i % 2 == 0 ? -1 : 1));
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0;
        qInfo("move one row among 5000: %.1f ms per edit (build type dependent)", ms);
        QVERIFY(ms < 500.0);
        const auto t1 = std::chrono::steady_clock::now();
        const QImage img = w->grab().toImage();
        qInfo("paint after the edit: %.1f ms",
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
        QVERIFY(!img.isNull());
    }
};

QTEST_MAIN(TestWaveArrange)
#include "test_wave_arrange.moc"
