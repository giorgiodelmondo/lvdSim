// SPDX-License-Identifier: GPL-3.0-or-later
// Proves the vendored fstapi compiles, links against zlib and can write and read back a trace.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

extern "C" {
#include <fstapi.h>
}

namespace {

struct Collected {
    int changes = 0;
    std::string lastValue;
    uint64_t lastTime = 0;
};

void onValue(void* user, uint64_t time, fstHandle, const unsigned char* value) {
    auto* c = static_cast<Collected*>(user);
    ++c->changes;
    c->lastTime = time;
    c->lastValue = reinterpret_cast<const char*>(value);
}

} // namespace

TEST_CASE("fstapi writes and reads back value changes") {
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "lvdsim_roundtrip.fst";
    const std::string p = path.string();

    {
        auto* w = fstWriterCreate(p.c_str(), 1);
        REQUIRE(w != nullptr);
        fstWriterSetTimescale(w, -9);
        fstWriterSetScope(w, FST_ST_VCD_MODULE, "top", nullptr);
        fstHandle h = fstWriterCreateVar(w, FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 4, "bus", 0);
        fstWriterSetUpscope(w);
        fstWriterEmitValueChange(w, h, "0000");
        fstWriterEmitTimeChange(w, 10);
        fstWriterEmitValueChange(w, h, "10x1");
        fstWriterEmitTimeChange(w, 25);
        fstWriterEmitValueChange(w, h, "zzzz");
        fstWriterClose(w);
    }

    auto* r = fstReaderOpen(p.c_str());
    REQUIRE(r != nullptr);
    Collected c;
    fstReaderSetFacProcessMaskAll(r);
    fstReaderIterBlocks(r, onValue, &c, nullptr);
    fstReaderClose(r);
    fs::remove(path);

    REQUIRE(c.changes == 3);
    REQUIRE(c.lastTime == 25);
    REQUIRE(c.lastValue == "zzzz");
}
