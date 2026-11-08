// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <lvdwave/live_reader.hpp>

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
