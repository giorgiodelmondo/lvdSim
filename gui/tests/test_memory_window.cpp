// SPDX-License-Identifier: GPL-3.0-or-later
// The Memory window: synthetic stores (headless) and a real nvc simulation of a VHDL RAM.
#include "app/objects_pane.hpp"
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"
#include "wave/memory_model.hpp"
#include "wave/memory_window.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include <environment.hpp>
#include <fstream>
#include <process.hpp>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::string bitsOf(unsigned v, unsigned width) {
    std::string s(width, '0');
    for (unsigned b = 0; b < width; ++b)
        s[width - 1 - b] = ((v >> b) & 1u) ? '1' : '0';
    return s;
}

// t.mem: 16 words of 8 bits, t.imem: 4 integers. Word a of mem is written with 3a+1 at 10a + 5 (a = 0..9).
struct Ram {
    std::shared_ptr<WaveStore> st = std::make_shared<WaveStore>();
    ScopeId top = st->addScope(kRootScope, "t");
    std::vector<SignalId> mem, imem;
    std::vector<unsigned> values = std::vector<unsigned>(16, 0);

    Ram() {
        for (int i = 0; i < 16; ++i)
            mem.push_back(st->addSignal(top, "mem[" + std::to_string(i) + "][7:0]", SignalKind::Logic, 8));
        for (int i = 3; i >= 0; --i)
            imem.push_back(st->addSignal(top, "imem[" + std::to_string(i) + "]", SignalKind::Logic, 32));
        st->addSignal(top, "clk", SignalKind::Logic, 1);
        dump(0);
        for (unsigned a = 0; a < 10; ++a)
            write(10 * a + 5, a, 3 * a + 1);
    }
    void dump(Time t) {
        for (std::size_t i = 0; i < mem.size(); ++i)
            st->mutableSignal(mem[i]).appendLogic(t, bitsOf(values[i], 8));
        if (t == 0)
            for (SignalId id : imem)
                st->mutableSignal(id).appendLogic(0, bitsOf(0, 32));
        st->extendTimeRange(t);
    }
    void write(Time t, unsigned addr, unsigned v) {
        values[addr] = v;
        dump(t);
    }
};

// ---- real nvc -------------------------------------------------------------------------------------------

const char* kRamDesign = R"(
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity t is end;
architecture a of t is
  type ram_t is array (0 to 63) of std_logic_vector(15 downto 0);
  type iram_t is array (0 to 7) of integer;
  signal mem : ram_t := (others => (others => '0'));
  signal imem : iram_t := (others => 0);
  signal clk : std_logic := '0';
begin
  clk <= not clk after 5 ns;
  process (clk)
    variable a : integer := 0;
  begin
    if rising_edge(clk) then
      mem(a) <= std_logic_vector(to_unsigned(a * 3 + 1, 16));
      imem(a mod 8) <= a;
      a := (a + 1) mod 64;
    end if;
  end process;
end;
)";

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
    ~Design() { QDir::setCurrent(previousCwd); }

    bool prepare() {
        if (!found || !dir.isValid())
            return false;
        QDir::setCurrent(dir.path());
        std::ofstream("t.vhd") << kRamDesign;
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
    lvd::tools::Environment environment() const {
        lvd::tools::Environment e = lvd::tools::discoverEnvironment();
        e.nvc = exe.toStdString();
        e.nvcDllDir = dllDir.toStdString();
        e.ctx.nvcGlobalArgs = {"-L", libDir.toStdString()};
        return e;
    }
};

} // namespace

