// SPDX-License-Identifier: GPL-3.0-or-later
// The List window: lazy model over a large store, live growth, row modes, columns, copy and CSV export, drag
// and drop from the Objects pane, cursor sync, and `add list` through a real simulation.
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"
#include "wave/list_window.hpp"
#include "wave/signal_mime.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

#include <environment.hpp>
#include <fstream>
#include <process.hpp>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> smallStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const ScopeId dut = st->addScope(top, "dut");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    const SignalId cnt = st->addSignal(dut, "cnt", SignalKind::Logic, 8);
    const SignalId state = st->addSignal(dut, "state", SignalKind::String, 0);
    for (Time t = 0; t <= 50000000; t += 10000000) // 0..50 ns every 10 ns (fs timescale)
        st->mutableSignal(clk).appendLogic(t, (t / 10000000) % 2 ? "1" : "0");
    st->mutableSignal(cnt).appendLogic(0, "00000000");
    st->mutableSignal(cnt).appendLogic(15000000, "00001111");
    st->mutableSignal(cnt).appendLogic(15000000, "11111111");   // delta: the row shows FF
    st->mutableSignal(state).appendText(2500, "idle, waiting"); // 2.5 ps: a comma for the CSV quoting
    st->extendTimeRange(0);
    st->extendTimeRange(50000000);
    return st;
}

QString envOr(const char* name, const QString& fallback) {
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : QString::fromLocal8Bit(v);
}

struct Design {
    QTemporaryDir dir;
    QString exe, libDir, dllDir;
    bool found = false;
    QString previousCwd = QDir::currentPath();
    Design() {
        const QString build = QStringLiteral(LVD_KERNEL_BUILD);
#ifdef Q_OS_WIN
        exe = envOr("LVDSIM_NVC", build + "/bin/nvc.exe");
        dllDir =
            envOr("LVDSIM_NVC_DLLDIR", QDir("C:/msys64/mingw64/bin").exists() ? "C:/msys64/mingw64/bin" : "");
#else
        exe = envOr("LVDSIM_NVC", build + "/bin/nvc");
        dllDir = envOr("LVDSIM_NVC_DLLDIR", "");
#endif
        libDir = envOr("LVDSIM_NVC_LIBDIR", build + "/lib");
        found = QFileInfo::exists(exe);
    }
    bool prepare() {
        if (!dir.isValid())
            return false;
        QDir::setCurrent(dir.path());
        std::ofstream("t.vhd") << R"(
entity t is end;
architecture a of t is
  signal clk : bit := '0';
  signal cnt : integer := 0;
begin
  clk <= not clk after 5 ns;
  process (clk) begin if clk = '1' then cnt <= cnt + 1; end if; end process;
end;
)";
        QDir().mkpath("work");
        std::ofstream("work/_NVC_LIB") << "lvdsim\n";
        lvd::tools::ProcessOptions po;
        po.capture = true;
        if (!dllDir.isEmpty())
            po.pathPrepend.push_back(dllDir.toStdString());
        const auto r = lvd::tools::runProcess(
            {exe.toStdString(), "-L", libDir.toStdString(), "--std=08", "--work=work:work", "-a", "t.vhd"},
            po);
        return r.started && r.exitCode == 0;
    }
    ~Design() { QDir::setCurrent(previousCwd); }
    lvd::tools::Environment environment() const {
        lvd::tools::Environment e = lvd::tools::discoverEnvironment();
        e.nvc = exe.toStdString();
        e.nvcDllDir = dllDir.toStdString();
        e.ctx.nvcGlobalArgs = {"-L", libDir.toStdString()};
        return e;
    }
};

AddWaveRequest req(std::initializer_list<const char*> patterns) {
    AddWaveRequest r;
    for (const char* p : patterns)
        r.patterns.emplace_back(p);
    return r;
}

} // namespace

