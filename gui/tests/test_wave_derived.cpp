// SPDX-License-Identifier: GPL-3.0-or-later
// Expression rows, datasets and compare rows in the wave window, their dialogs, the List window columns and
// the layout formats.
#include "wave/compare_dialog.hpp"
#include "wave/expression_dialog.hpp"
#include "wave/list_model.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

// top.clk toggles every 10 up to 200; top.en: 0, 1 at 40, 0 at 120; top.cnt (8 bit) counts on every rising
// clock edge (1 at 10, 2 at 30, ...).
std::shared_ptr<WaveStore> makeStore(bool variant = false) {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId en = st->addSignal(top, "en", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(top, "cnt[7:0]", SignalKind::Logic, 8);
    int n = 0;
    for (Time t = 0; t <= 200; t += 10) {
        const bool high = (t / 10) % 2 == 1;
        st->mutableSignal(clk).appendLogic(t, high ? "1" : "0");
        if (t == 0 || high) {
            std::string bits(8, '0');
            const int v = variant && n == 5 ? 9 : n; // the variant differs in one count, from 90 to 110
            for (int b = 0; b < 8; ++b)
                bits[7 - static_cast<std::size_t>(b)] = ((v >> b) & 1) ? '1' : '0';
            st->mutableSignal(cnt).appendLogic(t, bits);
            ++n;
        }
    }
    st->mutableSignal(en).appendLogic(0, "0");
    st->mutableSignal(en).appendLogic(variant ? 42 : 40, "1");
    st->mutableSignal(en).appendLogic(120, "0");
    st->extendTimeRange(0);
    st->extendTimeRange(200);
    return st;
}

struct Fixture {
    WaveWidget w;
    std::shared_ptr<WaveStore> store = makeStore();
    Fixture() {
        w.resize(1000, 400);
        DisplayModel m;
        m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
        m.root().children.push_back(DisplayModel::makeSignal("top.en"));
        m.root().children.push_back(DisplayModel::makeSignal("top.cnt[7:0]", Radix::Unsigned));
        w.setDisplayModel(m);
        w.setStore(store);
        w.setView(0.0, 0.25);
    }
    int row(const std::string& name) const {
        for (int i = 0; i < w.rowCount(); ++i)
            if (w.rowName(i) == name && w.rowBit(i) < 0)
                return i;
        return -1;
    }
    std::string valueAt(const std::string& name, double t) {
        w.setCursorTime(0, t);
        return w.rowValueText(row(name));
    }
};

QAction* findAction(QMenu* menu, const QString& name) {
    return menu->findChild<QAction*>(name);
}

} // namespace

class TestWaveDerived : public QObject {
    Q_OBJECT
private slots:
    void expressionRow() {
        Fixture f;
        QVERIFY(f.w.addExpression("gated", "clk and en"));
        const int r = f.row("gated");
        QVERIFY(r >= 0);
        QVERIFY(f.w.rowHasSource(r));
        QVERIFY(f.w.rowError(r).isEmpty());
        QCOMPARE(f.w.rowClass(r), WaveClass::Scalar);
        QVERIFY(f.w.rowSignal(r) != nullptr);
        QCOMPARE(f.valueAt("gated", 15), std::string("0"));
        QCOMPARE(f.valueAt("gated", 55), std::string("1"));
        QCOMPARE(f.valueAt("gated", 125), std::string("0"));
        QVERIFY(f.w.canUndo());

        QVERIFY(f.w.addExpression("big", "cnt > 5"));
        QCOMPARE(f.valueAt("big", 95), std::string("0"));
        QCOMPARE(f.valueAt("big", 115), std::string("1"));

        QVERIFY(!f.w.addExpression("bad", "clk and nosuch"));
        QCOMPARE(f.w.lastEditError(), QString("Expression: col 9: unknown signal 'nosuch'"));
        QVERIFY(!f.w.addExpression("", "clk"));
        QCOMPARE(f.row("bad"), -1);

        // a vector result can be shown in a radix, expanded into bits and drawn as an analog trace
        QVERIFY(f.w.addExpression("low", "cnt(3 downto 0)"));
        f.w.selectRow(f.row("low"));
        f.w.setRadixForSelection(Radix::Unsigned);
        QCOMPARE(f.valueAt("low", 115), std::string("6"));
        QVERIFY(f.w.rowExpandable(f.row("low")));
        f.w.toggleAnalogForSelection();
        QCOMPARE(f.w.rowClass(f.row("low")), WaveClass::Analog);
        QVERIFY(f.w.effectiveAnalogRange(f.row("low")).has_value());
    }

