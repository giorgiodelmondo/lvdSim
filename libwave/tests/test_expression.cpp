// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/expression.hpp>
#include <lvdwave/search.hpp>
#include <lvdwave/wave_space.hpp>

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace lvd::wave;

namespace {

/// tb.a, tb.b (1 bit), tb.bus[7:0], tb.cnt[15:0], tb.v (real), tb.state (text), all in a top scope "tb".
struct Fixture {
    std::shared_ptr<WaveStore> store = std::make_shared<WaveStore>();
    WaveSpace space;
    SignalId a, b, bus, cnt, v, state;

    Fixture() {
        const ScopeId tb = store->addScope(kRootScope, "tb");
        a = store->addSignal(tb, "a", SignalKind::Logic, 1);
        b = store->addSignal(tb, "b", SignalKind::Logic, 1);
        bus = store->addSignal(tb, "bus[7:0]", SignalKind::Logic, 8);
        cnt = store->addSignal(tb, "cnt[15:0]", SignalKind::Logic, 16);
        v = store->addSignal(tb, "v", SignalKind::Real, 1);
        state = store->addSignal(tb, "state", SignalKind::String, 1);
        space.setCurrent(store);
    }
    Signal& sig(SignalId id) { return store->mutableSignal(id); }

    std::unique_ptr<ExpressionSignal> make(std::string_view text) {
        auto r = ExpressionSignal::create(text, space.resolver());
        INFO(text << ": " << (r.ok() ? std::string() : r.error()));
        REQUIRE(r.ok());
        return std::move(r).value();
    }
    std::string error(std::string_view text) {
        auto r = Expression::compile(text, space.resolver());
        REQUIRE_FALSE(r.ok());
        return r.error();
    }
};

/// (time, value) pairs of a Logic signal.
std::vector<std::pair<Time, std::string>> changes(const Signal& s) {
    std::vector<std::pair<Time, std::string>> out;
    for (std::size_t i = 0; i < s.changeCount(); ++i)
        out.emplace_back(s.time(i), s.kind() == SignalKind::Logic ? s.bits(i) : std::string(s.text(i)));
    return out;
}

using C = std::vector<std::pair<Time, std::string>>;

} // namespace

TEST_CASE("expression: and, not, or over two scalars") {
    Fixture f;
    f.sig(f.a).appendLogic(0, "0");
    f.sig(f.b).appendLogic(0, "0");
    f.sig(f.a).appendLogic(10, "1");
    f.sig(f.b).appendLogic(20, "1");
    f.sig(f.a).appendLogic(30, "0");
    f.sig(f.b).appendLogic(40, "0");

    auto e = f.make("a and not b");
    CHECK(e->expression().describe() == "1 bit");
    CHECK(changes(e->signal()) == C{{0, "0"}, {10, "1"}, {20, "0"}});

    auto o = f.make("/tb/a or tb.b");
    CHECK(changes(o->signal()) == C{{0, "0"}, {10, "1"}, {40, "0"}});
    auto x = f.make("a ^ b");
    CHECK(changes(x->signal()) == C{{0, "0"}, {10, "1"}, {20, "0"}, {30, "1"}, {40, "0"}});
    auto n = f.make("a nand b");
    CHECK(changes(n->signal()) == C{{0, "1"}, {20, "0"}, {30, "1"}});
}

TEST_CASE("expression: unknown bits follow std_logic rules") {
    Fixture f;
    f.sig(f.a).appendLogic(0, "0");
    f.sig(f.b).appendLogic(0, "X");
    f.sig(f.a).appendLogic(10, "1");
    f.sig(f.a).appendLogic(20, "H");
    f.sig(f.b).appendLogic(30, "1");

    CHECK(changes(f.make("a and b")->signal()) == C{{0, "0"}, {10, "X"}, {30, "1"}});
    CHECK(changes(f.make("a or b")->signal()) == C{{0, "X"}, {10, "1"}});
    CHECK(changes(f.make("not b")->signal()) == C{{0, "X"}, {30, "0"}});
    // before its first change a signal reads as U
    Fixture g;
    g.sig(g.a).appendLogic(10, "1");
    g.sig(g.b).appendLogic(20, "1");
    CHECK(changes(g.make("a and b")->signal()) == C{{10, "U"}, {20, "1"}});
}

