// SPDX-License-Identifier: GPL-3.0-or-later
// Objects pane: drag of signals to the wave window, port mode icons. The drop side is tested with the real
// drag events (QDragEnterEvent, QDragMoveEvent, QDropEvent) sent to the wave widget; the platform's modal
// drag loop is replaced by the pane's drag runner.
#include "app/objects_pane.hpp"
#include "app/port_icons.hpp"
#include "wave/signal_mime.hpp"
#include "wave/wave_widget.hpp"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

#include <memory>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(top, "cnt", SignalKind::Logic, 8);
    st->addSignal(top, "lo", SignalKind::Logic, 4);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 64);
    const ScopeId dut = st->addScope(top, "dut");
    st->addSignal(dut, "q", SignalKind::Logic, 1);
    st->addSignal(dut, "d", SignalKind::Logic, 1);
    for (Time t = 0; t <= 1000; t += 50) {
        st->mutableSignal(volt).appendReal(t, static_cast<double>(t) / 100.0);
        st->mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
    }
    st->mutableSignal(cnt).appendLogic(0, "00000000");
    st->extendTimeRange(0);
    st->extendTimeRange(1000);
    return st;
}

DisplayItem sig(const char* label, const char* path) {
    DisplayItem s = DisplayModel::makeSignal(path);
    s.label = label;
    return s;
}

// Clocks[ a b ]  c  --div--  d
DisplayModel makeModel() {
    DisplayModel m;
    DisplayItem clocks = DisplayModel::makeGroup("Clocks");
    clocks.children.push_back(sig("a", "top.clk"));
    clocks.children.push_back(sig("b", "top.cnt"));
    m.root().children.push_back(std::move(clocks));
    m.root().children.push_back(sig("c", "top.lo"));
    m.root().children.push_back(DisplayModel::makeDivider("div"));
    m.root().children.push_back(sig("d", "top.clk"));
    return m;
}

QStringList names(const WaveWidget& w) {
    QStringList l;
    for (int i = 0; i < w.rowCount(); ++i)
        l << QString::fromStdString(w.rowName(i));
    return l;
}

int rowNamed(const WaveWidget& w, const QString& name) {
    for (int i = 0; i < w.rowCount(); ++i)
        if (QString::fromStdString(w.rowName(i)) == name && w.rowBit(i) < 0)
            return i;
    return -1;
}

QTreeWidgetItem* findItem(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i)
        if (parent->child(i)->text(0) == name)
            return parent->child(i);
    return nullptr;
}

} // namespace

class TestObjectsDrag : public QObject {
    Q_OBJECT

    std::unique_ptr<WaveWidget> make(DisplayModel model = makeModel(), int height = 320) {
        auto w = std::make_unique<WaveWidget>();
        w->resize(900, height);
        w->setDisplayModel(std::move(model));
        w->setStore(makeStore());
        return w;
    }

    // enter, move, drop: what the platform delivers for a drop at `pos`. Returns whether the drop was
    // accepted.
    static bool dropAt(WaveWidget& w, const QMimeData* mime, const QPoint& pos, bool* entered = nullptr) {
        QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &enter);
        if (entered != nullptr)
            *entered = enter.isAccepted();
        if (!enter.isAccepted())
            return false;
        QDragMoveEvent move(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &move);
        QDropEvent drop(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &drop);
        return drop.isAccepted();
    }

    static std::unique_ptr<QMimeData> mimeOf(const QStringList& paths) {
        return std::unique_ptr<QMimeData>(makeSignalMime(paths));
    }

    static QPoint nameAt(const WaveWidget& w, const QString& row, double frac = 0.5, int x = 60) {
        return QPoint(x, w.rowTop(rowNamed(w, row)) + static_cast<int>(frac * w.rowHeight()));
    }

