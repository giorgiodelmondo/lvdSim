// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/compare.hpp>
#include <lvdwave/wave_space.hpp>

#include <catch2/catch_test_macros.hpp>
#include <random>
#include <vector>

using namespace lvd::wave;

namespace {

constexpr auto Fwd = SearchDirection::Forward;
constexpr auto Bwd = SearchDirection::Backward;

Signal logic(std::uint32_t width, std::vector<std::pair<Time, const char*>> changes) {
    Signal s("s", kRootScope, SignalKind::Logic, width);
    for (const auto& [t, v] : changes)
        s.appendLogic(t, v);
    return s;
}

using I = std::vector<DiffInterval>;

} // namespace

TEST_CASE("compare: identical signals have no difference") {
    const Signal a = logic(1, {{0, "0"}, {10, "1"}, {20, "0"}});
    const Signal b = logic(1, {{0, "0"}, {10, "1"}, {20, "0"}});
    SignalCompare c(a, b);
    CHECK(c.update(100) == 0);
    CHECK(c.count() == 0);
    REQUIRE(c.differences().changeCount() == 1);
    CHECK(c.differences().bits(0) == "0");
}

TEST_CASE("compare: intervals, the open difference at the end and the difference row") {
    const Signal ref = logic(4, {{0, "0000"}, {10, "0001"}, {30, "0010"}, {50, "0011"}});
    const Signal test = logic(4, {{0, "0000"}, {10, "0001"}, {20, "0101"}, {30, "0010"}, {50, "0111"}});
    SignalCompare c(ref, test);
    CHECK(c.update(60) == 2);
    CHECK(c.intervals() == I{{20, 30}});
    CHECK(c.openSince() == Time{50});
    CHECK(c.count() == 2);
    const Signal& d = c.differences();
    CHECK(d.bits(d.holdIndex(15)) == "0");
    CHECK(d.bits(d.holdIndex(20)) == "1");
    CHECK(d.bits(d.holdIndex(29)) == "1");
    CHECK(d.bits(d.holdIndex(30)) == "0");
    CHECK(d.bits(d.holdIndex(55)) == "1");

    // navigation over the differences, as the wave window does it
    CHECK(findDifference(c, std::nullopt, Fwd) == Time{20});
    CHECK(findDifference(c, Time{20}, Fwd) == Time{50});
    CHECK(findDifference(c, Time{50}, Fwd) == std::nullopt);
    CHECK(findDifference(c, Time{50}, Bwd) == Time{20});
    CHECK(findDifference(c, std::nullopt, Bwd) == Time{50});
}

TEST_CASE("compare: value rules") {
    const Signal zero = logic(2, {{0, "00"}});
    const Signal weak = logic(2, {{0, "LL"}});
    const Signal unknown = logic(2, {{0, "0X"}});
    const Signal narrow = logic(1, {{0, "0"}});
    CHECK(sameValue(zero, 0, weak, 0));
    CHECK_FALSE(sameValue(zero, 0, unknown, 0));
    CHECK(sameValue(unknown, 0, unknown, 0));
    CHECK(sameValue(zero, 0, narrow, 0)); // zero extension
    CHECK(sameValue(zero, kNoIndex, narrow, kNoIndex));
    CHECK_FALSE(sameValue(zero, 0, narrow, kNoIndex));

    Signal r1("r", kRootScope, SignalKind::Real, 1), r2("r", kRootScope, SignalKind::Real, 1);
    r1.appendReal(0, 1.0);
    r2.appendReal(0, 1.05);
    CHECK_FALSE(sameValue(r1, 0, r2, 0));
    CHECK(sameValue(r1, 0, r2, 0, 0.1));

    Signal t1("t", kRootScope, SignalKind::String, 1), t2("t", kRootScope, SignalKind::String, 1);
    t1.appendText(0, "idle");
    t2.appendText(0, "idle");
    CHECK(sameValue(t1, 0, t2, 0));
    CHECK_FALSE(sameValue(t1, 0, r1, 0));
}

TEST_CASE("compare: a time tolerance hides edges that moved a little") {
    const Signal ref = logic(1, {{0, "0"}, {100, "1"}, {200, "0"}, {300, "1"}});
    const Signal test = logic(1, {{0, "0"}, {102, "1"}, {200, "0"}, {310, "1"}});
    {
        SignalCompare c(ref, test);
        c.update(400);
        CHECK(c.intervals() == I{{100, 102}, {300, 310}});
    }
    {
        CompareOptions o;
        o.tolerance = 2;
        SignalCompare c(ref, test, o);
        c.update(400);
        CHECK(c.intervals() == I{{300, 310}});
        const Signal& d = c.differences();
        CHECK(d.bits(d.holdIndex(101)) == "0");
        CHECK(d.bits(d.holdIndex(305)) == "1");
    }
    {
        CompareOptions o;
        o.tolerance = 10;
        SignalCompare c(ref, test, o);
        c.update(400);
        CHECK(c.count() == 0);
    }
}

