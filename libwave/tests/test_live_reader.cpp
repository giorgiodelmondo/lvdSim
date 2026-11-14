// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <lvdwave/compare.hpp>
#include <lvdwave/list_index.hpp>
#include <lvdwave/live_reader.hpp>
#include <lvdwave/lod.hpp>
#include <lvdwave/memory.hpp>
#include <lvdwave/wave_space.hpp>
#include <memory>

using namespace lvd::wave;
namespace fs = std::filesystem;

namespace {

// A file that the "simulator" appends to in pieces.
struct LiveFile {
    fs::path path;
    explicit LiveFile(const char* name) : path(fs::temp_directory_path() / name) { fs::remove(path); }
    ~LiveFile() { fs::remove(path); }
    void append(const std::string& text) const {
        std::ofstream f(path, std::ios::binary | std::ios::app);
        f << text;
    }
    void write(const std::string& text) const {
        fs::remove(path);
        append(text);
    }
};

const char* kDefs =
    "LVW 1\n"
    "TS -12\n"
    "S 12 top\n"
    "V 1 23 1 L clk\n"
    "S 12 dut\n"
    "V 2 1 8 L bus\n"
    "V 3 3 64 R volt\n"
    "V 4 21 0 S state\n"
    "V 1 23 1 L clk_alias\n"
    "U\n"
    "U\n"
    "E\n";

}  // namespace

TEST_CASE("definitions build the scope tree, signals and aliases") {
    LiveFile f("lvdwave_live_defs.lvw");
    f.write(kDefs);
    WaveStore st;
    LiveReader r(f.path.string());

    auto res = r.poll(st);
    REQUIRE(res.ok());
    REQUIRE(res.value().newSignals == 5);
    REQUIRE(res.value().definitionsCompleted);
    REQUIRE(r.definitionsComplete());
    REQUIRE(st.timescaleExponent() == -12);

    const SignalId clk = st.findSignal("top.clk");
    const SignalId bus = st.findSignal("top.dut.bus");
    const SignalId volt = st.findSignal("top.dut.volt");
    const SignalId state = st.findSignal("top.dut.state");
    const SignalId alias = st.findSignal("top.dut.clk_alias");
    REQUIRE(clk != kNoSignal);
    REQUIRE(bus != kNoSignal);
    REQUIRE(volt != kNoSignal);
    REQUIRE(state != kNoSignal);
    REQUIRE(alias != kNoSignal);
    REQUIRE(st.signal(bus).width() == 8);
    REQUIRE(st.signal(volt).kind() == SignalKind::Real);
    REQUIRE(st.signal(state).kind() == SignalKind::String);
    REQUIRE(st.resolve(alias) == clk);
    REQUIRE(st.scope(st.entry(bus).scope()).name == "dut");
    REQUIRE(st.scope(st.entry(bus).scope()).type == 12);
}

TEST_CASE("values arrive in pieces across polls") {
    LiveFile f("lvdwave_live_pieces.lvw");
    f.write(kDefs);
    WaveStore st;
    LiveReader r(f.path.string());
    REQUIRE(r.poll(st).ok());
    const SignalId clk = st.findSignal("top.clk");
    const SignalId bus = st.findSignal("top.dut.bus");

    f.append("#0\n1 L 0\n2 L 00000000\n");
    auto a = r.poll(st);
    REQUIRE(a.ok());
    REQUIRE(a.value().valueChanges == 2);
    REQUIRE_FALSE(a.value().timeAdvanced);  // time 0 is not an advance
    REQUIRE(st.signal(clk).changeCount() == 1);

    f.append("#5000\n1 L 1\n2 L 1010X0Z1\n");
    auto b = r.poll(st);
    REQUIRE(b.value().valueChanges == 2);
    REQUIRE(b.value().timeAdvanced);
    REQUIRE(r.currentTime() == 5000);
    REQUIRE(st.endTime() == 5000);
    REQUIRE(st.signal(clk).bits(1) == "1");
    REQUIRE(st.signal(bus).bits(1) == "1010X0Z1");
    REQUIRE(st.signal(clk).time(1) == 5000);

    auto none = r.poll(st);  // nothing new
    REQUIRE(none.ok());
    REQUIRE_FALSE(none.value().changed());
}