TEST_CASE("expression: comparing a vector with constants in every notation") {
    Fixture f;
    f.sig(f.bus).appendLogic(0, "00000000");
    f.sig(f.bus).appendLogic(10, "00111111");
    f.sig(f.bus).appendLogic(20, "0011111X");
    f.sig(f.bus).appendLogic(30, "00111111");
    f.sig(f.bus).appendLogic(40, "11111111");

    const C expected{{0, "0"}, {10, "1"}, {20, "X"}, {30, "1"}, {40, "0"}};
    for (const char* text :
         {"bus = 16#3F", "bus = 16#3F#", "bus == 0x3F", "bus = x\"3F\"", "bus = 63", "bus = \"00111111\"",
          "bus = 8'h3F", "bus = b\"0011_1111\"", "bus = 2#111111#", "16#3F# = /tb/bus"}) {
        INFO(text);
        CHECK(changes(f.make(text)->signal()) == expected);
    }
    CHECK(changes(f.make("bus /= 63")->signal()) == C{{0, "1"}, {10, "0"}, {20, "X"}, {30, "0"}, {40, "1"}});
    // exact comparison never gives X
    CHECK(changes(f.make("bus === 8'b0011111x")->signal()) == C{{0, "0"}, {20, "1"}, {30, "0"}});
    CHECK(changes(f.make("bus !== 63")->signal()) == C{{0, "1"}, {10, "0"}, {20, "1"}, {30, "0"}, {40, "1"}});
}

TEST_CASE("expression: relations, signed() and arithmetic") {
    Fixture f;
    f.sig(f.cnt).appendLogic(0, "0000000001100100");  // 100
    f.sig(f.cnt).appendLogic(10, "0000000001100101"); // 101
    f.sig(f.cnt).appendLogic(20, "0000000000000000");
    CHECK(changes(f.make("cnt > 100")->signal()) == C{{0, "0"}, {10, "1"}, {20, "0"}});
    CHECK(changes(f.make("cnt >= 100")->signal()) == C{{0, "1"}, {20, "0"}});
    CHECK(changes(f.make("cnt < 1")->signal()) == C{{0, "0"}, {20, "1"}});
    CHECK(changes(f.make("cnt <= 100")->signal()) == C{{0, "1"}, {10, "0"}, {20, "1"}});

    f.sig(f.bus).appendLogic(0, "11111111"); // 255 unsigned, -1 signed
    f.sig(f.bus).appendLogic(10, "00000001");
    CHECK(changes(f.make("bus > 1")->signal()) == C{{0, "1"}, {10, "0"}});
    CHECK(changes(f.make("signed(bus) < 0")->signal()) == C{{0, "1"}, {10, "0"}});
    CHECK(changes(f.make("signed(bus) = -1")->signal()) == C{{0, "1"}, {10, "0"}});
    CHECK(changes(f.make("signed(bus) > -2")->signal()) == C{{0, "1"}});

    auto plus = f.make("bus + 1");
    CHECK(plus->expression().describe() == "8 bit vector");
    CHECK(changes(plus->signal()) == C{{0, "00000000"}, {10, "00000010"}});
    CHECK(changes(f.make("bus - 2")->signal()) == C{{0, "11111101"}, {10, "11111111"}});
    CHECK(changes(f.make("-bus")->signal()) == C{{0, "00000001"}, {10, "11111111"}});
}