    void expressionErrorRowAndEdit() {
        Fixture f;
        DisplayModel m = f.w.displayModel();
        m.root().children.push_back(DisplayModel::makeExpression("broken", "clk and"));
        f.w.setDisplayModel(m);
        const int r = f.row("broken");
        QVERIFY(r >= 0);
        QVERIFY(!f.w.rowHasSource(r));
        QCOMPARE(f.w.rowError(r), QString("col 8: expression incomplete"));
        const ItemId id = f.w.itemIdAtRow(r);
        QVERIFY(f.w.editExpression(id, "fixed", "clk and en"));
        QVERIFY(f.w.rowHasSource(f.row("fixed")));
        QVERIFY(f.w.undo());
        QVERIFY(!f.w.rowHasSource(f.row("broken")));
    }

    void expressionSearch() {
        Fixture f;
        QVERIFY(f.w.addExpression("gated", "clk and en"));
        f.w.selectRow(f.row("gated"));
        f.w.setCursorTime(0, 0.0);
        QVERIFY(f.w.jumpToEdge(+1, EdgeKind::Rising));
        QCOMPARE(*f.w.cursorTime(0), 50.0);
        const auto res = f.w.findValue({SearchCondition::Equals, "0"}, +1);
        QVERIFY(res.found);
        QCOMPARE(res.time, 60.0);
    }

    void expressionFollowsLiveData() {
        Fixture f;
        QVERIFY(f.w.addExpression("gated", "clk and en"));
        const Signal* before = f.w.rowSignal(f.row("gated"));
        const std::size_t n = before->changeCount();
        f.store->mutableSignal(f.store->findSignal("top.en")).appendLogic(210, "1");
        f.store->mutableSignal(f.store->findSignal("top.clk")).appendLogic(210, "1");
        f.store->extendTimeRange(220);
        f.w.dataChanged();
        // the same signal object, grown: evaluated incrementally, not rebuilt
        QCOMPARE(f.w.rowSignal(f.row("gated")), before);
        QCOMPARE(before->changeCount(), n + 1);
        QCOMPARE(f.valueAt("gated", 215), std::string("1"));
    }

    void expressionDialog() {
        Fixture f;
        bool sawError = false;
        f.w.setExpressionDialogRunner([&](QDialog& d) {
            auto* dlg = qobject_cast<ExpressionDialog*>(&d);
            if (dlg == nullptr)
                return false;
            dlg->exprEdit()->setText("clk and");
            sawError = dlg->statusLabel()->text().contains("col 8") && !dlg->isValid();
            auto* ok = dlg->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            if (ok->isEnabled())
                return false;
            dlg->exprEdit()->setText("cnt = 16#05#");
            if (!dlg->isValid() || !ok->isEnabled() || !dlg->statusLabel()->text().contains("1 bit"))
                return false;
            dlg->nameEdit()->setText("is5");
            dlg->accept();
            return true;
        });
        QVERIFY(f.w.showExpressionDialog());
        QVERIFY(sawError);
        QVERIFY(f.row("is5") >= 0);
        QCOMPARE(f.valueAt("is5", 95), std::string("1"));

        // editing: the dialog starts with the row's name and text
        const ItemId id = f.w.itemIdAtRow(f.row("is5"));
        QString seenName, seenText;
        f.w.setExpressionDialogRunner([&](QDialog& d) {
            auto* dlg = qobject_cast<ExpressionDialog*>(&d);
            seenName = dlg->nameEdit()->text();
            seenText = dlg->exprEdit()->text();
            dlg->exprEdit()->setText("cnt = 6");
            return true;
        });
        QVERIFY(f.w.showExpressionDialog(id));
        QCOMPARE(seenName, QString("is5"));
        QCOMPARE(seenText, QString("cnt = 16#05#"));
        QCOMPARE(f.valueAt("is5", 115), std::string("1"));

        // the context menu offers both
        f.w.selectRow(f.row("is5"));
        std::unique_ptr<QMenu> menu(f.w.createContextMenu(f.row("is5")));
        QVERIFY(findAction(menu.get(), "addExpression") != nullptr);
        QVERIFY(findAction(menu.get(), "editExpression") != nullptr);
    }

