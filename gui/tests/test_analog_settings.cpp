// SPDX-License-Identifier: GPL-3.0-or-later
// The "Analog Settings" dialog, the range it sets (auto / manual) and how the range is drawn.
#include "rhi/gpu_wave_renderer.hpp"
#include "wave/analog_settings_dialog.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId volt = st->addSignal(top, "volt", SignalKind::Real, 64);
    for (Time t = 0; t <= 1000; t += 50) {
        st->mutableSignal(clk).appendLogic(t, (t / 50) % 2 ? "1" : "0");
        st->mutableSignal(volt).appendReal(t, static_cast<double>(t) / 100.0); // 0 .. 10
    }
    st->extendTimeRange(0);
    st->extendTimeRange(1000);
    return st;
}

struct Fixture {
    std::unique_ptr<WaveWidget> w = std::make_unique<WaveWidget>();
    Fixture() {
        w->resize(900, 320);
        DisplayModel m;
        m.root().children.push_back(DisplayModel::makeSignal("top.clk"));
        DisplayItem volt = DisplayModel::makeSignal("top.volt");
        volt.analog.enabled = true;
        m.root().children.push_back(volt);
        w->setDisplayModel(m);
        w->setStore(makeStore());
        w->setView(0.0, 1.5);
    }
    WaveWidget* operator->() { return w.get(); }
    const AnalogSettings& analog() const {
        for (const DisplayItem& it : w->displayModel().root().children)
            if (it.path == "top.volt")
                return it.analog;
        static AnalogSettings none;
        return none;
    }
    /// Drives the dialog the way a user would: `fill` edits it, then OK is pressed (or Cancel when it returns
    /// false).
    void driveDialog(std::function<bool(AnalogSettingsDialog&)> fill) {
        w->setAnalogDialogRunner([fill](QDialog& d) {
            auto& dlg = static_cast<AnalogSettingsDialog&>(d);
            dlg.open();
            const bool ok = fill(dlg);
            if (ok && dlg.okButton()->isEnabled())
                dlg.okButton()->click();
            else
                dlg.reject();
            return dlg.result() == QDialog::Accepted;
        });
    }
};

QImage grabWaveArea(WaveWidget& w) {
    const QImage c = w.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return c.copy(w.waveLeft(), w.rowsTop(), w.waveWidth(), w.rowsBottom() - w.rowsTop());
}

} // namespace

class TestAnalogSettings : public QObject {
    Q_OBJECT
private slots:
    // ---- parsing and the dialog itself ----
    void parsesPlainAndScientificNumbers() {
        QCOMPARE(parseRangeLimit("-1.5").value(), -1.5);
        QCOMPARE(parseRangeLimit("  2e-3 ").value(), 0.002);
        QCOMPARE(parseRangeLimit("1E6").value(), 1e6);
        QCOMPARE(parseRangeLimit("-2.5E+2").value(), -250.0);
        QCOMPARE(parseRangeLimit(".5").value(), 0.5);
        QVERIFY(!parseRangeLimit("").has_value());
        QVERIFY(!parseRangeLimit("abc").has_value());
        QVERIFY(!parseRangeLimit("1,5").has_value());
        QVERIFY(!parseRangeLimit("1e").has_value());
        QVERIFY(!parseRangeLimit("1e999").has_value()); // overflows to infinity
        QVERIFY(!parseRangeLimit("nan").has_value());
        QVERIFY(!parseRangeLimit("inf").has_value());
    }

    void autoIsTheDefaultAndDisablesTheFields() {
        AnalogSettingsDialog d(AnalogSettingsInput{});
        QVERIFY(d.isAuto());
        QVERIFY(d.findChild<QRadioButton*>("autoRadio")->isChecked());
        QVERIFY(!d.findChild<QLineEdit*>("minEdit")->isEnabled());
        QVERIFY(!d.findChild<QLineEdit*>("maxEdit")->isEnabled());
        QVERIFY(d.okButton()->isEnabled());
        QVERIFY(d.isValid());
        QVERIFY(d.choice().autoRange);
        QVERIFY(d.isModal());
    }