TEST_CASE("compare: incremental updates match one comparison over everything") {
    std::mt19937 rng(3);
    Signal ref("r", kRootScope, SignalKind::Logic, 3), test("t", kRootScope, SignalKind::Logic, 3);
    CompareOptions o;
    o.tolerance = 2;
    SignalCompare inc(ref, test, o);
    const char* values[] = {"000", "001", "010", "0X1"};
    Time t = 0;
    for (int chunk = 0; chunk < 60; ++chunk) {
        for (int k = 0; k < 30; ++k) {
            t += rng() % 4;
            (rng() % 2 ? ref : test).appendLogic(t, values[rng() % 4]);
            if (rng() % 3 == 0) // keep them mostly equal so that intervals open and close
                (rng() % 2 ? ref : test).appendLogic(t, values[0]);
        }
        inc.update(t);
    }
    SignalCompare full(ref, test, o);
    full.update(t);
    CHECK(inc.intervals() == full.intervals());
    CHECK(inc.openSince() == full.openSince());
    for (Time q = 0; q <= t; ++q) {
        const std::size_t i = inc.differences().holdIndex(q), j = full.differences().holdIndex(q);
        REQUIRE((i == kNoIndex) == (j == kNoIndex));
        if (i != kNoIndex)
            REQUIRE(inc.differences().bits(i) == full.differences().bits(j));
    }
}

TEST_CASE("compare: later delta cycles of the last time step can cancel a difference") {
    Signal ref("r", kRootScope, SignalKind::Logic, 1), test("t", kRootScope, SignalKind::Logic, 1);
    ref.appendLogic(0, "0");
    test.appendLogic(0, "0");
    ref.appendLogic(10, "1");
    SignalCompare c(ref, test);
    c.update(10);
    CHECK(c.openSince() == Time{10});
    test.appendLogic(10, "1"); // same time, next chunk
    c.update(20);
    CHECK(c.count() == 0);
    const Signal& d = c.differences();
    CHECK(d.bits(d.holdIndex(10)) == "0");
    CHECK_FALSE(findDifference(c, std::nullopt, Fwd).has_value());
}

TEST_CASE("compare: whole stores") {
    WaveStore ref, test;
    for (WaveStore* st : {&ref, &test}) {
        const ScopeId tb = st->addScope(kRootScope, "tb");
        const ScopeId dut = st->addScope(tb, "dut");
        const SignalId clk = st->addSignal(tb, "clk", SignalKind::Logic, 1);
        st->addAlias(dut, "clk", clk); // a port: same data, compared once
        st->addSignal(dut, "q[3:0]", SignalKind::Logic, 4);
        st->extendTimeRange(0);
    }
    ref.addSignal(ref.findScope("tb.dut"), "old", SignalKind::Logic, 1);
    test.addSignal(test.findScope("tb.dut"), "new", SignalKind::Logic, 1);
    ref.addSignal(ref.findScope("tb"), "mode", SignalKind::Logic, 1);
    test.addSignal(test.findScope("tb"), "mode", SignalKind::String, 1);
    for (WaveStore* st : {&ref, &test}) {
        Signal& clk = st->mutableSignal(st->findSignal("tb.clk"));
        for (Time t = 0; t <= 100; t += 10)
            clk.appendLogic(t, (t / 10) % 2 ? "1" : "0");
        st->mutableSignal(st->findSignal("tb.dut.q[3:0]")).appendLogic(0, "0000");
    }
    test.mutableSignal(test.findSignal("tb.dut.q[3:0]")).appendLogic(40, "0001");
    test.mutableSignal(test.findSignal("tb.dut.q[3:0]")).appendLogic(60, "0000");
    ref.extendTimeRange(100);
    test.extendTimeRange(120);

    const StoreDiff d = compareStores(ref, test);
    CHECK(d.until == 100);
    CHECK(d.compared == 2); // tb.clk (and its alias tb.dut.clk) and tb.dut.q
    REQUIRE(d.differing.size() == 1);
    CHECK(d.differing[0].path == "tb.dut.q[3:0]");
    CHECK(d.differing[0].count == 1);
    CHECK(d.differing[0].first == 40);
    CHECK(d.total == 1);
    CHECK(d.onlyInRef == std::vector<std::string>{"tb.dut.old"});
    CHECK(d.onlyInTest == std::vector<std::string>{"tb.dut.new"});
    CHECK(d.kindMismatch == std::vector<std::string>{"tb.mode"});

    const StoreDiff scoped = compareStores(ref, test, {}, "tb.dut");
    CHECK(scoped.compared == 2);
    CHECK(scoped.kindMismatch.empty());
    CHECK(compareStores(ref, test, {}, "tb.du").compared == 0); // a scope prefix ends at a dot
}