class TestListWindow : public QObject {
    Q_OBJECT
private slots:
    void rowsColumnsAndValues() {
        ListWindow lw;
        lw.setStore(smallStore());
        auto r = req({"/top/clk", "/top/dut/cnt"});
        r.radix = Radix::Hex;
        QCOMPARE(lw.addList(r), 2);
        ListModel* m = lw.model();
        QCOMPARE(m->columnCount(), 3);
        QCOMPARE(m->headerText(0), QString("Time (ns)"));
        QCOMPARE(m->headerText(1), QString("clk"));
        QCOMPARE(m->headerText(2), QString("dut/cnt"));
        // rows: 0 10 15 20 30 40 50 ns
        QCOMPARE(m->rowCount(), 7);
        QCOMPARE(m->cellText(2, 0), QString("15"));
        QCOMPARE(m->cellText(2, 2), QString("FF")); // last delta wins
        QCOMPARE(m->cellText(2, 1), QString("1"));
        QCOMPARE(m->cellText(1, 2), QString("00"));
        // bold where the signal changes at the row's time
        QVERIFY(m->data(m->index(2, 2), Qt::FontRole).value<QFont>().bold());
        QVERIFY(!m->data(m->index(2, 1), Qt::FontRole).isValid());
        // radix and time unit
        m->setRadix(1, Radix::Unsigned);
        QCOMPARE(m->cellText(2, 2), QString("255"));
        m->setTimeUnitExp(-12);
        QCOMPARE(m->cellText(2, 0), QString("15000"));
        QCOMPARE(m->timeValue(2500), QString("2.5")); // 2500 fs in ps
        m->setTimeUnitExp(-6);
        QCOMPARE(m->cellText(2, 0), QString("0.015"));
        QCOMPARE(lw.findChild<QLabel*>("rowCountLabel")->text(), QString("7 rows"));
        // a value before the signal's first change
        QCOMPARE(lw.addList(req({"/top/dut/state"})), 1);
        QCOMPARE(m->rowCount(), 8); // 2.5 ps is a new row
        QCOMPARE(m->cellText(0, 3), QString("-"));
        QCOMPARE(m->cellText(1, 3), QString("idle, waiting"));
        // patterns that match nothing add nothing
        QCOMPARE(lw.addList(req({"/top/nothing"})), 0);
    }

    void headerMenuMovesRemovesAndSetsTheRadix() {
        ListWindow lw;
        lw.setStore(smallStore());
        lw.addList(req({"/top/clk", "/top/dut/cnt", "/top/dut/state"}));
        ListModel* m = lw.model();
        std::unique_ptr<QMenu> menu(lw.createHeaderMenu(1));
        QVERIFY(!menu->findChild<QAction*>("moveLeft")->isEnabled());
        menu->findChild<QAction*>("moveRight")->trigger();
        QCOMPARE(m->headerText(1), QString("dut/cnt"));
        QCOMPARE(m->headerText(2), QString("clk"));
        menu.reset(lw.createHeaderMenu(1));
        menu->findChild<QAction*>("radix_binary")->trigger();
        QCOMPARE(m->signalColumns()[0].radix, Radix::Binary);
        QCOMPARE(m->cellText(2, 1).size(), qsizetype(8));
        menu.reset(lw.createHeaderMenu(3));
        menu->findChild<QAction*>("removeColumn")->trigger();
        QCOMPARE(m->columnCount(), 3);
        // Delete removes the current cell's column
        lw.table()->setCurrentIndex(m->index(0, 2));
        QTest::keyClick(lw.table(), Qt::Key_Delete);
        QCOMPARE(m->columnCount(), 2);
        QCOMPARE(m->headerText(1), QString("dut/cnt"));
        // the time column has the unit menu only
        menu.reset(lw.createHeaderMenu(0));
        QVERIFY(menu->findChild<QAction*>("unit_ps") != nullptr);
        QVERIFY(menu->findChild<QAction*>("removeColumn") == nullptr);
        // dragging a header reorders the model, the header stays in natural order
        lw.addList(req({"/top/clk"}));
        lw.table()->horizontalHeader()->moveSection(2, 1);
        QCOMPARE(m->headerText(1), QString("clk"));
        QCOMPARE(lw.table()->horizontalHeader()->visualIndex(1), 1);
    }

    void rowModes() {
        ListWindow lw;
        lw.setStore(smallStore());
        lw.addList(req({"/top/clk", "/top/dut/cnt"}));
        ListModel* m = lw.model();
        std::unique_ptr<QMenu> menu(lw.createHeaderMenu(2));
        menu->findChild<QAction*>("useAsTrigger")->trigger(); // rows on changes of cnt: 0 and 15 ns
        QCOMPARE(m->rowCount(), 2);
        auto* mode = lw.findChild<QComboBox*>("rowModeBox");
        QCOMPARE(mode->currentData().toInt(), static_cast<int>(ListTrigger::Signal));
        QVERIFY(!lw.findChild<QComboBox*>("triggerBox")->isHidden());
        mode->setCurrentIndex(mode->findData(static_cast<int>(ListTrigger::Strobe)));
        auto* every = lw.findChild<QLineEdit*>("strobeEdit");
        every->setText("20 ns");
        emit every->editingFinished();
        QCOMPARE(m->rowCount(), 3); // 0 20 40
        QCOMPARE(m->cellText(1, 0), QString("20"));
        QCOMPARE(m->cellText(1, 2), QString("FF"));
        auto* from = lw.findChild<QLineEdit*>("strobeStartEdit");
        from->setText("5");
        emit from->editingFinished();
        QCOMPARE(m->cellText(0, 0), QString("5"));
        every->setText("bad");
        emit every->editingFinished(); // refused, marked, nothing changes
        QCOMPARE(m->rowCount(), 3);
        QVERIFY(!every->styleSheet().isEmpty());
        mode->setCurrentIndex(mode->findData(static_cast<int>(ListTrigger::AnyChange)));
        QCOMPARE(m->rowCount(), 7);
    }