    void manualEnablesAndPreloadsTheEffectiveRange() {
        AnalogSettingsInput in;
        in.min = -1.5;
        in.max = 2.5e3;
        AnalogSettingsDialog d(in);
        d.setAuto(false);
        QVERIFY(d.findChild<QLineEdit*>("minEdit")->isEnabled());
        QVERIFY(d.findChild<QLineEdit*>("maxEdit")->isEnabled());
        QCOMPARE(d.minText(), QString("-1.5"));
        QCOMPARE(d.maxText(), QString("2500"));
        QVERIFY(d.isValid());
        const AnalogSettingsChoice c = d.choice();
        QVERIFY(!c.autoRange);
        QCOMPARE(c.min, -1.5);
        QCOMPARE(c.max, 2500.0);
        // a row that is already manual starts in manual mode
        in.autoRange = false;
        AnalogSettingsDialog m(in);
        QVERIFY(!m.isAuto());
        QVERIFY(m.findChild<QRadioButton*>("manualRadio")->isChecked());
    }

    void validationRefusesBadRanges() {
        AnalogSettingsInput in;
        in.autoRange = false;
        AnalogSettingsDialog d(in);
        auto expectBad = [&](const QString& lo, const QString& hi, const QString& message) {
            d.setMinText(lo);
            d.setMaxText(hi);
            QVERIFY2(!d.isValid(), qPrintable(lo + " / " + hi));
            QVERIFY2(d.error().contains(message), qPrintable(d.error()));
            QCOMPARE(d.findChild<QLabel*>("errorLabel")->text(), d.error());
            QVERIFY(!d.okButton()->isEnabled());
        };
        expectBad("5", "5", "Min must be less than Max");
        expectBad("6", "5", "Min must be less than Max");
        expectBad("", "5", "Min is not a number");
        expectBad("1", "x", "Max is not a number");
        expectBad("1", "1e999", "Max is not a number");
        d.setMinText("-1e-3");
        d.setMaxText("3.3E0");
        QVERIFY(d.isValid());
        QVERIFY(d.error().isEmpty());
        QVERIFY(d.okButton()->isEnabled());
        QCOMPARE(d.choice().min, -0.001);
        QCOMPARE(d.choice().max, 3.3);
        // automatic mode ignores whatever is in the fields
        d.setMinText("junk");
        d.setAuto(true);
        QVERIFY(d.isValid());
        QVERIFY(d.okButton()->isEnabled());
    }

    void okIsRefusedWhileInvalid() {
        AnalogSettingsInput in;
        in.autoRange = false;
        AnalogSettingsDialog d(in);
        d.open();
        d.setMinText("9");
        d.setMaxText("1");
        QVERIFY(!d.okButton()->isEnabled());
        d.findChild<QDialogButtonBox*>("buttonBox")
            ->accepted(); // even a stray accept signal does not close it
        QVERIFY(d.isVisible());
        d.reject();
    }

    // ---- applying to the wave window ----
    void thePreloadIsTheEffectiveRangeOfTheRow() {
        Fixture f;
        f->selectRow(1);
        QString minText, maxText;
        bool wasAuto = false;
        f.driveDialog([&](AnalogSettingsDialog& d) {
            wasAuto = d.isAuto();
            d.setAuto(false); // switching to manual shows what the row is drawn with now: 0 .. 10
            minText = d.minText();
            maxText = d.maxText();
            return false; // cancel
        });
        QVERIFY(!f->showAnalogSettings());
        QVERIFY(wasAuto);
        QCOMPARE(minText, QString("0"));
        QCOMPARE(maxText, QString("10"));
        QVERIFY(f.analog().autoRange); // cancel changed nothing
        QVERIFY(!f->canUndo());
    }

    void manualRangeIsAppliedAndUndone() {
        Fixture f;
        f->selectRow(1);
        QSignalSpy structure(f.w.get(), &WaveWidget::structureChanged);
        f.driveDialog([](AnalogSettingsDialog& d) {
            d.setAuto(false);
            d.setMinText("2");
            d.setMaxText("5e0");
            return true;
        });
        QVERIFY(f->showAnalogSettings());
        QVERIFY(!f.analog().autoRange);
        QCOMPARE(f.analog().min, 2.0);
        QCOMPARE(f.analog().max, 5.0);
        QCOMPARE(structure.count(), 1);
        const auto eff = f->effectiveAnalogRange(1);
        QVERIFY(eff.has_value());
        QCOMPARE(eff->min, 2.0);
        QCOMPARE(eff->max, 5.0);
        QVERIFY(f->canUndo());
        QVERIFY(f->undoText().contains("range"));

        QVERIFY(f->undo());
        QVERIFY(f.analog().autoRange);
        QCOMPARE(f->effectiveAnalogRange(1)->max, 10.0);
        QVERIFY(f->redo());
        QVERIFY(!f.analog().autoRange);
        QCOMPARE(f.analog().min, 2.0);

        // reopening a manual row starts in manual mode with its limits; back to auto is one more undo step
        QString seenMin;
        bool seenAuto = true;
        f.driveDialog([&](AnalogSettingsDialog& d) {
            seenAuto = d.isAuto();
            seenMin = d.minText();
            d.setAuto(true);
            return true;
        });
        QVERIFY(f->showAnalogSettings());
        QVERIFY(!seenAuto);
        QCOMPARE(seenMin, QString("2"));
        QVERIFY(f.analog().autoRange);
        QVERIFY(f->undo());
        QVERIFY(!f.analog().autoRange);
    }