TEST_CASE("expression: slices, bits and concatenation") {
    Fixture f;
    f.sig(f.bus).appendLogic(0, "10100101");
    f.sig(f.a).appendLogic(0, "1");
    f.sig(f.b).appendLogic(0, "0");

    auto hi = f.make("bus(7 downto 4)");
    CHECK(hi->expression().describe() == "4 bit vector");
    CHECK(changes(hi->signal()) == C{{0, "1010"}});
    CHECK(changes(f.make("bus[3:0]")->signal()) == C{{0, "0101"}});
    CHECK(changes(f.make("bus(4 to 7)")->signal()) == C{{0, "1010"}});
    CHECK(changes(f.make("bus(0)")->signal()) == C{{0, "1"}});
    CHECK(changes(f.make("bus[6]")->signal()) == C{{0, "0"}});
    CHECK(changes(f.make("a & b")->signal()) == C{{0, "10"}});
    CHECK(changes(f.make("{a, bus[1:0], b}")->signal()) == C{{0, "1010"}});
    CHECK(changes(f.make("(bus(7 downto 4) & a) = 2#10101#")->signal()) == C{{0, "1"}});
    CHECK(changes(f.make("bus and x\"0F\"")->signal()) == C{{0, "00000101"}});
    CHECK(changes(f.make("~bus")->signal()) == C{{0, "01011010"}});
}

TEST_CASE("expression: logical operators reduce to one bit") {
    Fixture f;
    f.sig(f.bus).appendLogic(0, "00000000");
    f.sig(f.a).appendLogic(0, "1");
    f.sig(f.bus).appendLogic(10, "00010000");
    f.sig(f.bus).appendLogic(20, "0000000X");
    CHECK(changes(f.make("bus && a")->signal()) == C{{0, "0"}, {10, "1"}, {20, "X"}});
    CHECK(changes(f.make("!bus")->signal()) == C{{0, "1"}, {10, "0"}, {20, "X"}});
    CHECK(changes(f.make("bus || !a")->signal()) == C{{0, "0"}, {10, "1"}, {20, "X"}});
    CHECK(changes(f.make("true and a")->signal()) == C{{0, "1"}});
}

TEST_CASE("expression: reals and text") {
    Fixture f;
    f.sig(f.v).appendReal(0, 1.0);
    f.sig(f.v).appendReal(10, 2.0);
    f.sig(f.v).appendReal(20, 1.5);
    CHECK(changes(f.make("v > 1.5")->signal()) == C{{0, "0"}, {10, "1"}, {20, "0"}});
    auto sum = f.make("v + 1");
    REQUIRE(sum->signal().kind() == SignalKind::Real);
    REQUIRE(sum->signal().changeCount() == 3);
    CHECK(sum->signal().real(1) == 3.0);

    f.sig(f.state).appendText(0, "idle");
    f.sig(f.state).appendText(10, "run");
    f.sig(f.state).appendText(20, "idle");
    CHECK(changes(f.make("state = IDLE")->signal()) == C{{0, "1"}, {10, "0"}, {20, "1"}});
    CHECK(changes(f.make("state /= \"run\"")->signal()) == C{{0, "1"}, {10, "0"}, {20, "1"}});
    auto copy = f.make("state");
    CHECK(copy->signal().kind() == SignalKind::String);
    CHECK(changes(copy->signal()) == C{{0, "idle"}, {10, "run"}, {20, "idle"}});
}

TEST_CASE("expression: errors name the column and the problem") {
    Fixture f;
    CHECK(f.error("a and nosuch") == "col 7: unknown signal 'nosuch'");
    CHECK(f.error("a and") == "col 6: expression incomplete");
    CHECK(f.error("(a") == "col 3: ')' expected");
    CHECK(f.error("a b") == "col 3: unexpected 'b'");
    CHECK(f.error("bus(8)").find("outside the 8 bit value") != std::string::npos);
    CHECK(f.error("bus(2 downto 5)") == "col 4: empty range");
    CHECK(f.error("state + 1") == "col 7: cannot add or subtract text");
    CHECK(f.error("state = a") == "col 7: text can only be compared with text");
    CHECK(f.error("v and a") == "col 3: 'and' needs bit vectors");
    CHECK(f.error("1 and 0") == "the expression reads no signal");
    CHECK(f.error("") == "empty expression");
    CHECK(f.error("a # b") == "col 3: unexpected character '#'");
    CHECK(f.error("bus = 16#3G") == "col 7: bad digit 'G'");
    CHECK(f.error("/tb/*") == "col 1: wildcards are not allowed in '/tb/*'");
    CHECK(f.error("other:/tb/a") == "col 1: no dataset named 'other'");
    CHECK(f.error("bus = \"12\"").find("not a bit string") != std::string::npos);
    CHECK(f.error("bus < state").find("text") != std::string::npos);
}