TEST_CASE("a line that is still being written waits for the next poll") {
    LiveFile f("lvdwave_live_partial.lvw");
    f.write(kDefs);
    WaveStore st;
    LiveReader r(f.path.string());
    REQUIRE(r.poll(st).ok());
    const SignalId bus = st.findSignal("top.dut.bus");

    f.append("#10\n2 L 1111");  // the writer has not finished this line
    auto a = r.poll(st);
    REQUIRE(a.value().valueChanges == 0);
    REQUIRE(st.signal(bus).changeCount() == 0);

    f.append("0000\n");
    auto b = r.poll(st);
    REQUIRE(b.value().valueChanges == 1);
    REQUIRE(st.signal(bus).bits(0) == "11110000");
}

TEST_CASE("real, string and escaped string values") {
    LiveFile f("lvdwave_live_kinds.lvw");
    f.write(std::string(kDefs) + "#0\n3 R 2.5\n4 S IDLE\n#100\n3 R -1e-3\n4 S two words\n#200\n4 S line\\none\\\\x\n");
    WaveStore st;
    LiveReader r(f.path.string());
    REQUIRE(r.poll(st).ok());

    const Signal& volt = st.signal(st.findSignal("top.dut.volt"));
    const Signal& state = st.signal(st.findSignal("top.dut.state"));
    REQUIRE(volt.real(0) == 2.5);
    REQUIRE(volt.real(1) == -1e-3);
    REQUIRE(state.text(0) == "IDLE");
    REQUIRE(state.text(1) == "two words");
    REQUIRE(state.text(2) == "line\none\\x");
}

TEST_CASE("alias values land on the original signal") {
    LiveFile f("lvdwave_live_alias.lvw");
    f.write(std::string(kDefs) + "#7\n1 L 1\n");
    WaveStore st;
    LiveReader r(f.path.string());
    REQUIRE(r.poll(st).ok());
    REQUIRE(st.signal(st.findSignal("top.dut.clk_alias")).changeCount() == 1);
    REQUIRE(st.totalChanges() == 1);
}

TEST_CASE("signal references stay valid while data keeps arriving") {
    LiveFile f("lvdwave_live_refs.lvw");
    f.write(kDefs);
    WaveStore st;
    LiveReader r(f.path.string());
    REQUIRE(r.poll(st).ok());
    const Signal* clk = &st.signal(st.findSignal("top.clk"));

    for (int i = 0; i < 200; ++i) {
        f.append("#" + std::to_string(i * 10) + "\n1 L " + std::string(i % 2 ? "1" : "0") + "\n");
        REQUIRE(r.poll(st).ok());
    }
    REQUIRE(clk == &st.signal(st.findSignal("top.clk")));
    REQUIRE(clk->changeCount() == 200);
}

TEST_CASE("the writer may not have created the file yet") {
    LiveFile f("lvdwave_live_late.lvw");
    WaveStore st;
    LiveReader r(f.path.string());
    auto missing = r.poll(st);
    REQUIRE_FALSE(missing.ok());
    REQUIRE(missing.error().find("cannot open") != std::string::npos);

    f.write("LVW 1\n");  // created, but empty so far
    REQUIRE(r.poll(st).ok());
    f.append(kDefs + 6);
    REQUIRE(r.poll(st).value().newSignals == 5);
}

TEST_CASE("bad input is reported or skipped without damage") {
    SECTION("wrong header") {
        LiveFile f("lvdwave_live_badhdr.lvw");
        f.write("NOT A STREAM\n");
        WaveStore st;
        LiveReader r(f.path.string());
        REQUIRE_FALSE(r.poll(st).ok());
    }
    SECTION("truncated file") {
        LiveFile f("lvdwave_live_trunc.lvw");
        f.write(std::string(kDefs) + "#5\n1 L 1\n");
        WaveStore st;
        LiveReader r(f.path.string());
        REQUIRE(r.poll(st).ok());
        f.write("LVW 1\n");
        REQUIRE_FALSE(r.poll(st).ok());
    }
    SECTION("garbage records are counted and skipped") {
        LiveFile f("lvdwave_live_garbage.lvw");
        f.write(std::string(kDefs) + "#5\n99 L 1\nx y z\n1 Q 1\n1 R 1.0\n#abc\n1 L 1\n");
        WaveStore st;
        LiveReader r(f.path.string());
        auto res = r.poll(st);
        REQUIRE(res.ok());
        REQUIRE(res.value().badLines == 5);  // unknown handle, nonsense, bad kind, kind mismatch, bad time
        REQUIRE(res.value().valueChanges == 1);
    }
    SECTION("time going backwards cannot corrupt the store") {
        LiveFile f("lvdwave_live_back.lvw");
        f.write(std::string(kDefs) + "#100\n1 L 1\n#50\n1 L 0\n");
        WaveStore st;
        LiveReader r(f.path.string());
        auto res = r.poll(st);
        REQUIRE(res.value().badLines == 1);
        REQUIRE(st.signal(st.findSignal("top.clk")).changeCount() == 1);
    }
    SECTION("definitions after the end marker are ignored") {
        LiveFile f("lvdwave_live_late_def.lvw");
        f.write(std::string(kDefs) + "V 9 1 1 L late\n");
        WaveStore st;
        LiveReader r(f.path.string());
        auto res = r.poll(st);
        REQUIRE(res.value().badLines == 1);
        REQUIRE(st.findSignal("top.dut.late") == kNoSignal);
    }
    SECTION("Windows line endings are accepted") {
        LiveFile f("lvdwave_live_crlf.lvw");
        f.write("LVW 1\r\nTS -9\r\nS 12 t\r\nV 1 1 4 L v\r\nU\r\nE\r\n#3\r\n1 L 0101\r\n");
        WaveStore st;
        LiveReader r(f.path.string());
        REQUIRE(r.poll(st).ok());
        REQUIRE(st.signal(st.findSignal("t.v")).bits(0) == "0101");
    }
}

