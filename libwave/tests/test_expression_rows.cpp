// SPDX-License-Identifier: GPL-3.0-or-later
// Expression and compare rows in the display model, the layout format and the add wave commands.
#include <lvdwave/display_edit.hpp>
#include <lvdwave/display_model.hpp>
#include <lvdwave/wave_commands.hpp>
#include <lvdwave/wave_space.hpp>

#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> design() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId tb = st->addScope(kRootScope, "tb");
    const ScopeId dut = st->addScope(tb, "dut");
    st->addSignal(tb, "clk", SignalKind::Logic, 1);
    st->addSignal(dut, "a", SignalKind::Logic, 1);
    st->addSignal(dut, "b", SignalKind::Logic, 1);
    st->addSignal(dut, "q[7:0]", SignalKind::Logic, 8);
    return st;
}

std::vector<std::string> words(std::string_view cmd) {
    return parseTclList(cmd);
}

} // namespace

TEST_CASE("expression and compare items round-trip through the layout format") {
    DisplayModel m;
    DisplayItem e = DisplayModel::makeExpression("both", "a and \"01\" = b", Radix::Binary);
    e.color = 0x40e040;
    e.analog.enabled = true;
    e.analog.heightRows = 3;
    m.root().children.push_back(e);
    DisplayItem g = DisplayModel::makeGroup("cmp");
    g.children.push_back(DisplayModel::makeCompare("diff q", "ref:tb.dut.q[7:0]", "tb.dut.q[7:0]", 2000));
    m.root().children.push_back(g);

    const std::string text = m.serialize();
    CHECK(text.find("expr \"both\" \"a and \\\"01\\\" = b\" radix=binary color=#40e040") !=
          std::string::npos);
    CHECK(text.find(
              "compare \"diff q\" \"ref:tb.dut.q[7:0]\" \"tb.dut.q[7:0]\" tolerance=2000 color=#ff6060") !=
          std::string::npos);
    auto back = DisplayModel::parse(text);
    REQUIRE(back.ok());
    const DisplayItem& e2 = back.value().root().children.at(0);
    CHECK(e2.kind == ItemKind::Expression);
    CHECK(e2.label == "both");
    CHECK(e2.expr == "a and \"01\" = b");
    CHECK(e2.radix == Radix::Binary);
    CHECK(e2.color == 0x40e040u);
    CHECK(e2.analog.enabled);
    CHECK(e2.analog.heightRows == 3);
    const DisplayItem& c2 = back.value().root().children.at(1).children.at(0);
    CHECK(c2.kind == ItemKind::Compare);
    CHECK(c2.label == "diff q");
    REQUIRE(c2.members.size() == 2);
    CHECK(c2.members[0].path == "ref:tb.dut.q[7:0]");
    CHECK(c2.members[1].path == "tb.dut.q[7:0]");
    CHECK(c2.tolerance == 2000);
    CHECK(c2.color == 0xff6060u);
    CHECK(back.value().serialize() == text);
}

TEST_CASE("an expression row is named by its label, or by its text") {
    DisplayItem e = DisplayModel::makeExpression("", "a or b");
    CHECK(e.displayName() == "a or b");
    e.label = "either";
    CHECK(e.displayName() == "either");

    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeExpression("x", "a"));
    const ItemId id = m.root().children[0].id;
    CHECK_FALSE(renameItem(m, id, "").ok());
    REQUIRE(renameItem(m, id, "y").ok());
    CHECK(m.root().children[0].label == "y");
}