TEST_CASE("compare: durations") {
    CHECK(parseDuration("2 ns", -15) == Time{2000000});
    CHECK(parseDuration("2ns", -12) == Time{2000});
    CHECK(parseDuration("1.5us", -9) == Time{1500});
    CHECK(parseDuration("100", -15) == Time{100});
    CHECK(parseDuration("0", -15) == Time{0});
    CHECK_FALSE(parseDuration("-1 ns", -15).has_value());
    CHECK_FALSE(parseDuration("2 parsecs", -15).has_value());
    CHECK_FALSE(parseDuration("", -15).has_value());
}

TEST_CASE("wave space: names, datasets and time units") {
    CHECK(WaveSpace::validDatasetName("ref"));
    CHECK(WaveSpace::validDatasetName("run_2"));
    CHECK_FALSE(WaveSpace::validDatasetName("2run"));
    CHECK_FALSE(WaveSpace::validDatasetName(""));
    CHECK_FALSE(WaveSpace::validDatasetName("a-b"));
    CHECK(WaveSpace::split("ref:tb.x").dataset == "ref");
    CHECK(WaveSpace::split("ref:tb.x").path == "tb.x");
    CHECK(WaveSpace::split("tb.x").dataset.empty());
    CHECK(WaveSpace::split("/a:b").dataset.empty()); // not a dataset name
    CHECK(WaveSpace::join("ref", "tb.x") == "ref:tb.x");
    CHECK(WaveSpace::join("", "tb.x") == "tb.x");

    auto cur = std::make_shared<WaveStore>();
    const ScopeId tb = cur->addScope(kRootScope, "tb");
    const ScopeId dut = cur->addScope(tb, "dut");
    const SignalId x = cur->addSignal(dut, "x[3:0]", SignalKind::Logic, 4);
    cur->addSignal(tb, "y", SignalKind::Logic, 1);
    cur->addAlias(tb, "x", x);
    WaveSpace sp;
    sp.setCurrent(cur);
    std::string err;
    const Signal* sx = &cur->signal(x);
    CHECK(sp.findName("/tb/dut/x", err) == sx);
    CHECK(sp.findName("tb.dut.x[3:0]", err) == sx);
    CHECK(sp.findName("tb.dut.x", err) == sx);
    CHECK(sp.findName("dut/x", err) == sx);
    CHECK(sp.findName("x", err) == sx); // tb.x is a port of the same signal
    CHECK(sp.findName("nope", err) == nullptr);
    CHECK(err == "unknown signal 'nope'");
    CHECK(sp.findPath("tb.dut.x[3:0]") == sx);

    auto ps = std::make_shared<WaveStore>();
    ps->setTimescaleExponent(-12);
    const ScopeId ptb = ps->addScope(kRootScope, "tb");
    Signal& py = ps->mutableSignal(ps->addSignal(ptb, "y", SignalKind::Logic, 1));
    py.appendLogic(0, "0");
    py.appendLogic(5, "1");
    ps->extendTimeRange(0);
    ps->extendTimeRange(7);
    CHECK_FALSE(sp.addDataset("ref", ps).empty()); // ps against fs
    CHECK_FALSE(sp.addDataset("1ref", cur).empty());
    auto fine = retimeStore(*ps, -15);
    REQUIRE(fine.ok());
    CHECK(fine.value().timescaleExponent() == -15);
    CHECK(fine.value().endTime() == 7000);
    CHECK(fine.value().signal(fine.value().findSignal("tb.y")).time(1) == 5000);
    auto coarse = retimeStore(fine.value(), -9);
    REQUIRE(coarse.ok());
    CHECK(coarse.value().signal(coarse.value().findSignal("tb.y")).time(1) == 0); // 5 ps rounds down to 0 ns
    CHECK(coarse.value().signal(coarse.value().findSignal("tb.y")).holdIndex(0) == 1);

    REQUIRE(sp.addDataset("ref", std::make_shared<WaveStore>(std::move(fine).value())).empty());
    CHECK(sp.datasetNames() == std::vector<std::string>{"ref"});
    CHECK(sp.findName("ref:/tb/y", err) != nullptr);
    CHECK(sp.findName("ref:y", err) != nullptr);
    CHECK(sp.findPath("ref:tb.y") == sp.findName("ref:y", err));
    CHECK(sp.expandPattern("ref:/tb/*", false) == std::vector<std::string>{"ref:tb.y"});
    CHECK(sp.endTime() == 7000);
    CHECK(sp.endTimeOf("ref:tb.y") == 7000);
    CHECK(sp.endTimeOf("tb.y") == 0);
    CHECK(sp.removeDataset("ref"));
    CHECK(sp.findName("ref:y", err) == nullptr);
}