    void applyingTheSameThingChangesNothing() {
        Fixture f;
        f->selectRow(1);
        QVERIFY(f->setAnalogRangeForSelection(false, 1.0, 2.0));
        QVERIFY(!f->setAnalogRangeForSelection(false, 1.0, 2.0));
        QVERIFY(f->lastEditError().isEmpty());
        QVERIFY(f->setAnalogRangeForSelection(true));
        QVERIFY(!f->setAnalogRangeForSelection(true));
    }

    void invalidRangesAreRefusedByTheModelToo() {
        Fixture f;
        f->selectRow(1);
        QSignalSpy rejected(f.w.get(), &WaveWidget::editRejected);
        QVERIFY(!f->setAnalogRangeForSelection(false, 5.0, 5.0));
        QVERIFY(!f->setAnalogRangeForSelection(false, 5.0, 1.0));
        QVERIFY(!f->setAnalogRangeForSelection(false, std::nan(""), 1.0));
        QCOMPARE(rejected.count(), 3);
        QVERIFY(f.analog().autoRange);
        QVERIFY(!f->canUndo());
        // an accepted dialog that is invalid (a runner that ignores OK being disabled) applies nothing either
        f->setAnalogDialogRunner([](QDialog& d) {
            auto& dlg = static_cast<AnalogSettingsDialog&>(d);
            dlg.setAuto(false);
            dlg.setMinText("3");
            dlg.setMaxText("3");
            return true;
        });
        QVERIFY(!f->showAnalogSettings());
        QVERIFY(f.analog().autoRange);
    }

    void appliesToEverySelectedAnalogRow() {
        Fixture f;
        // a second analog row and a plain one in the selection
        f->selectRow(1);
        f->selectRow(0, true);
        QVERIFY(f->analogSelectionAvailable());
        QVERIFY(f->setAnalogRangeForSelection(false, -1.0, 12.0));
        QCOMPARE(f.analog().min, -1.0);
        for (const DisplayItem& it : f->displayModel().root().children)
            if (it.path == "top.clk")
                QVERIFY(it.analog.autoRange && !it.analog.enabled); // the plain row is untouched
        f->clearSelection();
        QVERIFY(!f->analogSelectionAvailable());
        QVERIFY(!f->setAnalogRangeForSelection(true));
        QVERIFY(!f->showAnalogSettings());
        f->selectRow(0);
        QVERIFY(!f->analogSelectionAvailable());
    }

    void contextMenuAndWaveMenuHaveTheEntry() {
        Fixture f;
        {
            std::unique_ptr<QMenu> menu(f->createContextMenu(1));
            const auto actions = menu->findChildren<QAction*>("analogSettings");
            QCOMPARE(actions.size(), 1); // once, not once more from the arranging block
            QVERIFY(actions[0]->isEnabled());
            QCOMPARE(actions[0]->text(), QString("Analog Settings..."));
            bool opened = false;
            f->setAnalogDialogRunner([&](QDialog&) {
                opened = true;
                return false;
            });
            actions[0]->trigger();
            QVERIFY(opened);
        }
        {
            std::unique_ptr<QMenu> menu(f->createContextMenu(0)); // a plain signal row
            const auto actions = menu->findChildren<QAction*>("analogSettings");
            QCOMPARE(actions.size(), 1);
            QVERIFY(!actions[0]->isEnabled());
        }
        {
            f->selectRow(1);
            QMenu waveMenu;
            f->populateArrangeMenu(&waveMenu);
            QAction* a = waveMenu.findChild<QAction*>("analogSettings");
            QVERIFY(a != nullptr);
            QVERIFY(a->isEnabled());
            QMenu without;
            f->populateArrangeMenu(&without, false);
            QVERIFY(without.findChild<QAction*>("analogSettings") == nullptr);
        }
    }