TEST_CASE("D records give the port direction of the preceding entry") {
    LiveFile f("lvdwave_live_dir.lvw");
    f.write("LVW 1\nTS -15\nS 12 top\n"
            "V 1 23 1 L a\nD internal\n"
            "S 12 u\n"
            "V 1 23 1 L i\nD in\n" // a port that shares the data of top.a
            "V 2 23 1 L o\nD out\n"
            "V 3 23 1 L io\nD inout\n"
            "V 4 23 1 L b\nD buffer\n"
            "V 5 23 1 L l\nD linkage\n"
            "V 6 23 1 L f\nD sideways\n" // a word from a newer writer
            "V 7 23 1 L old\n"           // no D record: an older writer
            "U\nU\nE\n#0\n1 L 1\n");
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    REQUIRE(res.value().badLines == 0);
    auto dir = [&](const char* path) { return st.entry(st.findSignal(path)).direction(); };
    REQUIRE(dir("top.a") == PortDirection::Internal);
    REQUIRE(dir("top.u.i") == PortDirection::In); // the alias has its own direction
    REQUIRE(st.resolve(st.findSignal("top.u.i")) == st.findSignal("top.a"));
    REQUIRE(dir("top.u.o") == PortDirection::Out);
    REQUIRE(dir("top.u.io") == PortDirection::InOut);
    REQUIRE(dir("top.u.b") == PortDirection::Buffer);
    REQUIRE(dir("top.u.l") == PortDirection::Linkage);
    REQUIRE(dir("top.u.f") == PortDirection::Unknown);
    REQUIRE(dir("top.u.old") == PortDirection::Unknown);
}

TEST_CASE("a stream without D records still loads") {
    LiveFile f("lvdwave_live_nodir.lvw");
    f.write(kDefs);
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    REQUIRE(res.value().badLines == 0);
    REQUIRE(st.entry(st.findSignal("top.clk")).direction() == PortDirection::Unknown);
}

TEST_CASE("misplaced D records are skipped") {
    LiveFile f("lvdwave_live_baddir.lvw");
    // before any V, twice for one V, after the end of the definitions
    f.write("LVW 1\nD in\nS 12 t\nV 1 23 1 L a\nD out\nD in\nU\nE\nD in\n");
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    REQUIRE(res.value().badLines == 3);
    REQUIRE(st.entry(st.findSignal("t.a")).direction() == PortDirection::Out);
}

TEST_CASE("a time record without values marks how far the run got") {
    // The kernel ends every run with the time it reached, also when the last value change was earlier.
    LiveFile f("lvdwave_live_runend.lvw");
    f.write(std::string(kDefs) + "#0\n1 L 0\n#40\n1 L 1\n#100\n");
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    REQUIRE(res.value().timeAdvanced);
    REQUIRE(st.endTime() == 100);
    REQUIRE(r.currentTime() == 100);
    REQUIRE(st.signal(st.findSignal("top.clk")).changeCount() == 2);

    // the next run continues after it
    f.append("#150\n1 L 0\n#200\n");
    res = r.poll(st);
    REQUIRE(res.ok());
    REQUIRE(st.endTime() == 200);
    REQUIRE(st.signal(st.findSignal("top.clk")).time(2) == 150);
}

