// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/list_index.hpp>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <random>

using namespace lvd::wave;

namespace {

std::vector<Time> brute(const WaveStore& st, const std::vector<SignalId>& ids) {
    std::vector<Time> t;
    for (SignalId id : ids) {
        const Signal& s = st.signal(id);
        for (std::size_t i = 0; i < s.changeCount(); ++i)
            t.push_back(s.time(i));
    }
    std::sort(t.begin(), t.end());
    t.erase(std::unique(t.begin(), t.end()), t.end());
    return t;
}

} // namespace

TEST_CASE("list rows: one per time step of any listed signal") {
    WaveStore st;
    const SignalId a = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
    const SignalId b = st.addSignal(kRootScope, "b", SignalKind::Logic, 4);
    const SignalId c = st.addAlias(kRootScope, "c", a);
    st.mutableSignal(a).appendLogic(0, "0");
    st.mutableSignal(a).appendLogic(10, "1");
    st.mutableSignal(a).appendLogic(10, "0"); // delta at 10: one row
    st.mutableSignal(b).appendLogic(5, "0001");
    st.mutableSignal(b).appendLogic(10, "0010");
    st.mutableSignal(b).appendLogic(30, "0011");
    const ListIndex li(st, {a, b, c});
    REQUIRE(li.rowCount() == 4);
    CHECK(li.rowTime(0) == 0);
    CHECK(li.rowTime(1) == 5);
    CHECK(li.rowTime(2) == 10);
    CHECK(li.rowTime(3) == 30);
    CHECK(li.rowTime(1) == 5); // backwards again
    CHECK(li.rowAtOrBefore(9) == 1);
    CHECK(li.rowAtOrBefore(10) == 2);
    CHECK(li.rowAtOrBefore(1000) == 3);

    const ListIndex none(st, {});
    CHECK(none.rowCount() == 0);
    CHECK(none.rowAtOrBefore(10) == kNoIndex);
}

TEST_CASE("list rows: trigger signal and strobe") {
    WaveStore st;
    const SignalId clk = st.addSignal(kRootScope, "clk", SignalKind::Logic, 1);
    const SignalId d = st.addSignal(kRootScope, "d", SignalKind::Logic, 8);
    for (Time t = 0; t < 100; t += 5)
        st.mutableSignal(clk).appendLogic(t, (t / 5) % 2 ? "1" : "0");
    for (Time t = 0; t < 100; t += 3)
        st.mutableSignal(d).appendLogic(t, "00000001");
    st.extendTimeRange(0);
    st.extendTimeRange(99);

    ListOptions trig;
    trig.trigger = ListTrigger::Signal;
    trig.triggerSignal = clk;
    const ListIndex t(st, {d}, trig);
    REQUIRE(t.rowCount() == 20);
    CHECK(t.rowTime(7) == 35);

    ListOptions strobe;
    strobe.trigger = ListTrigger::Strobe;
    strobe.strobeStart = 2;
    strobe.strobePeriod = 10;
    const ListIndex s(st, {d}, strobe);
    REQUIRE(s.rowCount() == 10); // 2, 12, ..., 92
    CHECK(s.rowTime(9) == 92);
    CHECK(s.rowAtOrBefore(50) == 4);
    CHECK(s.rowAtOrBefore(1) == kNoIndex);
}

TEST_CASE("list rows: random access over many checkpoints matches a full merge") {
    WaveStore st;
    std::mt19937_64 rng(11);
    std::vector<SignalId> ids;
    for (int k = 0; k < 5; ++k)
        ids.push_back(st.addSignal(kRootScope, "s" + std::to_string(k), SignalKind::Logic, 1));
    for (SignalId id : ids) {
        Time t = 0;
        for (int i = 0; i < 4000; ++i) {
            t += rng() % 7; // 0: a delta at the same time
            st.mutableSignal(id).appendLogic(t, (i & 1) ? "1" : "0");
        }
    }
    const auto want = brute(st, ids);
    const ListIndex li(st, ids);
    REQUIRE(li.rowCount() == want.size());
    REQUIRE(want.size() > 3 * ListIndex::kCheckpointEvery);
    for (std::size_t r = 0; r < want.size(); r += 1)
        REQUIRE(li.rowTime(r) == want[r]);
    std::uniform_int_distribution<std::size_t> pick(0, want.size() - 1);
    for (int k = 0; k < 2000; ++k) {
        const std::size_t r = pick(rng);
        REQUIRE(li.rowTime(r) == want[r]);
        const Time t = want[r] + (rng() % 2);
        const auto it = std::upper_bound(want.begin(), want.end(), t);
        REQUIRE(li.rowAtOrBefore(t) == static_cast<std::size_t>(it - want.begin()) - 1);
    }
}

TEST_CASE("list rows: live growth, including deltas appended to the last row") {
    WaveStore st;
    const SignalId a = st.addSignal(kRootScope, "a", SignalKind::Logic, 1);
    const SignalId b = st.addSignal(kRootScope, "b", SignalKind::Logic, 1);
    ListIndex li(st, {a, b});
    REQUIRE(li.rowCount() == 0);
    Signal& sa = st.mutableSignal(a);
    Signal& sb = st.mutableSignal(b);
    std::vector<Time> want;
    Time t = 0;
    for (int round = 0; round < 3000; ++round) {
        sa.appendLogic(t, "1");
        if (round % 3 == 0)
            sb.appendLogic(t, "0");
        if (want.empty() || want.back() != t)
            want.push_back(t);
        if (round % 5 == 0) {
            const std::size_t before = li.rowCount();
            REQUIRE(li.update() == want.size() - before);
            REQUIRE(li.rowCount() == want.size());
            REQUIRE(li.rowTime(li.rowCount() - 1) == want.back());
        }
        if (round % 4 != 0)
            t += 1 + round % 3; // otherwise the next round adds a delta at the same time
    }
    li.update();
    REQUIRE(li.rowCount() == want.size());
    for (std::size_t r = 0; r < want.size(); r += 13)
        REQUIRE(li.rowTime(r) == want[r]);
}