    void copyAndExportCsv() {
        ListWindow lw;
        lw.setStore(smallStore());
        lw.addList(req({"/top/clk", "/top/dut/state"}));
        lw.table()->selectRow(1);
        lw.table()->selectionModel()->select(lw.model()->index(2, 0),
                                             QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const QString text = lw.copySelection();
        QCOMPARE(text,
                 QString("Time (ns)\tclk\tdut/state\n0.0025\t0\tidle, waiting\n10\t1\tidle, waiting\n"));
        QCOMPARE(QGuiApplication::clipboard()->text(), text);

        QTemporaryDir dir;
        const QString path = dir.filePath("list.csv");
        lw.setFilePrompt([&] { return path; });
        QVERIFY(lw.exportCsvDialog());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split("\r\n");
        QCOMPARE(lines[0], QString("Time (ns),/top/clk,/top/dut/state"));
        QCOMPARE(lines[1], QString("0,0,-"));
        QCOMPARE(lines[2], QString("0.0025,0,\"idle, waiting\""));
        QCOMPARE(lines.size(), 7 + 1 + 1); // header, 7 rows, empty after the last CRLF
        lw.setFilePrompt([] { return QString(); });
        QVERIFY(!lw.exportCsvDialog()); // cancelled
    }

    void dropFromObjectsAddsColumns() {
        ListWindow lw;
        lw.resize(600, 300);
        lw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&lw));
        lw.setStore(smallStore());
        QSignalSpy added(&lw, &ListWindow::signalsAdded);
        std::unique_ptr<QMimeData> mime(makeSignalMime({"/top/clk", "/top/dut/cnt"}));
        QWidget* vp = lw.table()->viewport();
        const QPoint at(20, 20);
        QDragEnterEvent enter(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(at), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &drop);
        QCOMPARE(lw.model()->columnCount(), 3);
        QCOMPARE(added.count(), 1);
        // foreign data is ignored
        QMimeData text;
        text.setText("hello");
        QDragEnterEvent other(at, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
        other.setAccepted(false);
        QApplication::sendEvent(vp, &other);
        QVERIFY(!other.isAccepted());
    }

    void cursorSyncBothWays() {
        ListWindow lw;
        lw.setStore(smallStore());
        lw.addList(req({"/top/clk", "/top/dut/cnt"}));
        QSignalSpy sel(&lw, &ListWindow::timeSelected);
        lw.table()->selectRow(3); // 20 ns
        QCOMPARE(sel.count(), 1);
        QCOMPARE(sel.last().at(0).toDouble(), 20000000.0);
        lw.showTime(17000000.0); // between rows: the row at or before, no signal back
        QCOMPARE(lw.table()->currentIndex().row(), 2);
        QCOMPARE(sel.count(), 1);

        // With a wave window as the simulation window wires them.
        WaveWidget wave;
        wave.setStore(smallStore());
        connect(&lw, &ListWindow::timeSelected, &wave, &WaveWidget::moveActiveCursorTo);
        connect(&wave, &WaveWidget::cursorsChanged, &lw, [&] {
            if (const auto t = wave.cursorTime(wave.activeCursor()))
                lw.showTime(*t);
        });
        lw.table()->selectRow(5);
        QCOMPARE(wave.cursorTime(0).value_or(-1), 40000000.0);
        wave.setCursorTime(0, 12000000.0);
        QCOMPARE(lw.table()->currentIndex().row(), 1);
    }