TEST_CASE("a restart record clears the values and keeps the signals") {
    // The kernel writes R <base> when the shell restarts the simulation: time goes back to zero, the
    // definitions stay and the value records of the new run name handle h as h + base.
    LiveFile f("lvdwave_live_restart.lvw");
    f.write(std::string(kDefs) + "#0\n1 L 0\n2 L 00000001\n#40\n1 L 1\n3 R 2.5\n#100\n");
    WaveStore st;
    LiveReader r(f.path.string());
    REQUIRE(r.poll(st).ok());
    const SignalId clk = st.findSignal("top.clk");
    const SignalId bus = st.findSignal("top.dut.bus");
    const Signal& clkRef = st.signal(clk);
    REQUIRE(clkRef.changeCount() == 2);
    REQUIRE(st.endTime() == 100);
    const std::uint32_t generation = clkRef.generation();

    f.append("R 4\n#0\n5 L 0\n6 L 00000111\n#10\n5 L 1\n#20\n");
    auto res = r.poll(st);
    REQUIRE(res.ok());
    CHECK(res.value().restarted);
    CHECK(res.value().changed());
    CHECK(res.value().badLines == 0);
    CHECK(res.value().newSignals == 0);
    CHECK(r.restartCount() == 1);
    CHECK(r.currentTime() == 20);
    // the same signals (references stay valid), only the new run's values
    CHECK(st.findSignal("top.clk") == clk);
    CHECK(&st.signal(clk) == &clkRef);
    CHECK(clkRef.generation() == generation + 1);
    REQUIRE(clkRef.changeCount() == 2);
    CHECK(clkRef.time(0) == 0);
    CHECK(clkRef.time(1) == 10);
    CHECK(clkRef.bits(1) == "1");
    REQUIRE(st.signal(bus).changeCount() == 1);
    CHECK(st.signal(bus).bits(0) == "00000111");
    CHECK(st.signal(st.findSignal("top.dut.volt")).changeCount() == 0);
    CHECK(st.startTime() == 0);
    CHECK(st.endTime() == 20);

    // a later poll without a restart keeps adding to the new run
    f.append("#30\n5 L 0\n");
    res = r.poll(st);
    REQUIRE(res.ok());
    CHECK_FALSE(res.value().restarted);
    CHECK(clkRef.changeCount() == 3);

    // the second restart has the base of two runs; the alias still lands on the original
    f.append("R 8\n#0\n9 L 1\n");
    res = r.poll(st);
    REQUIRE(res.ok());
    CHECK(res.value().restarted);
    CHECK(r.restartCount() == 2);
    REQUIRE(clkRef.changeCount() == 1);
    CHECK(st.signal(st.findSignal("top.dut.clk_alias")).bits(0) == "1");
}

TEST_CASE("the stream the kernel writes for run, restart, run, restart, run") {
    // Written by the fork's regress test restart2 (run 15 ns, restart, run 12 ns, restart, run).
    LiveFile f("lvdwave_live_restart_kernel.lvw");
    f.write("LVW 1\nTS -15\nS 12 restart2\nV 1 23 1 L x\nD internal\nV 2 3 0 R r\nD internal\nU\nE\n"
            "#0\n1 L 0\n2 R 0.5\n#10000000\n1 L 1\n2 R 1.5\n#15000000\n"
            "R 2\n#0\n3 L 0\n4 R 0.5\n#10000000\n3 L 1\n4 R 1.5\n#12000000\n"
            "R 4\n#0\n5 L 0\n6 R 0.5\n#10000000\n5 L 1\n6 R 1.5\n#20000000\n5 L 0\n");
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    CHECK(res.value().badLines == 0);
    CHECK(res.value().restarted);
    CHECK(r.restartCount() == 2);
    const Signal& x = st.signal(st.findSignal("restart2.x"));
    const Signal& v = st.signal(st.findSignal("restart2.r"));
    REQUIRE(x.changeCount() == 3);
    CHECK(x.time(1) == 10000000);
    CHECK(x.time(2) == 20000000);
    CHECK(x.bits(2) == "0");
    REQUIRE(v.changeCount() == 2);
    CHECK(v.real(1) == 1.5);
    CHECK(st.endTime() == 20000000);
}

TEST_CASE("values before and after a restart in one poll") {
    LiveFile f("lvdwave_live_restart_onepoll.lvw");
    f.write(std::string(kDefs) + "#0\n1 L 0\n#40\n1 L 1\n#100\nR 4\n#0\n5 L 1\n#5\n");
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    CHECK(res.value().restarted);
    CHECK(res.value().definitionsCompleted);
    const Signal& clk = st.signal(st.findSignal("top.clk"));
    REQUIRE(clk.changeCount() == 1);
    CHECK(clk.bits(0) == "1");
    CHECK(st.endTime() == 5);
}