TEST_CASE("add wave -expr and -compare") {
    auto st = design();
    WaveSpace sp;
    sp.setCurrent(st);
    auto ref = design();
    REQUIRE(sp.addDataset("ref", ref).empty());

    auto req = parseAddWave(words("-expr {a and not b} -label en -radix binary"));
    REQUIRE(req.ok());
    REQUIRE(req.value().expr);
    CHECK(*req.value().expr == "a and not b");
    DisplayModel m;
    CHECK(applyAddWave(m, sp, req.value()) == 1);
    CHECK(m.root().children[0].kind == ItemKind::Expression);
    CHECK(m.root().children[0].label == "en");
    CHECK(m.root().children[0].radix == Radix::Binary);

    req = parseAddWave(words("-expr {cnt > 100}"));
    REQUIRE(req.ok());
    CHECK(applyAddWave(m, sp, req.value()) == 1);
    CHECK(m.root().children[1].label == "cnt > 100"); // not checked here: the window shows the error

    CHECK_FALSE(parseAddWave(words("-expr")).ok());
    CHECK_FALSE(parseAddWave(words("-compare /tb/dut/a")).ok());

    req = parseAddWave(words("-compare -tolerance {2 ns} ref:/tb/dut/* /tb/dut/*"));
    REQUIRE(req.ok());
    std::string why;
    CHECK(applyAddWave(m, sp, req.value(), &why) == 3);
    const DisplayItem& c = m.root().children[2];
    CHECK(c.kind == ItemKind::Compare);
    CHECK(c.members[0].path == "ref:tb.dut.a");
    CHECK(c.members[1].path == "tb.dut.a");
    CHECK(c.tolerance == 2000000);
    CHECK(c.label == "diff /tb/dut/a");

    req = parseAddWave(words("-compare ref:/tb/dut/* /tb/*"));
    REQUIRE(req.ok());
    CHECK(applyAddWave(m, sp, req.value(), &why) == 0);
    CHECK(why == "the reference matches 3 signals, the test 1");
    req = parseAddWave(words("-compare -tolerance soon ref:/tb/clk /tb/clk"));
    REQUIRE(req.ok());
    CHECK(applyAddWave(m, sp, req.value(), &why) == 0);
    CHECK(why == "bad tolerance 'soon'");

    // dataset signals as plain rows
    req = parseAddWave(words("ref:/tb/dut/q"));
    REQUIRE(req.ok());
    CHECK(applyAddWave(m, sp, req.value()) == 1);
    CHECK(m.root().children.back().path == "ref:tb.dut.q[7:0]");
    req = parseAddWave(words("nosuch:/tb/dut/q"));
    REQUIRE(req.ok());
    CHECK(applyAddWave(m, sp, req.value()) == 0);
}

TEST_CASE("wave scripts keep expression, compare and dataset rows") {
    auto st = design();
    WaveSpace sp;
    sp.setCurrent(st);
    REQUIRE(sp.addDataset("ref", design()).empty());

    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeSignal("ref:tb.dut.q[7:0]", Radix::Unsigned));
    DisplayItem e = DisplayModel::makeExpression("en", "a and {b}", Radix::Binary);
    m.root().children.push_back(e);
    DisplayItem g = DisplayModel::makeGroup("checks");
    g.children.push_back(DisplayModel::makeCompare("q differs", "ref:tb.dut.q[7:0]", "tb.dut.q[7:0]", 5));
    m.root().children.push_back(g);
    DisplayItem bus =
        DisplayModel::makeBus("mix", {BusMember{"ref:tb.dut.a"}, BusMember{"tb.dut.q[7:0]", 0}});
    m.root().children.push_back(bus);

    const std::string script = exportDoScript(m);
    CHECK(script.find("add wave -radix unsigned {ref:/tb/dut/q[7:0]}") != std::string::npos);
    CHECK(script.find("add wave -radix binary -label en -expr {a and {b}}") != std::string::npos);
    CHECK(script.find("add wave -group checks -compare -color #ff6060 -label {q differs} -tolerance 5 "
                      "{ref:/tb/dut/q[7:0]} {/tb/dut/q[7:0]}") != std::string::npos);
    CHECK(script.find("ref:/tb/dut/a") != std::string::npos);

    DisplayModel back;
    const ImportResult r = importDoScript(script, back, sp);
    CHECK(r.errors.empty());
    CHECK(r.rowsAdded == 4);
    CHECK(back.serialize() == m.serialize());
}