TEST_CASE("expression: delta cycles at one time are one step") {
    Fixture f;
    f.sig(f.a).appendLogic(0, "0");
    f.sig(f.b).appendLogic(0, "1");
    f.sig(f.a).appendLogic(10, "1");
    f.sig(f.a).appendLogic(10, "0"); // a glitch inside one time step
    f.sig(f.b).appendLogic(10, "1");
    CHECK(changes(f.make("a and b")->signal()) == C{{0, "0"}});
}

TEST_CASE("expression: incremental update follows a growing run") {
    Fixture f;
    auto e = f.make("a xor b");
    CHECK(e->signal().changeCount() == 0);

    f.sig(f.a).appendLogic(0, "0");
    f.sig(f.b).appendLogic(0, "0");
    CHECK(e->update() == 1);
    f.sig(f.a).appendLogic(10, "1");
    CHECK(e->update() == 1);
    // more delta cycles of the last evaluated time arrive in the next chunk
    f.sig(f.b).appendLogic(10, "1");
    CHECK(e->update() == 1);
    CHECK(changes(e->signal()) == C{{0, "0"}, {10, "1"}, {10, "0"}});
    // the value at 10 is the last change at 10
    CHECK(e->signal().holdIndex(10) == 2);
    CHECK(e->update() == 0);

    // random input, chunked updates give the same values as one evaluation over everything
    Fixture g;
    std::mt19937 rng(7);
    auto inc = g.make("(bus(3 downto 0) = 5) or (a and not b)");
    Time t = 0;
    for (int chunk = 0; chunk < 50; ++chunk) {
        for (int k = 0; k < 40; ++k) {
            t += rng() % 3; // repeated times make delta cycles
            const unsigned which = rng() % 3;
            if (which == 0)
                g.sig(g.a).appendLogic(t, rng() % 2 ? "1" : "0");
            else if (which == 1)
                g.sig(g.b).appendLogic(t, rng() % 5 == 0 ? "X" : rng() % 2 ? "1" : "0");
            else
                g.sig(g.bus).appendLogic(t, std::string("0000") + (rng() % 2 ? "0101" : "0100"));
        }
        inc->update();
    }
    auto full = g.make("(bus(3 downto 0) = 5) or (a and not b)");
    // compare the value in effect at every time step
    for (Time q = 0; q <= t; ++q) {
        const std::size_t i = inc->signal().holdIndex(q), j = full->signal().holdIndex(q);
        REQUIRE((i == kNoIndex) == (j == kNoIndex));
        if (i != kNoIndex)
            REQUIRE(inc->signal().bits(i) == full->signal().bits(j));
    }
}

TEST_CASE("expression: dataset names and searching the result") {
    Fixture f;
    auto ref = std::make_shared<WaveStore>();
    const ScopeId tb = ref->addScope(kRootScope, "tb");
    Signal& ra = ref->mutableSignal(ref->addSignal(tb, "a", SignalKind::Logic, 1));
    ra.appendLogic(0, "0");
    ra.appendLogic(10, "1");
    ra.appendLogic(30, "0");
    REQUIRE(f.space.addDataset("ref", ref).empty());
    f.sig(f.a).appendLogic(0, "0");
    f.sig(f.a).appendLogic(12, "1");
    f.sig(f.a).appendLogic(30, "0");

    auto d = f.make("ref:/tb/a xor /tb/a");
    CHECK(changes(d->signal()) == C{{0, "0"}, {10, "1"}, {12, "0"}});
    // the result is a normal signal for the searches
    CHECK(findEdge(SearchTarget{&d->signal(), -1}, std::nullopt, SearchDirection::Forward,
                   EdgeKind::Rising) == Time{10});
    CHECK(f.make("ref:tb.a = a")->signal().changeCount() == 3);
}
