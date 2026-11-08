// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <lvdwave/logic.hpp>
#include <lvdwave/store.hpp>

using namespace lvd::wave;

TEST_CASE("logic characters round-trip") {
    for (unsigned i = 0; i < kLogicCount; ++i) {
        const auto v = static_cast<Logic>(i);
        REQUIRE(fromChar(toChar(v)) == v);
    }
    REQUIRE(fromChar('x') == Logic::X);
    REQUIRE(fromChar('?') == Logic::X);
    REQUIRE(isKnown(Logic::H));
    REQUIRE(bitValue(Logic::H));
    REQUIRE_FALSE(isKnown(Logic::Z));
}

TEST_CASE("scalar signal changes and lookup") {
    WaveStore st;
    const SignalId clk = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
    Signal& s = st.mutableSignal(clk);
    s.appendLogic(0, "0");
    s.appendLogic(10, "1");
    s.appendLogic(20, "0");
    s.appendLogic(20, "X");  // same time: last wins
    s.appendLogic(35, "Z");

    REQUIRE(s.changeCount() == 5);
    REQUIRE(s.holdIndex(0) == 0);
    REQUIRE(s.holdIndex(9) == 0);
    REQUIRE(s.holdIndex(10) == 1);
    REQUIRE(s.holdIndex(20) == 3);
    REQUIRE(s.holdIndex(1000) == 4);
    REQUIRE(s.bits(s.holdIndex(20)) == "X");
    REQUIRE(s.bits(4) == "Z");

    auto [a, b] = s.changesIn(10, 35);
    REQUIRE(a == 1);
    REQUIRE(b == 4);
    auto [c, d] = s.changesIn(36, 100);
    REQUIRE(c == d);
}

TEST_CASE("hold index before first change") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
    st.mutableSignal(id).appendLogic(100, "1");
    REQUIRE(st.signal(id).holdIndex(99) == kNoIndex);
    REQUIRE(st.signal(id).holdIndex(100) == 0);
}

TEST_CASE("vector values keep all nine logic values") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Logic, 9);
    Signal& s = st.mutableSignal(id);
    s.appendLogic(0, "01XZUWLH-");
    REQUIRE(s.bits(0) == "01XZUWLH-");
    REQUIRE(s.bit(0, 0) == Logic::DontCare);  // least significant is the last character
    REQUIRE(s.bit(0, 8) == Logic::L0);
}

TEST_CASE("short values are left extended, long ones truncated") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Logic, 6);
    Signal& s = st.mutableSignal(id);
    s.appendLogic(0, "101");
    s.appendLogic(1, "x1");
    s.appendLogic(2, "z");
    s.appendLogic(3, "1111111111");
    REQUIRE(s.bits(0) == "000101");
    REQUIRE(s.bits(1) == "XXXXX1");
    REQUIRE(s.bits(2) == "ZZZZZZ");
    REQUIRE(s.bits(3) == "111111");
}

TEST_CASE("real and string signals") {
    WaveStore st;
    const SignalId r = st.addSignal(kRootScope, "r", SignalKind::Real, 64);
    const SignalId e = st.addSignal(kRootScope, "e", SignalKind::String, 0);
    st.mutableSignal(r).appendReal(0, 1.5);
    st.mutableSignal(r).appendReal(5, -2.25);
    st.mutableSignal(e).appendText(0, "idle");
    st.mutableSignal(e).appendText(5, "run");
    st.mutableSignal(e).appendText(9, "idle");

    REQUIRE(st.signal(r).real(1) == -2.25);
    REQUIRE(st.signal(e).text(0) == "idle");
    REQUIRE(st.signal(e).text(2) == "idle");
    REQUIRE(st.signal(e).text(1) == "run");
    REQUIRE(st.signal(r).width() == 1);  // non-logic kinds report width 1
}

TEST_CASE("scopes, paths, lookup and aliases") {
    WaveStore st;
    const ScopeId top = st.addScope(kRootScope, "top");
    const ScopeId dut = st.addScope(top, "dut");
    const SignalId a = st.addSignal(dut, "q[3]", SignalKind::Logic, 1);
    const SignalId al = st.addAlias(top, "q3", a);
    st.mutableSignal(a).appendLogic(7, "1");

    REQUIRE(st.pathOf(a) == "top.dut.q[3]");
    REQUIRE(st.scopePath(dut) == "top.dut");
    REQUIRE(st.findSignal("top.dut.q[3]") == a);
    REQUIRE(st.findSignal("top.q3") == al);
    REQUIRE(st.findSignal("top.nope") == kNoSignal);
    REQUIRE(st.resolve(al) == a);
    REQUIRE(st.signal(al).changeCount() == 1);
    REQUIRE(st.entry(al).aliasOf() == a);
    REQUIRE(st.totalChanges() == 1);  // aliases do not double count

    // Index is rebuilt after additions.
    const SignalId b = st.addSignal(top, "late", SignalKind::Logic, 1);
    REQUIRE(st.findSignal("top.late") == b);
}

TEST_CASE("time range tracking") {
    WaveStore st;
    st.extendTimeRange(50);
    st.extendTimeRange(10);
    st.extendTimeRange(90);
    REQUIRE(st.startTime() == 10);
    REQUIRE(st.endTime() == 90);
}

TEST_CASE("bitChanges lists only the changes that affect one bit") {
    WaveStore st;
    const SignalId id = st.addSignal(kRootScope, "v", SignalKind::Logic, 4);
    Signal& s = st.mutableSignal(id);
    s.appendLogic(0, "0000");
    s.appendLogic(10, "0001");  // bit 0 changes
    s.appendLogic(20, "0011");  // bit 1 changes
    s.appendLogic(30, "0010");  // bit 0 changes
    s.appendLogic(40, "0010");  // nothing changes
    s.appendLogic(50, "10X0");  // bit 3 and bit 1 change

    REQUIRE(s.bitChanges(0) == std::vector<std::size_t>{0, 1, 3});
    REQUIRE(s.bitChanges(1) == std::vector<std::size_t>{0, 2, 5});
    REQUIRE(s.bitChanges(2) == std::vector<std::size_t>{0});
    REQUIRE(s.bitChanges(3) == std::vector<std::size_t>{0, 5});
    REQUIRE(s.bitChanges(4).empty());  // out of range

    const SignalId r = st.addSignal(kRootScope, "r", SignalKind::Real, 64);
    REQUIRE(st.signal(r).bitChanges(0).empty());
}

TEST_CASE("scopes can be found by dotted path") {
    WaveStore st;
    const ScopeId top = st.addScope(kRootScope, "tb");
    const ScopeId dut = st.addScope(top, "dut");
    REQUIRE(st.findScope("tb") == top);
    REQUIRE(st.findScope("tb.dut") == dut);
    REQUIRE(st.findScope("") == kRootScope);
    REQUIRE(st.findScope("tb.nothing") == kNoScope);
    const ScopeId late = st.addScope(dut, "core");  // the index follows additions
    REQUIRE(st.findScope("tb.dut.core") == late);
}