    // ---- cache key and redraw ----
    void theRangeIsPartOfTheTileCacheKey() {
        Fixture f;
        const QSize size(f->waveWidth(), f->rowsBottom() - f->rowsTop());
        auto revisionOfVolt = [&] {
            const GpuFrame frame = f->buildGpuFrame(size);
            std::uint64_t rev = 0;
            for (const GpuTileDraw& t : frame.tiles)
                if (t.heightPx > f->rowHeight())
                    rev = t.key.revision; // the analog row is the tall one
            return rev;
        };
        const std::uint64_t autoRev = revisionOfVolt();
        QVERIFY(autoRev != 0);
        f->selectRow(1);
        QVERIFY(f->setAnalogRangeForSelection(false, 2.0, 5.0));
        const std::uint64_t manualRev = revisionOfVolt();
        QVERIFY(manualRev != autoRev);
        QVERIFY(f->setAnalogRangeForSelection(false, 2.0, 6.0)); // only Max changed
        const std::uint64_t otherRev = revisionOfVolt();
        QVERIFY(otherRev != manualRev && otherRev != autoRev);
        QVERIFY(f->setAnalogRangeForSelection(false, 2.0, 5.0));
        QCOMPARE(revisionOfVolt(), manualRev); // the same range is the same key: tiles are reused
    }