private slots:
    void initTestCase() { qRegisterMetaType<PortMode>(); }

    // ---- the pane: what a drag carries -------------------------------------------------------------
    void paneMimeHasPathsInAddWaveNaming() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        QTreeWidgetItem* top = pane.tree()->topLevelItem(0);
        QVERIFY(top != nullptr);
        findItem(top, "clk")->setSelected(true);
        findItem(findItem(top, "dut"), "q")->setSelected(true);
        const std::unique_ptr<QMimeData> mime(pane.mimeDataForSelection());
        QVERIFY(mime != nullptr);
        QVERIFY(hasSignalPaths(mime.get()));
        const QStringList expect{"/top/clk", "/top/dut/q"};
        QCOMPARE(signalPathsFromMime(mime.get()), expect);
        QCOMPARE(mime->text(), QString("/top/clk\n/top/dut/q"));
    }

    void aScopeStandsForItsSignals() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        findItem(pane.tree()->topLevelItem(0), "dut")->setSelected(true);
        const std::unique_ptr<QMimeData> mime(pane.mimeDataForSelection());
        QVERIFY(mime != nullptr);
        QCOMPARE(signalPathsFromMime(mime.get()), (QStringList{"/top/dut/q", "/top/dut/d"}));
    }

    void nothingSelectedNoMime() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        QVERIFY(pane.mimeDataForSelection() == nullptr);
        int runs = 0;
        pane.setDragRunner([&](QDrag*) {
            ++runs;
            return Qt::IgnoreAction;
        });
        QVERIFY(!pane.startSignalDrag());
        QCOMPARE(runs, 0);
    }

    void foreignMimeIsNotOurs() {
        QMimeData plain;
        plain.setText("/top/clk");
        QVERIFY(!hasSignalPaths(&plain));
        QVERIFY(signalPathsFromMime(&plain).isEmpty());
        QVERIFY(!hasSignalPaths(nullptr));
    }

    void dragPixmapShowsNamesAndCount() {
        const QPixmap one = ObjectsPane::dragPixmap({"clk"});
        const QPixmap many = ObjectsPane::dragPixmap({"a", "b", "c", "d", "e", "f"});
        QVERIFY(!one.isNull());
        QVERIFY(many.height() > one.height()); // more lines (capped, plus "... and N more")
        const QPixmap hi = ObjectsPane::dragPixmap({"clk"}, 2.0);
        QCOMPARE(hi.devicePixelRatio(), 2.0);
        QCOMPARE(hi.width(), one.width() * 2);
        // the picture is not empty
        const QImage img = one.toImage();
        bool any = false;
        for (int y = 0; y < img.height() && !any; ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0) {
                    any = true;
                    break;
                }
        QVERIFY(any);
    }

    void realMouseDragStartsTheDrag() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.resize(360, 300);
        pane.setStore(st.get());
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));
        QTreeWidget* tree = pane.tree();
        QTreeWidgetItem* top = tree->topLevelItem(0);
        QTreeWidgetItem* clk = findItem(top, "clk");
        QTreeWidgetItem* cnt = findItem(top, "cnt");
        QVERIFY(clk != nullptr && cnt != nullptr);
        clk->setSelected(true);
        cnt->setSelected(true);

        QStringList got;
        QSize pixSize;
        pane.setDragRunner([&](QDrag* d) {
            got = signalPathsFromMime(d->mimeData());
            pixSize = d->pixmap().size();
            return Qt::IgnoreAction;
        });
        const QPoint from = tree->visualItemRect(cnt).center();
        QTest::mousePress(tree->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(tree->viewport(), from + QPoint(4, 4));
        QTest::mouseMove(tree->viewport(), from + QPoint(30, 30));
        QCOMPARE(got, (QStringList{"/top/clk", "/top/cnt"}));
        QVERIFY(!pixSize.isEmpty());
        QTest::mouseRelease(tree->viewport(), Qt::LeftButton, Qt::NoModifier, from + QPoint(30, 30));
    }

    void contextMenuAndReturnStillAdd() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        QSignalSpy spy(&pane, &ObjectsPane::addToWave);
        QTreeWidgetItem* clk = findItem(pane.tree()->topLevelItem(0), "clk");
        clk->setSelected(true);
        pane.tree()->setCurrentItem(clk);
        QTest::keyClick(pane.tree(), Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{"top.clk"});
        std::unique_ptr<QMenu> menu(pane.createContextMenu());
        menu->findChild<QAction*>("addToWave")->trigger();
        QCOMPARE(spy.count(), 2);
    }

    // ---- the wave widget: drops --------------------------------------------------------------------
    void dropOnEmptySpaceAppendsAtTheEnd() {
        auto w = make();
        QCOMPARE(names(*w), (QStringList{"Clocks", "a", "b", "c", "div", "d"}));
        QSignalSpy structure(w.get(), &WaveWidget::structureChanged);
        const auto mime = mimeOf({"/top/lo"});
        const QPoint below(60, w->rowTop(w->rowCount() - 1) + 3 * w->rowHeight());
        QVERIFY(w->signalDropTargetAt(below).valid);
        QVERIFY(dropAt(*w, mime.get(), below));
        QCOMPARE(w->rowCount(), 7);
        QCOMPARE(w->displayModel().root().children.back().path, std::string("top.lo"));
        QCOMPARE(structure.count() >= 1, true);
        QVERIFY(!w->signalDragActive());
        QCOMPARE(w->selectedCount(), 1); // the new row is selected
    }

    void dropBetweenRowsInTheNameColumn() {
        auto w = make();
        const auto mime = mimeOf({"/top/volt", "/top/cnt"});
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "c", 0.2))); // upper half of row c: before it
        const auto& ch = w->displayModel().root().children;
        QCOMPARE(ch.size(), std::size_t(6));
        QCOMPARE(ch[1].path, std::string("top.volt"));
        QCOMPARE(ch[2].path, std::string("top.cnt"));
        QCOMPARE(ch[3].label, std::string("c"));
        QCOMPARE(w->selectedCount(), 2);
    }

    void dropOnTheWaveAreaUsesTheRowUnderThePointer() {
        auto w = make();
        const QPoint p = nameAt(*w, "d", 0.8, w->waveLeft() + 100); // lower half of d, over the waveform
        const auto t = w->signalDropTargetAt(p);
        QVERIFY(t.valid);
        QVERIFY(!t.outside);
        const auto mime = mimeOf({"/top/lo"});
        QVERIFY(dropAt(*w, mime.get(), p));
        QCOMPARE(w->displayModel().root().children.back().path, std::string("top.lo")); // after d
        // the value column too
        auto w2 = make();
        QVERIFY(dropAt(*w2, mime.get(), nameAt(*w2, "c", 0.2, w2->waveLeft() - 30)));
        QCOMPARE(w2->displayModel().root().children[1].path, std::string("top.lo"));
    }

    void dropOnAGroupRowPutsTheSignalsIntoIt() {
        auto w = make();
        const QPoint onGroup = nameAt(*w, "Clocks", 0.5);
        const auto t = w->signalDropTargetAt(onGroup);
        QVERIFY(t.valid);
        QVERIFY(t.intoGroup);
        const auto mime = mimeOf({"/top/volt", "/top/lo"});
        QVERIFY(dropAt(*w, mime.get(), onGroup));
        const DisplayItem& g = w->displayModel().root().children.front();
        QCOMPARE(g.children.size(), std::size_t(4));
        QCOMPARE(g.children[2].path, std::string("top.volt"));
        QCOMPARE(g.children[3].path, std::string("top.lo"));
        QCOMPARE(w->displayModel().root().children.size(), std::size_t(4)); // nothing leaked to the top level
    }

    void dropIntoACollapsedGroupExpandsIt() {
        DisplayModel m = makeModel();
        m.root().children.front().expanded = false;
        auto w = make(std::move(m));
        const auto mime = mimeOf({"/top/volt"});
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "Clocks", 0.5)));
        QVERIFY(w->displayModel().root().children.front().expanded);
        QVERIFY(rowNamed(*w, "top.volt") >= 0 || rowNamed(*w, "volt") >= 0);
    }

    void dropInsideAGroupBetweenItsRows() {
        auto w = make();
        const auto mime = mimeOf({"/top/volt"});
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "b", 0.2))); // before b, inside Clocks
        const DisplayItem& g = w->displayModel().root().children.front();
        QCOMPARE(g.children.size(), std::size_t(3));
        QCOMPARE(g.children[1].path, std::string("top.volt"));
        QCOMPARE(g.children[2].label, std::string("b"));
    }

    void dropOnAnEmptyWaveWindow() {
        auto w = make(DisplayModel{});
        QCOMPARE(w->rowCount(), 0);
        const auto mime = mimeOf({"/top/clk", "/top/cnt"});
        QVERIFY(dropAt(*w, mime.get(), QPoint(80, w->rowsTop() + 40)));
        QCOMPARE(w->rowCount(), 2);
    }

    void duplicatesAreAddedAsAddWaveDoes() {
        auto w = make();
        const auto mime = mimeOf({"/top/clk"});
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "d", 0.8)));
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "d", 0.8)));
        int n = 0;
        for (const DisplayItem& it : w->displayModel().root().children)
            if (it.path == "top.clk" && it.label.empty())
                ++n;
        QCOMPARE(n, 2);
        // the same rows `add wave` makes: same item, same radix default
        DisplayModel viaCommand;
        AddWaveRequest req;
        req.patterns = {"/top/clk"};
        QCOMPARE(applyAddWave(viaCommand, *w->store(), req), std::size_t(1));
        const DisplayItem& expect = viaCommand.root().children.front();
        const DisplayItem& got = w->displayModel().root().children.back();
        QCOMPARE(got.path, expect.path);
        QCOMPARE(int(got.radix), int(expect.radix));
        QCOMPARE(got.showBits, expect.showBits);
    }

    void vectorsAndRealsKeepTheirTreatment() {
        auto w = make(DisplayModel{});
        const auto mime = mimeOf({"/top/cnt", "/top/volt", "/top/clk"});
        QVERIFY(dropAt(*w, mime.get(), QPoint(80, w->rowsTop() + 10)));
        QCOMPARE(w->rowCount(), 3);
        QVERIFY(w->rowExpandable(0));  // the 8 bit vector can show its bits
        QVERIFY(!w->rowExpandable(1)); // a real cannot
        QVERIFY(w->doScript().find("add wave /top/cnt") != std::string::npos);
        QVERIFY(w->doScript().find("add wave /top/clk") != std::string::npos);
    }

    void aDropIsOneUndoStep() {
        auto w = make();
        const QStringList before = names(*w);
        const std::string scriptBefore = w->doScript();
        const auto mime = mimeOf({"/top/volt", "/top/lo"});
        QVERIFY(!w->canUndo());
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "c", 0.2)));
        QVERIFY(w->canUndo());
        QVERIFY(w->undoText().startsWith("Add signals"));
        const QStringList after = names(*w);
        QVERIFY(after != before);
        QVERIFY(w->undo());
        QCOMPARE(names(*w), before);
        QCOMPARE(w->doScript(), scriptBefore);
        QVERIFY(w->redo());
        QCOMPARE(names(*w), after);
        QVERIFY(w->undo());
        QVERIFY(!w->canUndo());
    }

    void doScriptCarriesTheDroppedSignals() {
        auto w = make(DisplayModel{});
        const auto mime = mimeOf({"/top/dut/q"});
        QVERIFY(dropAt(*w, mime.get(), QPoint(80, w->rowsTop() + 10)));
        const std::string script = w->doScript();
        QVERIFY2(script.find("add wave /top/dut/q") != std::string::npos, script.c_str());
        // and the script rebuilds it
        auto w2 = make(DisplayModel{});
        w2->loadDoScript(script);
        QCOMPARE(w2->rowCount(), 1);
    }

    void refusedDropsChangeNothing() {
        auto w = make();
        const QStringList before = names(*w);
        // outside the rows: the ruler
        const auto mime = mimeOf({"/top/clk"});
        const QPoint ruler(60, 10);
        QVERIFY(w->signalDropTargetAt(ruler).outside);
        QVERIFY(!dropAt(*w, mime.get(), ruler));
        QCOMPARE(names(*w), before);
        QVERIFY(!w->signalDragActive());
        // an unknown signal
        QSignalSpy rejected(w.get(), &WaveWidget::editRejected);
        QCOMPARE(w->dropSignals({"/top/nothing"}), std::size_t(0));
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(names(*w), before);
        QVERIFY(!w->canUndo());
        // foreign data is not accepted at all
        QMimeData plain;
        plain.setText("/top/clk");
        bool entered = true;
        QVERIFY(!dropAt(*w, &plain, nameAt(*w, "c"), &entered));
        QVERIFY(!entered);
        QCOMPARE(names(*w), before);
    }

    void movingOverRulerGivesForbiddenFeedback() {
        auto w = make();
        const auto mime = mimeOf({"/top/clk"});
        QDragEnterEvent enter(nameAt(*w, "c"), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.get(), &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(w->signalDragActive());
        QVERIFY(w->rowDropIndicator().valid);
        QDragMoveEvent over(QPoint(60, 10), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.get(), &over);
        QVERIFY(!over.isAccepted()); // an ignored move shows the "not allowed" cursor
        QVERIFY(!w->rowDropIndicator().valid);
        QDragMoveEvent back(nameAt(*w, "d"), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.get(), &back);
        QVERIFY(back.isAccepted());
        QVERIFY(w->rowDropIndicator().valid);
        QDragLeaveEvent leave;
        QApplication::sendEvent(w.get(), &leave);
        QVERIFY(!w->signalDragActive());
        QVERIFY(!w->rowDropIndicator().valid);
    }

    void dropLineIsTheRowDragsLine() {
        auto w = make();
        const QPoint p = nameAt(*w, "c", 0.2);
        const auto ext = w->signalDropTargetAt(p);
        const auto rows = w->dropTargetAt(p);
        QCOMPARE(ext.parent, rows.parent);
        QCOMPARE(ext.index, rows.index);
        QCOMPARE(ext.lineY, rows.lineY);
        QCOMPARE(ext.lineX, rows.lineX);
        QCOMPARE(ext.intoGroup, rows.intoGroup);
        const auto g = w->signalDropTargetAt(nameAt(*w, "Clocks", 0.5));
        QVERIFY(g.intoGroup);
        QCOMPARE(g.parent, w->displayModel().root().children.front().id);
    }

    void dropIndicatorIsPaintedWhileDragging() {
        auto w = make();
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        const auto mime = mimeOf({"/top/clk"});
        const QPoint p = nameAt(*w, "c", 0.2);
        const QImage plain = w->grab().toImage();
        QDragEnterEvent enter(p, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.get(), &enter);
        const QImage during = w->grab().toImage();
        QVERIFY(plain != during);
        const int lineY = w->rowDropIndicator().lineY;
        QVERIFY(lineY >= 0);
        QColor c = during.pixelColor(w->rowDropIndicator().lineX + 20, lineY);
        QVERIFY2(c.blue() > 200 && c.red() < 120, qPrintable(c.name())); // the blue of the row drag
        QDragLeaveEvent leave;
        QApplication::sendEvent(w.get(), &leave);
    }

    void autoScrollsNearTheEdges() {
        DisplayModel m;
        for (int i = 0; i < 40; ++i)
            m.root().children.push_back(sig("s", "top.clk"));
        auto w = make(std::move(m), 260);
        QVERIFY(w->verticalScrollBar()->maximum() > 0);
        const auto mime = mimeOf({"/top/lo"});
        QDragEnterEvent enter(QPoint(100, w->rowsBottom() - 4), Qt::CopyAction, mime.get(), Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(w.get(), &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(w->autoScrollStep() >
                0); // the pointer is at the bottom edge, over the wave widget's whole width
        QVERIFY(w->verticalScrollBar()->value() > 0);
        QDragMoveEvent top(QPoint(w->waveLeft() + 50, w->rowsTop() + 3), Qt::CopyAction, mime.get(),
                           Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.get(), &top);
        QVERIFY(w->autoScrollStep() < 0); // also over the wave area
        QDragLeaveEvent leave;
        QApplication::sendEvent(w.get(), &leave);
        QCOMPARE(w->autoScrollStep(), 0); // stops when the drag ends
    }

    void endToEndFromThePane() {
        auto w = make();
        ObjectsPane pane;
        pane.setStore(w->store());
        QTreeWidgetItem* top = pane.tree()->topLevelItem(0);
        findItem(top, "volt")->setSelected(true);
        findItem(top, "lo")->setSelected(true);
        const std::unique_ptr<QMimeData> mime(pane.mimeDataForSelection());
        QVERIFY(mime != nullptr);
        QVERIFY(dropAt(*w, mime.get(), nameAt(*w, "Clocks", 0.5)));
        QCOMPARE(w->displayModel().root().children.front().children.size(), std::size_t(4));
    }

    // ---- port mode icons ---------------------------------------------------------------------------
    void iconsAreDistinctAndNotEmpty() {
        const PortMode all[] = {PortMode::In,     PortMode::Out,      PortMode::InOut,
                                PortMode::Buffer, PortMode::Internal, PortMode::Unknown};
        QList<QImage> images;
        for (PortMode m : all) {
            const QImage img = portModePixmap(m, 16).toImage();
            bool any = false;
            for (int y = 0; y < img.height() && !any; ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 40) {
                        any = true;
                        break;
                    }
            QVERIFY2(any, qPrintable(portModeText(m)));
            for (const QImage& other : images)
                QVERIFY(other != img);
            images << img;
            QVERIFY(!portModeIcon(m).pixmap(QSize(16, 16)).isNull());
            QVERIFY(!portModeText(m).isEmpty());
        }
    }

    void iconsScaleWithTheDevicePixelRatio() {
        const QPixmap p100 = portModePixmap(PortMode::InOut, 16, 1.0);
        const QPixmap p150 = portModePixmap(PortMode::InOut, 16, 1.5);
        QCOMPARE(p100.width(), 16);
        QCOMPARE(p150.width(), 24);
        QCOMPARE(p150.devicePixelRatio(), 1.5);
    }

    void iconOutlinesFollowTheTheme() {
        const QPalette saved = QApplication::palette();
        QPalette dark = saved;
        dark.setColor(QPalette::Text, QColor(0xe8, 0xe8, 0xe8));
        QApplication::setPalette(dark);
        const QImage onDark = portModePixmap(PortMode::In, 16).toImage();
        QPalette light = saved;
        light.setColor(QPalette::Text, QColor(0x20, 0x20, 0x20));
        QApplication::setPalette(light);
        const QImage onLight = portModePixmap(PortMode::In, 16).toImage();
        QApplication::setPalette(saved);
        QVERIFY(onDark != onLight); // the box outline changes, the green arrow does not
    }

    void modesAreShownAndSurviveRebuild() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        QTreeWidgetItem* top = pane.tree()->topLevelItem(0);
        QTreeWidgetItem* clk = findItem(top, "clk");
        QCOMPARE(clk->data(0, Qt::UserRole + 2).toInt(), int(PortMode::Unknown)); // nothing known: neutral
        QVERIFY(!clk->icon(0).isNull());
        QVERIFY(clk->toolTip(0).contains("direction not known"));

        QHash<QString, PortMode> modes;
        modes.insert("top.clk", PortMode::In);
        modes.insert("top.cnt", PortMode::Out);
        modes.insert("top.lo", PortMode::InOut);
        modes.insert("top.volt", PortMode::Buffer);
        modes.insert("top.dut.q", PortMode::Internal);
        pane.setPortModes(modes);
        QCOMPARE(pane.portModeOf("top.clk"), PortMode::In);
        QCOMPARE(pane.portModeOf("top.dut.d"), PortMode::Unknown);
        QVERIFY(clk->toolTip(0).contains("input port"));
        QVERIFY(clk->toolTip(0).contains("logic"));
        QVERIFY(findItem(top, "cnt")->toolTip(0).contains("8 bits"));
        QVERIFY(findItem(top, "volt")->toolTip(0).contains("buffer port"));
        QVERIFY(findItem(findItem(top, "dut"), "q")->toolTip(0).contains("internal signal"));

        pane.setScope("top.dut"); // a rebuild keeps what is known
        QTreeWidgetItem* q = findItem(pane.tree()->invisibleRootItem(), "q");
        QVERIFY(q != nullptr);
        QCOMPARE(q->data(0, Qt::UserRole + 2).toInt(), int(PortMode::Internal));
        QVERIFY(q->toolTip(0).contains("internal signal"));
    }

    void filterAndSelectionAreNotAffectedByIcons() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        pane.setPortModes({{"top.clk", PortMode::In}, {"top.cnt", PortMode::Out}});
        QCOMPARE(pane.signalCount(), 6);
        pane.setFilter("c*");
        QCOMPARE(pane.visibleSignalCount(), 2); // clk, cnt
        pane.setFilter("");
        QCOMPARE(pane.visibleSignalCount(), 6);
        QCOMPARE(pane.tree()->columnCount(), 3); // no extra column
        QTreeWidgetItem* clk = findItem(pane.tree()->topLevelItem(0), "clk");
        QCOMPARE(clk->text(0), QString("clk")); // the name is the name, the icon is separate
        clk->setSelected(true);
        QCOMPARE(pane.selectedSignalPaths(), QStringList{"top.clk"});
    }

    void modesAreClearedWithANewStore() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.setStore(st.get());
        pane.setPortModes({{"top.clk", PortMode::In}});
        pane.setStore(st.get());
        QCOMPARE(pane.portModeOf("top.clk"), PortMode::Unknown);
    }

    void labelsFitTheNameColumn() {
        const auto st = makeStore();
        ObjectsPane pane;
        pane.resize(300, 300);
        pane.setStore(st.get());
        pane.setPortModes({{"top.clk", PortMode::In}});
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));
        QTreeWidget* tree = pane.tree();
        QTreeWidgetItem* clk = findItem(tree->topLevelItem(0), "clk");
        QVERIFY(clk != nullptr);
        QCOMPARE(tree->iconSize(), QSize(16, 16));
        QVERIFY(tree->sizeHintForIndex(tree->indexFromItem(clk)).height() >= 16); // the icon fits the row
        QVERIFY(tree->columnWidth(0) > 60);
    }
    // Pictures for the documentation: LVDSIM_SHOT_DIR=dir (and QT_SCALE_FACTOR=1.5 for the high-DPI one).
    void screenshotsForDocs() {
        const QString dir = qEnvironmentVariable("LVDSIM_SHOT_DIR");
        if (dir.isEmpty())
            QSKIP("LVDSIM_SHOT_DIR not set");
        const QString tag = qEnvironmentVariable("LVDSIM_SHOT_TAG", "100");
        auto st = std::make_shared<WaveStore>();
        const ScopeId top = st->addScope(kRootScope, "top");
        const ScopeId dut = st->addScope(top, "dut");
        const struct {
            const char* name;
            SignalKind kind;
            unsigned w;
            PortMode mode;
        } sigs[] = {{"clk", SignalKind::Logic, 1, PortMode::In},
                    {"rst", SignalKind::Logic, 1, PortMode::In},
                    {"din", SignalKind::Logic, 8, PortMode::In},
                    {"dout", SignalKind::Logic, 8, PortMode::Out},
                    {"data", SignalKind::Logic, 8, PortMode::InOut},
                    {"count", SignalKind::Logic, 4, PortMode::Buffer},
                    {"state", SignalKind::String, 0, PortMode::Internal},
                    {"gain", SignalKind::Real, 64, PortMode::Unknown}};
        QHash<QString, PortMode> modes;
        for (const auto& g : sigs) {
            const SignalId id = st->addSignal(dut, g.name, g.kind, g.w);
            if (g.kind == SignalKind::Logic)
                st->mutableSignal(id).appendLogic(0, std::string(g.w, '0'));
            modes.insert(QString("top.dut.") + g.name, g.mode);
        }
        st->extendTimeRange(0);
        st->extendTimeRange(1000);

        QWidget host;
        auto* lay = new QHBoxLayout(&host);
        auto* pane = new ObjectsPane;
        pane->setStore(st.get());
        pane->setPortModes(modes);
        pane->tree()->expandAll();
        pane->setMinimumWidth(300);
        auto* w = new WaveWidget;
        DisplayModel m;
        DisplayItem grp = DisplayModel::makeGroup("Control");
        grp.children.push_back(sig("clk", "top.dut.clk"));
        m.root().children.push_back(std::move(grp));
        m.root().children.push_back(sig("dout", "top.dut.dout"));
        w->setDisplayModel(std::move(m));
        w->setStore(st);
        lay->addWidget(pane, 1);
        lay->addWidget(w, 3);
        host.resize(900, 330);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        // select two signals, show the drop line between rows of the wave window
        QTreeWidgetItem* dutItem = pane->tree()->topLevelItem(0)->child(0);
        for (int i = 0; i < dutItem->childCount(); ++i)
            if (dutItem->child(i)->text(0) == "din" || dutItem->child(i)->text(0) == "data")
                dutItem->child(i)->setSelected(true);
        const std::unique_ptr<QMimeData> mime(pane->mimeDataForSelection());
        QDragEnterEvent enter(nameAt(*w, "dout", 0.2), Qt::CopyAction, mime.get(), Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(w, &enter);
        QVERIFY(host.grab().save(dir + "/objects_icons_" + tag + ".png"));
        QDragLeaveEvent leave;
        QApplication::sendEvent(w, &leave);
        const qreal dpr = host.devicePixelRatioF();
        QVERIFY(
            ObjectsPane::dragPixmap({"din", "data"}, dpr).save(dir + "/objects_drag_pixmap_" + tag + ".png"));
    }
};

QTEST_MAIN(TestObjectsDrag)
#include "test_objects_drag.moc"