    void datasetsAndCompareRows() {
        Fixture f;
        QVERIFY(f.w.addDataset("ref", makeStore(true)).isEmpty());
        QCOMPARE(f.w.datasetNames(), QStringList{"ref"});
        QVERIFY(!f.w.addDataset("2bad", makeStore()).isEmpty());
        auto ps = makeStore();
        ps->setTimescaleExponent(-12);
        QVERIFY(!f.w.addDataset("ps", ps).isEmpty()); // other time unit

        f.w.selectRow(f.row("top.en"));
        f.w.selectRow(f.row("top.cnt[7:0]"), true);
        std::unique_ptr<QMenu> menu(f.w.createContextMenu(f.row("top.en")));
        QVERIFY(findAction(menu.get(), "compareWith_ref") != nullptr);
        QVERIFY(findAction(menu.get(), "addFromDataset_ref") != nullptr);
        QVERIFY(findAction(menu.get(), "compareWaveforms") != nullptr);
        menu.reset();

        QCOMPARE(f.w.compareSelectionWith("ref"), std::size_t{2});
        // below each row: the reference signal and the compare row
        const int en = f.row("top.en");
        QCOMPARE(f.w.rowName(en + 1), std::string("ref:top.en"));
        QCOMPARE(f.w.rowName(en + 2), std::string("diff top.en"));
        QVERIFY(f.w.rowHasSource(en + 1));
        QVERIFY(f.w.rowHasSource(en + 2));
        QCOMPARE(f.w.compareRowCount(), 2);
        QCOMPARE(f.w.differenceCount(), std::size_t{2}); // en at 40..42, cnt at 90..110

        f.w.clearSelection();
        f.w.setCursorTime(0, 0.0);
        QSignalSpy msg(&f.w, &WaveWidget::navigationMessage);
        QVERIFY(f.w.jumpToDifference(+1));
        QCOMPARE(*f.w.cursorTime(0), 40.0);
        QVERIFY(msg.last().at(0).toString().contains("2 in all"));
        QVERIFY(f.w.jumpToDifference(+1));
        QCOMPARE(*f.w.cursorTime(0), 90.0);
        QVERIFY(!f.w.jumpToDifference(+1));
        QVERIFY(f.w.jumpToDifference(-1));
        QCOMPARE(*f.w.cursorTime(0), 40.0);
        // the difference rows are ordinary one bit rows for the edge search too
        f.w.selectRow(en + 2);
        f.w.setCursorTime(0, 0.0);
        QVERIFY(f.w.jumpToEdge(+1, EdgeKind::Falling));
        QCOMPARE(*f.w.cursorTime(0), 42.0);

        // the layout keeps dataset and compare rows
        const std::string saved = f.w.displayModel().serialize();
        auto parsed = DisplayModel::parse(saved);
        QVERIFY(parsed.ok());
        f.w.setDisplayModel(std::move(parsed).value());
        QCOMPARE(f.w.compareRowCount(), 2);
        QCOMPARE(f.w.differenceCount(), std::size_t{2});
        const std::string script = f.w.doScript();
        QVERIFY(script.find("-compare") != std::string::npos);
        f.w.clearWave();
        const ImportResult imp = f.w.loadDoScript(script);
        QVERIFY(imp.errors.empty());
        QCOMPARE(f.w.compareRowCount(), 2);

        // closing the dataset leaves the rows, without data
        QVERIFY(f.w.removeDataset("ref"));
        const int ref = f.row("ref:top.en");
        QVERIFY(ref >= 0);
        QVERIFY(!f.w.rowHasSource(ref));
        QCOMPARE(f.w.rowError(ref), QString("no dataset named ref"));
        QVERIFY(!f.w.rowHasSource(f.row("diff top.en")));
    }

    void rowsEndWithTheirWaveform() {
        // A dataset that runs longer than the current waveform: each row holds its last value only up to the
        // end of its own data; an expression up to the end of the shortest of its inputs.
        Fixture f;
        auto longer = makeStore();
        longer->extendTimeRange(400);
        QVERIFY(f.w.addDataset("ref", longer).isEmpty());
        QVERIFY(f.w.addExpression("both", "en and ref:top.en"));
        auto req = parseAddWave(parseTclList("ref:/top/en"));
        QVERIFY(req.ok());
        QCOMPARE(f.w.addWaveSignals(req.value()), std::size_t{1});
        QCOMPARE(f.valueAt("top.en", 300), std::string("-"));
        QCOMPARE(f.valueAt("ref:top.en", 300), std::string("0"));
        QCOMPARE(f.valueAt("both", 300), std::string("-"));
        QCOMPARE(f.valueAt("both", 100), std::string("1"));
        f.w.zoomFull(); // the view covers the longer waveform
        QVERIFY(f.w.view().endTime() >= 400.0);
    }

    void compareTolerance() {
        Fixture f;
        QVERIFY(f.w.addDataset("ref", makeStore(true)).isEmpty());
        f.w.selectRow(f.row("top.en"));
        QCOMPARE(f.w.compareSelectionWith("ref", 2), std::size_t{1});
        QCOMPARE(f.w.differenceCount(), std::size_t{0}); // the edge moved by 2: within the tolerance
    }