class TestMemoryWindow : public QObject {
    Q_OBJECT

private slots:
    void listsTheMemoriesAndShowsTheFirst() {
        Ram r;
        MemoryWindow w;
        w.resize(700, 500);
        w.setStore(r.st);
        QCOMPARE(w.memoryPaths(), QStringList({"t.imem", "t.mem"}));
        QCOMPARE(w.memoryList()->topLevelItemCount(), 2);
        QCOMPARE(w.memoryList()->topLevelItem(0)->text(1), QString("3 downto 0"));
        QCOMPARE(w.memoryList()->topLevelItem(1)->text(1), QString("0 to 15"));
        QCOMPARE(w.currentMemory(), QString("t.imem"));
        QVERIFY(w.findChild<QLabel*>("memoryHint")->isHidden());

        QVERIFY(w.showMemory("/t/mem"));
        QCOMPARE(w.currentMemory(), QString("t.mem"));
        QVERIFY(!w.showMemory("/t/nothing"));
        MemoryModel* m = w.model();
        QCOMPARE(m->rowCount(), 2); // 16 words, 8 per line
        QCOMPARE(m->columnCount(), 8);
        // latest: words 0..9 written
        QCOMPARE(m->data(m->index(0, 1), Qt::DisplayRole).toString(), QString("04"));
        QCOMPARE(m->data(m->index(1, 1), Qt::DisplayRole).toString(), QString("1C")); // word 9 = 28
        QCOMPARE(m->data(m->index(1, 2), Qt::DisplayRole).toString(), QString("00"));
        QCOMPARE(m->headerData(1, Qt::Vertical, Qt::DisplayRole).toString(), QString("8"));
        QCOMPARE(m->headerData(3, Qt::Horizontal, Qt::DisplayRole).toString(), QString("+3"));
        // the last write (word 9 at 95) is marked
        QCOMPARE(m->highlightTime(), std::optional<Time>(95));
        QVERIFY(m->data(m->index(1, 1), MemoryModel::kWrittenRole).toBool());
        QVERIFY(!m->data(m->index(1, 0), MemoryModel::kWrittenRole).toBool());
        QVERIFY(m->data(m->index(1, 1), Qt::ToolTipRole).toString().contains("t.mem[9]"));
        QVERIFY(w.statusText().contains("16 x 8 bits"));
    }

    void wordsPerLineAndRadixes() {
        Ram r;
        MemoryWindow w;
        w.setStore(r.st);
        w.showMemory("t.mem");
        MemoryModel* m = w.model();
        w.findChild<QSpinBox*>("wordsBox")->setValue(5);
        QCOMPARE(m->wordsPerLine(), 5);
        QCOMPARE(m->rowCount(), 4);
        QCOMPARE(m->headerData(3, Qt::Vertical, Qt::DisplayRole).toString(), QString("F"));

        auto* data = w.findChild<QComboBox*>("dataRadixBox");
        data->setCurrentIndex(data->findData(static_cast<int>(Radix::Unsigned)));
        QCOMPARE(m->dataRadix(), Radix::Unsigned);
        QCOMPARE(m->valueText(9), QString("28"));
        w.setDataRadix(Radix::Binary);
        QCOMPARE(m->valueText(1), QString("00000100"));
        QCOMPARE(data->currentData().toInt(), static_cast<int>(Radix::Binary));

        auto* addr = w.findChild<QComboBox*>("addressRadixBox");
        addr->setCurrentIndex(addr->findData(static_cast<int>(Radix::Binary)));
        QCOMPARE(m->addressRadix(), Radix::Binary);
        QCOMPARE(m->addressText(5), QString("0101"));
        w.setAddressRadix(Radix::Unsigned);
        QCOMPARE(m->addressText(15), QString("15"));

        // add mem options
        AddMemRequest req;
        req.paths = {"/t/imem"};
        req.wordsPerLine = 2;
        req.dataRadix = Radix::Signed;
        QString err;
        QVERIFY(w.applyRequest(req, &err));
        QCOMPARE(w.currentMemory(), QString("t.imem"));
        QCOMPARE(m->wordsPerLine(), 2);
        QCOMPARE(m->dataRadix(), Radix::Signed);
        req.paths = {"/t/none"};
        QVERIFY(!w.applyRequest(req, &err));
        QVERIFY(err.contains("/t/none"));
    }