TEST_CASE("restart records that do not fit are skipped and change nothing") {
    LiveFile f("lvdwave_live_restart_bad.lvw");
    // before the end of the definitions, with a base that is not one run of handles, without a base
    f.write("LVW 1\nS 12 t\nV 1 23 1 L a\nR 1\nU\nE\n#0\n1 L 0\n#10\n1 L 1\nR 2\nR\nR x\n2 L 0\n#20\n");
    WaveStore st;
    LiveReader r(f.path.string());
    auto res = r.poll(st);
    REQUIRE(res.ok());
    CHECK_FALSE(res.value().restarted);
    CHECK(res.value().badLines == 5); // four R records and the value of a handle that is not defined
    CHECK(r.restartCount() == 0);
    const Signal& a = st.signal(st.findSignal("t.a"));
    REQUIRE(a.changeCount() == 2);
    CHECK(st.endTime() == 20);

    // handles of the old run are not valid after a restart
    f.append("R 1\n#0\n1 L 1\n2 L 0\n");
    res = r.poll(st);
    REQUIRE(res.ok());
    CHECK(res.value().restarted);
    CHECK(res.value().badLines == 1);
    REQUIRE(a.changeCount() == 1);
    CHECK(a.bits(0) == "0");
}

TEST_CASE("a restart starts the incremental readers of the values again") {
    // Indexes, expressions, comparisons, list rows and memory images built during the first run must not keep
    // the old values when the store is cleared, also when the new run is already longer than the old one.
    LiveFile f("lvdwave_live_restart_readers.lvw");
    f.write(
        "LVW 1\nS 12 t\nV 1 1 4 L a[3:0]\nV 2 1 4 L b[3:0]\nV 3 1 4 L m[0][3:0]\nV 4 1 4 L m[1][3:0]\nU\nE\n"
        "#0\n1 L 0001\n2 L 0001\n3 L 0000\n4 L 0000\n#10\n1 L 0010\n3 L 0001\n#20\n2 L 0011\n4 L "
        "0010\n#30\n");
    auto st = std::make_shared<WaveStore>();
    LiveReader r(f.path.string());
    REQUIRE(r.poll(*st).ok());
    const Signal& a = st->signal(st->findSignal("t.a[3:0]"));
    const Signal& b = st->signal(st->findSignal("t.b[3:0]"));

    NumericIndex idx = NumericIndex::build(a);
    WaveSpace space;
    space.setCurrent(st);
    auto expr = ExpressionSignal::create("a ^ b", space.resolver());
    REQUIRE(expr.ok());
    SignalCompare cmp(a, b);
    cmp.update(st->endTime());
    ListIndex list(*st, {st->findSignal("t.a[3:0]"), st->findSignal("t.b[3:0]")});
    const auto mem = findMemory(*st, "t.m");
    REQUIRE(mem.has_value());
    MemoryImage image(*st, *mem);
    REQUIRE(idx.size() == 2);
    REQUIRE(expr.value()->signal().changeCount() == 3);
    REQUIRE(list.rowCount() == 3);
    REQUIRE(image.writes().size() == 2);

    // the new run has more changes than the old one before anything is updated
    f.append("R 4\n#0\n5 L 0111\n6 L 0111\n7 L 0000\n8 L 0000\n#5\n5 L 1000\n7 L 0011\n#6\n5 L 1001\n#7\n5 L "
             "1010\n"
             "#8\n5 L 1011\n#9\n");
    auto res = r.poll(*st);
    REQUIRE(res.ok());
    REQUIRE(res.value().restarted);
    REQUIRE(a.changeCount() == 5);

    idx.update(a);
    REQUIRE(idx.size() == 5);
    CHECK(idx.value(0) == 7.0);
    CHECK(idx.value(4) == 11.0);
    CHECK(idx.range(0, 5).min == 7.0);
    CHECK(idx.range(0, 5).max == 11.0);

    expr.value()->update();
    const Signal& x = expr.value()->signal();
    REQUIRE(x.changeCount() == 5);
    CHECK(x.time(0) == 0);
    CHECK(x.bits(0) == "0000");
    CHECK(x.bits(4) == "1100");

    cmp.update(st->endTime());
    REQUIRE(cmp.intervals().empty());
    CHECK(cmp.openSince() == Time{5});

    list.update();
    CHECK(list.rowCount() == 5);
    CHECK(list.rowTime(0) == 0);
    CHECK(list.rowTime(4) == 8);

    CHECK(image.update());
    REQUIRE(image.writes().size() == 1);
    CHECK(image.writes()[0].time == 5);
    CHECK(image.bits(0, 9) == "0011");
    CHECK(image.bits(1, 9) == "0000");
}