    void theQPainterPathRedrawsWhenTheRangeChanges() {
        Fixture f;
        f->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.w.get()));
        f->clearSelection();
        const QImage before = grabWaveArea(*f.w);
        const std::size_t missesBefore = f->tileCache().misses();
        f->selectRow(1);
        QVERIFY(f->setAnalogRangeForSelection(false, 4.0, 6.0));
        f->clearSelection();
        const QImage after = grabWaveArea(*f.w);
        QVERIFY(after != before);
        QVERIFY(f->tileCache().misses() > missesBefore); // new tiles were rendered, not the old ones reused
        QVERIFY(f->undo());
        f->clearSelection();
        QCOMPARE(grabWaveArea(*f.w), before); // back to auto: the original picture
    }

    // ---- drawing: clipping and a stable auto range ----
    void valuesOutsideAManualRangeAreClippedToTheRowEdges() {
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        Signal& s = st.mutableSignal(id);
        for (Time t = 0; t < 400; t += 10)
            s.appendReal(t, (t / 10) % 4 == 0 ? -50.0
                                              : ((t / 10) % 4 == 1   ? 3.0
                                                 : (t / 10) % 4 == 2 ? 99.0
                                                                     : 5.0));
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        constexpr int h = 88, margin = 3;
        TileRequest r;
        r.source = &src;
        r.timePerPixel = 1.0;
        r.widthPx = IRenderer::kTileWidth;
        r.heightPx = h;
        r.style.autoRange = false;
        r.style.min = 0.0;
        r.style.max = 10.0;
        PainterRenderer pr;
        const QImage img = pr.renderTile(r);
        bool anyAbove = false, anyBelow = false, topEdge = false, bottomEdge = false, middle = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < 400 && x < img.width(); ++x) {
                if (qAlpha(img.pixel(x, y)) == 0)
                    continue;
                anyAbove |= y < margin;
                anyBelow |= y > h - margin - 1;
                topEdge |= y == margin;
                bottomEdge |= y == h - margin - 1;
                middle |= y > margin + 20 && y < h - margin - 20;
            }
        QVERIFY(!anyAbove && !anyBelow); // nothing outside the row's drawing area
        QVERIFY(topEdge);                // 99 sits on the top edge
        QVERIFY(bottomEdge);             // -50 sits on the bottom edge
        QVERIFY(middle);                 // 3 and 5 are inside
        // the value 3.0 is 30 % up the row, the value 5.0 half way
        const int span = h - 2 * margin - 1;
        const int y3 = (h - margin - 1) - static_cast<int>(std::lround(0.3 * span));
        QVERIFY(qAlpha(img.pixel(15, y3)) > 0);
    }

    void autoRangeIsOneRangeForEveryTile() {
        // 0 .. 10 over the whole signal, but the first 100 time units only reach 1: a per-tile range would
        // stretch them
        WaveStore st;
        const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Real, 64);
        Signal& s = st.mutableSignal(id);
        for (Time t = 0; t < 100; t += 10)
            s.appendReal(t, (t / 10) % 2 ? 1.0 : 0.0);
        for (Time t = 100; t < 2000; t += 10)
            s.appendReal(t, (t / 10) % 2 ? 10.0 : 0.0);
        const NumericIndex idx = NumericIndex::build(st.signal(id));
        SignalSource src(st.signal(id), &idx, true);
        constexpr int h = 88, margin = 3;
        PainterRenderer pr;
        auto tile = [&](long long k) {
            TileRequest r;
            r.source = &src;
            r.timePerPixel = 0.25;  // a tile is 64 time units: tile 0 holds only the small values
            r.tileIndex = k;
            r.widthPx = IRenderer::kTileWidth;
            r.heightPx = h;
            return pr.renderTile(r);
        };
        const QImage first = tile(0), later = tile(2);
        auto topmost = [&](const QImage& img) {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 0)
                        return y;
            return -1;
        };
        const int span = h - 2 * margin - 1;
        QCOMPARE(topmost(later), margin); // 10 is the top of the row in the busy tile
        // 1.0 is at 10 % of the height in tile 0 as well: far below the top edge, not stretched to it
        QVERIFY(qAbs(topmost(first) - ((h - margin - 1) - static_cast<int>(std::lround(0.1 * span)))) <= 1);
    }

    // ---- GPU and QPainter agree ----
    void gpuMatchesQPainterForAClippedManualRange() {
        QString err;
        auto gpu = GpuOffscreen::create(&err);
        if (!gpu)
            QSKIP(qPrintable("no headless GPU: " + err));
        Fixture f;
        f->selectRow(1);
        QVERIFY(
            f->setAnalogRangeForSelection(false, 2.0, 5.0)); // the data goes 0 .. 10: both ends are clipped
        f->setView(0.0, 1.5);
        const QSize size(f->waveWidth(), f->rowsBottom() - f->rowsTop());
        gpu->renderer().clearCache();
        const QImage g =
            gpu->render(f->buildGpuFrame(size)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage c = f->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        long differing = 0, total = 0;
        for (int y = 0; y < size.height(); ++y)
            for (int x = 1; x < size.width(); ++x) {
                const QRgb a = g.pixel(x, y), b = c.pixel(f->waveLeft() + x, f->rowsTop() + y);
                ++total;
                if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
                    qAbs(qBlue(a) - qBlue(b)) > 2)
                    ++differing;
            }
        QVERIFY2(differing * 1000 < total,
                 qPrintable(QString("%1 of %2 pixels differ").arg(differing).arg(total)));
        // and the clipped trace really is on the row edges in the GPU image: some analog colour pixel on the
        // top edge
        const int top = f->rowHeight(); // the analog row starts below the 1-row clk row
        bool edge = false;
        for (int x = 1; x < size.width() && !edge; ++x)
            edge = g.pixel(x, top + 3) != g.pixel(x, top + 40);
        QVERIFY(edge);
    }

    // ---- serialisation ----
    void rangeSurvivesDoScriptAndLayout() {
        Fixture f;
        f->selectRow(1);
        QVERIFY(f->setAnalogRangeForSelection(false, -2.5e-3, 7.5));
        const std::string script = f->doScript();
        QVERIFY2(script.find("-min -0.0025") != std::string::npos, script.c_str());
        QVERIFY2(script.find("-max 7.5") != std::string::npos, script.c_str());
        WaveWidget g;
        g.setStore(makeStore());
        QCOMPARE(g.loadDoScript(script).rowsAdded, std::size_t{2});
        QCOMPARE(g.rowCount(), 2);
        const AnalogSettings& ga = g.displayModel().root().children.at(1).analog;
        QVERIFY(ga.enabled && !ga.autoRange);
        QCOMPARE(ga.min, -2.5e-3);
        QCOMPARE(ga.max, 7.5);
        auto parsed = DisplayModel::parse(f->displayModel().serialize());
        QVERIFY(parsed.ok());
        bool found = false;
        for (const DisplayItem& it : parsed.value().root().children)
            if (it.path == "top.volt") {
                found = true;
                QVERIFY(!it.analog.autoRange);
                QCOMPARE(it.analog.min, -2.5e-3);
                QCOMPARE(it.analog.max, 7.5);
            }
        QVERIFY(found);
    }

    // ---- pictures for the docs (written when LVDSIM_TEST_IMAGE_DIR is set) ----
    void dialogPicture() {
        const QByteArray dir = qgetenv("LVDSIM_TEST_IMAGE_DIR");
        if (dir.isEmpty())
            QSKIP("LVDSIM_TEST_IMAGE_DIR not set");
        AnalogSettingsInput in;
        in.autoRange = false;
        in.min = 2.0;
        in.max = 5.0;
        AnalogSettingsDialog d(in);
        d.show();
        QVERIFY(QTest::qWaitForWindowExposed(&d));
        QVERIFY(d.grab().save(QString::fromLocal8Bit(dir) + "/analog_settings_dialog.png"));
        d.setMinText("7");
        QTest::qWait(20);
        QVERIFY(d.grab().save(QString::fromLocal8Bit(dir) + "/analog_settings_dialog_error.png"));
    }
};

QTEST_MAIN(TestAnalogSettings)
#include "test_analog_settings.moc"