    void compareWaveformsDialog() {
        Fixture f;
        QVERIFY(f.w.addDataset("ref", makeStore(true)).isEmpty());
        f.w.setCompareDialogRunner([](QDialog& d) {
            auto* dlg = qobject_cast<CompareDialog*>(&d);
            if (dlg == nullptr || dlg->reference() != "ref" || !dlg->test().isEmpty() || !dlg->isValid())
                return false;
            dlg->toleranceEdit()->setText("soon");
            if (dlg->isValid())
                return false;
            dlg->toleranceEdit()->setText("0");
            dlg->scopeEdit()->setText("/top");
            return dlg->scope() == "top" && dlg->isValid();
        });
        QSignalSpy msg(&f.w, &WaveWidget::navigationMessage);
        QVERIFY(f.w.showCompareDialog());
        QVERIFY(f.row("Compare ref vs current") >= 0);
        QCOMPARE(f.w.compareRowCount(), 2);
        QVERIFY(msg.last().at(0).toString().contains("3 signal(s) compared, 2 differ"));

        const StoreDiff none = f.w.compareDatasets("ref", "ref", 0);
        QVERIFY(none.differing.empty());
        QCOMPARE(f.w.lastEditError(), QString("Choose two different waveforms"));
    }

    void addWaveCommands() {
        Fixture f;
        QVERIFY(f.w.addDataset("ref", makeStore(true)).isEmpty());
        auto req = parseAddWave(parseTclList("-expr {clk xor ref:/top/clk} -label same"));
        QVERIFY(req.ok());
        QCOMPARE(f.w.addWaveSignals(req.value()), std::size_t{1});
        QVERIFY(f.w.rowHasSource(f.row("same")));
        req = parseAddWave(parseTclList("ref:/top/*"));
        QVERIFY(req.ok());
        QCOMPARE(f.w.addWaveSignals(req.value()), std::size_t{3});
        QVERIFY(f.w.rowHasSource(f.row("ref:top.cnt[7:0]")));
        req = parseAddWave(parseTclList("-compare ref:/top/* /top/c*"));
        QVERIFY(req.ok());
        QCOMPARE(f.w.addWaveSignals(req.value()), std::size_t{0});
        QCOMPARE(f.w.lastEditError(), QString("the reference matches 3 signals, the test 2"));
    }

    void listColumns() {
        Fixture f;
        QVERIFY(f.w.addDataset("ref", makeStore(true)).isEmpty());
        ListModel m;
        m.setStore(f.store);
        m.setSpace(&f.w.space());
        QCOMPARE(m.addSignals({"top.en", "ref:top.en", "=en xor ref:top.en"}), 3);
        // rows at every change of any column: 0, 40, 42, 120
        QCOMPARE(m.rowCount(), 4);
        QCOMPARE(m.cellText(1, 1), QString("1"));
        QCOMPARE(m.cellText(1, 2), QString("0"));
        QCOMPARE(m.cellText(1, 3), QString("1"));
        QCOMPARE(m.cellText(2, 3), QString("0"));
        QCOMPARE(m.headerText(2), QString("ref:en"));
        QCOMPARE(m.headerText(3), QString("en xor ref:top.en"));
        // a growing run: the expression column follows
        f.store->mutableSignal(f.store->findSignal("top.en")).appendLogic(150, "1");
        f.store->extendTimeRange(220);
        m.refresh();
        QCOMPARE(m.rowCount(), 5);
        QCOMPARE(m.cellText(4, 3), QString("1"));
    }

    void expressionRowDrawsLikeASignal() {
        // The renderer cannot tell an expression result from a recorded signal with the same changes.
        Fixture f;
        QVERIFY(f.w.addExpression("copy", "en"));
        const Signal* expr = f.w.rowSignal(f.row("copy"));
        const Signal& en = f.store->signal(f.store->findSignal("top.en"));
        SignalSource a(*expr, nullptr, false), b(en, nullptr, false);
        PainterRenderer r;
        for (long long tile = 0; tile < 2; ++tile) {
            TileRequest req;
            req.timePerPixel = 0.5;
            req.tileIndex = tile;
            req.widthPx = IRenderer::kTileWidth;
            req.heightPx = 22;
            req.validEnd = 200;
            req.source = &a;
            const QImage ia = r.renderTile(req);
            req.source = &b;
            const QImage ib = r.renderTile(req);
            QCOMPARE(ia, ib);
        }
    }
};

QTEST_MAIN(TestWaveDerived)
#include "test_wave_derived.moc"
