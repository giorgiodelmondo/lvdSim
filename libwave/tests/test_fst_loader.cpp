// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <lvdwave/fst_loader.hpp>

extern "C" {
#include <fstapi.h>
}

using namespace lvd::wave;

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const char* name) : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove(path);
    }
    ~TempFile() { std::filesystem::remove(path); }
};

}  // namespace

TEST_CASE("loadFst reads hierarchy, aliases and all value kinds") {
    TempFile f("lvdwave_loader_test.fst");
    {
        auto* w = fstWriterCreate(f.path.string().c_str(), 1);
        REQUIRE(w != nullptr);
        fstWriterSetTimescale(w, -12);
        fstWriterSetScope(w, FST_ST_VHDL_ARCHITECTURE, "top", nullptr);
        fstHandle clk = fstWriterCreateVar(w, FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "clk", 0);
        fstWriterSetScope(w, FST_ST_VHDL_BLOCK, "dut", nullptr);
        fstHandle bus = fstWriterCreateVar(w, FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 8, "bus", 0);
        fstHandle real = fstWriterCreateVar(w, FST_VT_VCD_REAL, FST_VD_IMPLICIT, 8, "volt", 0);
        fstHandle state = fstWriterCreateVar(w, FST_VT_GEN_STRING, FST_VD_IMPLICIT, 0, "state", 0);
        fstWriterCreateVar(w, FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "clk_copy", clk);  // alias
        fstWriterSetUpscope(w);
        fstWriterSetUpscope(w);

        double volt = 0.5;
        // fstapi drops variable length values written before the first time change; NVC always emits
        // a time change first, so do the same here.
        fstWriterEmitTimeChange(w, 0);
        fstWriterEmitValueChange(w, clk, "0");
        fstWriterEmitValueChange(w, bus, "00000000");
        fstWriterEmitValueChange(w, real, &volt);
        fstWriterEmitVariableLengthValueChange(w, state, "IDLE", 4);

        fstWriterEmitTimeChange(w, 100);
        volt = -2.5;
        fstWriterEmitValueChange(w, clk, "1");
        fstWriterEmitValueChange(w, bus, "1010X0Z1");
        fstWriterEmitValueChange(w, real, &volt);
        fstWriterEmitVariableLengthValueChange(w, state, "RUNNING", 7);

        fstWriterEmitTimeChange(w, 250);
        fstWriterEmitValueChange(w, clk, "0");
        fstWriterClose(w);
    }

    auto r = loadFst(f.path.string());
    REQUIRE(r.ok());
    const WaveStore& st = r.value();

    REQUIRE(st.timescaleExponent() == -12);
    REQUIRE(st.startTime() == 0);
    REQUIRE(st.endTime() == 250);

    const SignalId clk = st.findSignal("top.clk");
    const SignalId bus = st.findSignal("top.dut.bus");
    const SignalId volt = st.findSignal("top.dut.volt");
    const SignalId state = st.findSignal("top.dut.state");
    const SignalId copy = st.findSignal("top.dut.clk_copy");
    REQUIRE(clk != kNoSignal);
    REQUIRE(bus != kNoSignal);
    REQUIRE(volt != kNoSignal);
    REQUIRE(state != kNoSignal);
    REQUIRE(copy != kNoSignal);

    REQUIRE(st.resolve(copy) == clk);
    REQUIRE(st.signal(clk).changeCount() == 3);
    REQUIRE(st.signal(clk).time(1) == 100);
    REQUIRE(st.signal(clk).bits(1) == "1");

    REQUIRE(st.signal(bus).width() == 8);
    REQUIRE(st.signal(bus).bits(1) == "1010X0Z1");

    REQUIRE(st.signal(volt).kind() == SignalKind::Real);
    REQUIRE(st.signal(volt).real(0) == 0.5);
    REQUIRE(st.signal(volt).real(1) == -2.5);

    REQUIRE(st.signal(state).kind() == SignalKind::String);
    REQUIRE(st.signal(state).changeCount() == 2);
    REQUIRE(st.signal(state).text(0) == "IDLE");
    REQUIRE(st.signal(state).text(1) == "RUNNING");

    REQUIRE(st.scope(st.entry(bus).scope()).name == "dut");
}

TEST_CASE("loadFst reports a missing file") {
    auto r = loadFst("this_file_does_not_exist.fst");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().find("cannot open") != std::string::npos);
}

TEST_CASE("loadFst rejects a file that was never finalised") {
    TempFile f("lvdwave_unfinished.fst");
    {
        std::FILE* fp = std::fopen(f.path.string().c_str(), "wb");
        REQUIRE(fp != nullptr);
        const char junk[] = "not an fst file";
        std::fwrite(junk, 1, sizeof junk, fp);
        std::fclose(fp);
    }
    REQUIRE_FALSE(loadFst(f.path.string()).ok());
}