    void largeStoreIsLazy() {
        // One million rows: building the index and showing any part of it must not touch every row.
        auto st = std::make_shared<WaveStore>();
        const ScopeId top = st->addScope(kRootScope, "top");
        const SignalId a = st->addSignal(top, "a", SignalKind::Logic, 16);
        const SignalId b = st->addSignal(top, "b", SignalKind::Logic, 1);
        Signal& sa = st->mutableSignal(a);
        Signal& sb = st->mutableSignal(b);
        std::string bits(16, '0');
        constexpr int kRows = 1000000;
        for (int i = 0; i < kRows; ++i) {
            for (int k = 0; k < 16; ++k)
                bits[15 - static_cast<std::size_t>(k)] = ((i >> k) & 1) ? '1' : '0';
            sa.appendLogic(static_cast<Time>(i) * 1000, bits);
            if (i % 2 == 0)
                sb.appendLogic(static_cast<Time>(i) * 1000, (i / 2) % 2 ? "1" : "0");
        }
        st->extendTimeRange(0);
        st->extendTimeRange(static_cast<Time>(kRows - 1) * 1000);

        ListWindow lw;
        lw.resize(700, 500);
        lw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&lw));
        lw.setStore(st);
        QElapsedTimer timer;
        timer.start();
        auto r = req({"/top/a", "/top/b"});
        r.radix = Radix::Unsigned;
        lw.addList(r);
        const qint64 indexMs = timer.elapsed();
        QCOMPARE(lw.model()->rowCount(), kRows);
        timer.restart();
        lw.table()->scrollToBottom();
        lw.table()->viewport()->repaint();
        lw.table()->scrollTo(lw.model()->index(kRows / 2, 0));
        lw.table()->viewport()->repaint();
        const qint64 paintMs = timer.elapsed();
        QCOMPARE(lw.model()->cellText(kRows - 1, 1), QString::number((kRows - 1) & 0xFFFF));
        QCOMPARE(lw.model()->cellText(123457, 0), QString("123.457")); // 123457 ps in ns
        QCOMPARE(lw.model()->rowAtOrBefore(500000500), 500000);
        qInfo("1M rows: index %lld ms, two scrolls and paints %lld ms", indexMs, paintMs);
        QVERIFY2(indexMs < 5000, "indexing a million rows is too slow");
        QVERIFY2(paintMs < 2000, "showing a page of a million rows is too slow");
    }

    void liveGrowthAppendsRowsAndFollowsTheEnd() {
        auto st = smallStore();
        ListWindow lw;
        lw.resize(500, 200);
        lw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&lw));
        lw.setStore(st);
        lw.addList(req({"/top/clk"}));
        QCOMPARE(lw.model()->rowCount(), 6);
        QSignalSpy inserted(lw.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(lw.model(), &QAbstractItemModel::modelReset);
        Signal& clk = st->mutableSignal(st->findSignal("top.clk"));
        lw.table()->scrollToBottom();
        for (int round = 0; round < 20; ++round) {
            for (int k = 0; k < 10; ++k) {
                const Time t = 60000000 + static_cast<Time>(round * 10 + k) * 10000000;
                clk.appendLogic(t, (round * 10 + k) % 2 ? "0" : "1");
                st->extendTimeRange(t);
            }
            lw.refresh();
            QApplication::processEvents();
        }
        QCOMPARE(lw.model()->rowCount(), 206);
        QCOMPARE(inserted.count(), 20);
        QCOMPARE(reset.count(), 0); // growth never resets the view
        QScrollBar* bar = lw.table()->verticalScrollBar();
        QCOMPARE(bar->value(), bar->maximum()); // stayed at the end
        QCOMPARE(lw.findChild<QLabel*>("rowCountLabel")->text(), QString("206 rows"));
        // not at the end: the view stays where it is
        bar->setValue(0);
        clk.appendLogic(3000000000, "1");
        lw.refresh();
        QCOMPARE(bar->value(), 0);
    }

    void addListCommandInASimulation() {
        Design d;
        if (!d.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY2(d.prepare(), "could not analyse the test design");
        QTemporaryDir settings;
        SimWindow w(d.environment(), settings.filePath("list.ini"));
        w.setDialogsEnabled(false);
        w.show();
        QVERIFY(w.findChild<QDockWidget*>("listDock")->isHidden()); // on demand only
        QVERIFY(w.startSimulation({"-do", "add list -radix unsigned /cnt /clk; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        ListWindow* lw = w.list();
        QVERIFY(lw != nullptr);
        QCOMPARE(lw->model()->columnCount(), 3);
        auto* dock = w.findChild<QDockWidget*>("listDock");
        QVERIFY(dock != nullptr);
        QVERIFY(!dock->isHidden()); // shown (and raised) by the first add list
        QVERIFY(w.tabifiedDockWidgets(w.findChild<QDockWidget*>("sourceDock"))
                    .contains(dock)); // a tab next to Source
        // a row for every 5 ns step (clk toggles), cnt in decimal
        QCOMPARE(lw->model()->rowCount(), 21);
        const int last = lw->model()->rowCount() - 1;
        QCOMPARE(lw->model()->cellText(last, 0), QString("100"));
        QCOMPARE(lw->model()->cellText(last, 1), QString("10"));

        // a later run grows the list
        w.sendCommand("run 50 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(lw->model()->rowCount(), 31);

        // selecting a row moves the wave cursor
        lw->table()->selectRow(4);
        QCOMPARE(w.wave()->cursorTime(w.wave()->activeCursor()).value_or(-1), 20000000.0);

        w.sendCommand("add list /nothing");
        QVERIFY(w.waitUntilIdle());
        QTRY_VERIFY(w.transcript()->text().contains("add list: no signal matches"));
        w.sendCommand("delete list *");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(lw->model()->columnCount(), 1);
        w.stopSimulation();
    }
};

QTEST_MAIN(TestListWindow)
#include "test_list_window.moc"