    void timeTravelWithTheCursorAndLiveUpdates() {
        Ram r;
        MemoryWindow w;
        w.setStore(r.st);
        w.showMemory("t.mem");
        MemoryModel* m = w.model();
        QCOMPARE(w.timeSource(), MemoryWindow::TimeSource::Latest);
        w.showCursorTime(50.0); // ignored while showing the latest data
        QCOMPARE(w.shownTime(), Time{95});

        auto* box = w.findChild<QComboBox*>("timeBox");
        box->setCurrentIndex(box->findData(static_cast<int>(MemoryWindow::TimeSource::Cursor)));
        QCOMPARE(w.timeSource(), MemoryWindow::TimeSource::Cursor);
        QCOMPARE(w.shownTime(), Time{50});
        QCOMPARE(m->valueText(4), QString("0D"));
        QCOMPARE(m->valueText(5), QString("00")); // written at 55
        QCOMPARE(m->highlightTime(), std::optional<Time>(45));
        QCOMPARE(m->highlightedWords(), std::vector<std::size_t>{4});
        w.showCursorTime(55.0);
        QCOMPARE(m->valueText(5), QString("10"));
        QVERIFY(m->data(m->index(0, 5), MemoryModel::kWrittenRole).toBool());

        // stepping through the writes asks the wave window to move the cursor
        QSignalSpy moved(&w, &MemoryWindow::timeSelected);
        QVERIFY(w.stepWrite(1));
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.at(0).at(0).toDouble(), 65.0);
        QCOMPARE(w.shownTime(), Time{65});
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 6);
        QVERIFY(w.stepWrite(-1));
        QCOMPARE(w.shownTime(), Time{55});
        w.showCursorTime(0.0);
        QVERIFY(!w.stepWrite(-1)); // the initial values are no write
        QVERIFY(w.statusText().contains("not written yet"));

        // live: the store grows, the latest view follows
        w.setTimeSource(MemoryWindow::TimeSource::Latest);
        r.write(105, 10, 0xAB);
        w.refresh();
        QCOMPARE(w.shownTime(), Time{105});
        QCOMPARE(m->valueText(10), QString("AB"));
        QCOMPARE(m->highlightedWords(), std::vector<std::size_t>{10});
    }

    void goToAddressAndFindValue() {
        Ram r;
        MemoryWindow w;
        w.setStore(r.st);
        w.showMemory("t.mem");
        MemoryModel* m = w.model();
        QVERIFY(w.goToAddress("c"));
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 12);
        QVERIFY(w.goToAddress("0x3"));
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 3);
        QVERIFY(!w.goToAddress("10")); // hex 16 is past the end
        QVERIFY(w.statusText().contains("not an address"));
        w.setAddressRadix(Radix::Unsigned);
        QVERIFY(w.goToAddress("10"));
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 10);

        // words 10..15 are 00; from word 10 forward
        QVERIFY(w.findValue("00", 1));
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 11);
        QVERIFY(w.findValue("1C", -1));
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 9);
        QVERIFY(w.findValue("0?", 1)); // wildcard digit: word 10 (00) is the next match
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 10);
        w.goToAddress("15");
        QVERIFY(w.findValue("01", 1));
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 0);
        QVERIFY(w.statusText().contains("wrapped"));
        QVERIFY(!w.findValue("EE", 1));
        QVERIFY(!w.findValue("GG", 1));
        QVERIFY(w.statusText().contains("Find:"));

        // the line edits drive the same functions
        auto* go = w.findChild<QLineEdit*>("gotoEdit");
        go->setText("2");
        QTest::keyClick(go, Qt::Key_Return);
        QCOMPARE(m->wordAt(w.table()->currentIndex()), 2);

        w.table()->selectionModel()->select(m->index(0, 1), QItemSelectionModel::ClearAndSelect);
        w.table()->selectionModel()->select(m->index(1, 0), QItemSelectionModel::Select);
        QCOMPARE(w.selectedWords(), (std::vector<std::size_t>{1, 8}));
    }

    void savesAReadmemFile() {
        Ram r;
        MemoryWindow w;
        w.setStore(r.st);
        w.showMemory("t.mem");
        QTemporaryDir dir;
        const QString hex = dir.path() + "/ram.mem";
        w.setFilePrompt([&] { return hex; });
        QVERIFY(w.saveDialog());
        QFile f(hex);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith("// t.mem [0:15], 8 bit words, at 95"));
        QVERIFY(text.contains("@0\n01 04 07 0a 0d 10 13 16\n19 1c 00 00 00 00 00 00\n"));
        const QString bin = dir.path() + "/ram.bin";
        QVERIFY(w.saveMemoryFile(bin));
        QFile b(bin);
        QVERIFY(b.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(b.readAll()).contains("$readmemb\n@0\n00000001 00000100"));
        QString err;
        QVERIFY(!w.saveMemoryFile(dir.path() + "/no/such/dir/x.mem", &err));
        QVERIFY(!err.isEmpty());
    }

    void withoutMemoriesAHintIsShown() {
        auto st = std::make_shared<WaveStore>();
        const ScopeId s = st->addScope(kRootScope, "t");
        st->addSignal(s, "data[7:0]", SignalKind::Logic, 8);
        MemoryWindow w;
        w.show();
        w.setStore(st);
        QVERIFY(w.memoryPaths().isEmpty());
        QVERIFY(!w.findChild<QLabel*>("memoryHint")->isHidden());
        QCOMPARE(w.model()->rowCount(), 0);
        QVERIFY(!w.goToAddress("0"));
        QVERIFY(!w.stepWrite(1));
        w.setStore(nullptr);
        QVERIFY(w.findChild<QLabel*>("memoryHint")->isHidden());
    }

    void objectsPaneGroupsTheElements() {
        Ram r;
        ObjectsPane pane;
        pane.setStore(r.st.get());
        QCOMPARE(pane.signalCount(), 16 + 4 + 1);
        QTreeWidgetItem* mem = pane.memoryItem("t.mem");
        QVERIFY(mem != nullptr);
        QCOMPARE(mem->text(0), QString("mem"));
        QCOMPARE(mem->text(1), QString("memory"));
        QCOMPARE(mem->text(2), QString("16 x 8"));
        QCOMPARE(mem->childCount(), 16);
        QVERIFY(!mem->isExpanded());
        QCOMPARE(mem->child(3)->text(0), QString("mem[3][7:0]"));
        QVERIFY(pane.memoryItem("t.imem") != nullptr);

        pane.tree()->setCurrentItem(mem->child(3));
        QCOMPARE(pane.currentMemoryPath(), QString("t.mem"));
        pane.tree()->setCurrentItem(mem);
        QCOMPARE(pane.currentMemoryPath(), QString("t.mem"));
        mem->setSelected(true);
        QCOMPARE(pane.selectedSignalPaths().size(), 16); // the memory stands for its words
        // clk is a plain signal of scope t
        for (int i = 0; i < mem->parent()->childCount(); ++i)
            if (mem->parent()->child(i)->text(0) == "clk") {
                pane.tree()->setCurrentItem(mem->parent()->child(i));
                QVERIFY(pane.currentMemoryPath().isEmpty());
            }
        pane.setFilter("mem?1*");
        QVERIFY(!mem->isHidden());
        QCOMPARE(pane.visibleSignalCount(), 8); // mem[1], mem[10..15] and imem[1]
    }

    // ---- with the real simulator ---------------------------------------------------------------------

    void aVhdlRamInTheSimulation() {
        Design d;
        if (!d.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY2(d.prepare(), "could not analyse the test design");
        SimWindow w(d.environment(), d.dir.path() + "/s.ini");
        w.setDialogsEnabled(false);
        QVERIFY(!w.recordMemories());
        w.findChild<QAction*>("recordMemories")->trigger();
        QVERIFY(w.recordMemories());

        QVERIFY(w.startSimulation(
            {"-do", "add mem -dataradix unsigned -wordsperline 4 /t/mem; run 100 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        MemoryWindow* mw = w.memory();
        QVERIFY(!w.findChild<QDockWidget*>("memoryDock")->isHidden());
        QCOMPARE(mw->memoryPaths(), QStringList({"t.imem", "t.mem"}));
        QCOMPARE(mw->currentMemory(), QString("t.mem"));
        MemoryModel* m = mw->model();
        QCOMPARE(m->wordsPerLine(), 4);
        QCOMPARE(m->dataRadix(), Radix::Unsigned);
        QCOMPARE(m->rowCount(), 16);
        // rising edges at 5, 15, ..., 95 ns wrote words 0..9 with 3a+1
        QCOMPARE(m->valueText(0), QString("1"));
        QCOMPARE(m->valueText(9), QString("28"));
        QCOMPARE(m->valueText(10), QString("0"));
        QCOMPARE(m->highlightTime(), std::optional<Time>(95000000));
        QCOMPARE(m->highlightedWords(), std::vector<std::size_t>{9});

        // time travel: the wave cursor at 50 ns
        mw->setTimeSource(MemoryWindow::TimeSource::Cursor);
        w.wave()->setCursorTime(0, 50000000.0);
        w.wave()->setActiveCursor(0);
        QCOMPARE(mw->shownTime(), Time{50000000});
        QCOMPARE(m->valueText(4), QString("13"));
        QCOMPARE(m->valueText(5), QString("0"));
        // next write moves the wave cursor
        QVERIFY(mw->stepWrite(1));
        QCOMPARE(w.wave()->cursorTime(w.wave()->activeCursor()), std::optional<double>(55000000.0));
        QCOMPARE(m->valueText(5), QString("16"));

        // live: more simulation time arrives in the latest view
        mw->setTimeSource(MemoryWindow::TimeSource::Latest);
        w.sendCommand("run 50 ns");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(m->valueText(14), QString("43"));
        QCOMPARE(m->highlightedWords(), std::vector<std::size_t>{14});

        // the Objects pane groups the words; the Tcl commands reach the window
        QVERIFY(w.objects()->memoryItem("t.mem") != nullptr);
        QCOMPARE(w.objects()->memoryItem("t.mem")->childCount(), 64);
        w.findChild<QDockWidget*>("memoryDock")->hide();
        w.sendCommand("view memory");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(!w.findChild<QDockWidget*>("memoryDock")->isHidden());
        w.sendCommand("add mem /t/imem");
        QVERIFY(w.waitUntilIdle());
        QCOMPARE(mw->currentMemory(), QString("t.imem"));
        QCOMPARE(m->valueText(7), QString("7")); // imem(7) <= 7 at 75 ns (unsigned radix kept)
        w.sendCommand("add mem /t/clk");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("** add mem: no memory /t/clk"));
        w.stopSimulation();
    }

    void withoutRecordingTheWindowExplainsWhy() {
        Design d;
        if (!d.found)
            QSKIP("nvc not found (set LVDSIM_NVC)");
        QVERIFY2(d.prepare(), "could not analyse the test design");
        SimWindow w(d.environment(), d.dir.path() + "/s.ini");
        w.setDialogsEnabled(false);
        QVERIFY(w.startSimulation({"-do", "run 20 ns", "work.t"}));
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.memory()->memoryPaths().isEmpty());
        w.showMemoryDock();
        QVERIFY(!w.memory()->findChild<QLabel*>("memoryHint")->isHidden());
        w.sendCommand("add mem /t/mem");
        QVERIFY(w.waitUntilIdle());
        QVERIFY(w.transcript()->text().contains("no memories are recorded"));
        w.stopSimulation();
    }
};

QTEST_MAIN(TestMemoryWindow)
#include "test_memory_window.moc"
