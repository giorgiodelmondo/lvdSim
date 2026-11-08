// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/wave_commands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <lvdwave/display_edit.hpp>

using namespace lvd::wave;
using Words = std::vector<std::string>;

namespace {

// tb
//   clk, rst, data[2]  (signals)
//   dut
//     count, valid
//     core
//       state
WaveStore makeStore() {
    WaveStore st;
    const ScopeId tb = st.addScope(kRootScope, "tb");
    st.addSignal(tb, "clk", SignalKind::Logic, 1);
    st.addSignal(tb, "rst", SignalKind::Logic, 1);
    st.addSignal(tb, "data[0]", SignalKind::Logic, 1);
    st.addSignal(tb, "data[1]", SignalKind::Logic, 1);
    const ScopeId dut = st.addScope(tb, "dut");
    st.addSignal(dut, "count", SignalKind::Logic, 8);
    st.addSignal(dut, "valid", SignalKind::Logic, 1);
    const ScopeId core = st.addScope(dut, "core");
    st.addSignal(core, "state", SignalKind::String, 0);
    const ScopeId pkg = st.addScope(kRootScope, "some_pkg");
    st.addSignal(pkg, "pkgsig", SignalKind::Logic, 1);
    return st;
}

} // namespace

TEST_CASE("Tcl lists are split like Tcl does") {
    REQUIRE(parseTclList("a b c") == Words{"a", "b", "c"});
    REQUIRE(parseTclList("  a   {b c}  \"d e\"  ") == Words{"a", "b c", "d e"});
    REQUIRE(parseTclList("{a {nested b} c} x") == Words{"a {nested b} c", "x"});
    REQUIRE(parseTclList("a\\ b c") == Words{"a b", "c"});
    REQUIRE(parseTclList("\"q\\\"uote\" z") == Words{"q\"uote", "z"});
    REQUIRE(parseTclList("").empty());
    REQUIRE(parseTclList("{} x") == Words{"", "x"});
    REQUIRE(parseTclList("{unterminated brace").size() == 1); // does not crash
}

TEST_CASE("colours by name and by value") {
    REQUIRE(parseColor("red").value() == 0xff6060);
    REQUIRE(parseColor("Gray").value() == 0x909090);
    REQUIRE(parseColor("#10ff20").value() == 0x10ff20);
    REQUIRE_FALSE(parseColor("chartreuse").has_value());
    REQUIRE_FALSE(parseColor("#12").has_value());
    REQUIRE_FALSE(parseColor("#12345g").has_value());
}

TEST_CASE("add wave options are parsed") {
    auto r = parseAddWave(
        {"-r", "-radix", "hex", "-color", "orange", "-label", "Counter", "-group", "Core", "/tb/dut/count"});
    REQUIRE(r.ok());
    const auto& q = r.value();
    REQUIRE(q.recursive);
    REQUIRE(q.radix.value() == Radix::Hex);
    REQUIRE(q.color.value() == 0xffa030);
    REQUIRE(q.label == "Counter");
    REQUIRE(q.group == "Core");
    REQUIRE(q.patterns == Words{"/tb/dut/count"});

    REQUIRE(parseAddWave({"-binary", "/a"}).value().radix.value() == Radix::Binary);
    REQUIRE(parseAddWave({"-unsigned", "/a"}).value().radix.value() == Radix::Unsigned);
    REQUIRE(parseAddWave({"-decimal", "/a"}).value().radix.value() == Radix::Unsigned);

    auto analog =
        parseAddWave({"-format", "analog-step", "-min", "-1.5", "-max", "3", "-height", "88", "/v"});
    REQUIRE(analog.ok());
    REQUIRE(analog.value().analog);
    REQUIRE(analog.value().min.value() == -1.5);
    REQUIRE(analog.value().max.value() == 3.0);
    REQUIRE(analog.value().heightRows == 4); // 88 pixels is 4 rows of 22

    auto many = parseAddWave({"/a", "/b", "-noupdate", "-position", "end", "/c", "-expand"});
    REQUIRE(many.value().patterns == Words{"/a", "/b", "/c"}); // known flags and their values are skipped
    REQUIRE(parseAddWave({"-someunknownflag", "/a"}).value().patterns == Words{"/a"});

    auto div = parseAddWave({"-divider", "Control"});
    REQUIRE(div.ok());
    REQUIRE(div.value().divider.value() == "Control");
}

TEST_CASE("add wave errors") {
    REQUIRE_FALSE(parseAddWave({}).ok());
    REQUIRE_FALSE(parseAddWave({"-r"}).ok()); // no signals
    REQUIRE_FALSE(parseAddWave({"-radix"}).ok());
    REQUIRE_FALSE(parseAddWave({"-radix", "base7", "/a"}).ok());
    REQUIRE_FALSE(parseAddWave({"-color", "nope", "/a"}).ok());
    REQUIRE_FALSE(parseAddWave({"-label"}).ok());
}

TEST_CASE("patterns relative to the design top, as in the NVC shell") {
    const WaveStore st = makeStore();
    REQUIRE(expandWavePattern(st, "/clk", false) == Words{"tb.clk"});
    REQUIRE(expandWavePattern(st, "clk", false) == Words{"tb.clk"});
    REQUIRE(expandWavePattern(st, "/dut/count", false) == Words{"tb.dut.count"});
    REQUIRE(expandWavePattern(st, "/dut/core/state", false) == Words{"tb.dut.core.state"});
    REQUIRE(expandWavePattern(st, "/nothere", false).empty());
    REQUIRE(expandWavePattern(st, "", false).empty());
}

TEST_CASE("patterns that start with the top's name, as in common simulator scripts") {
    const WaveStore st = makeStore();
    REQUIRE(expandWavePattern(st, "/tb/clk", false) == Words{"tb.clk"});
    REQUIRE(expandWavePattern(st, "/tb/dut/valid", false) == Words{"tb.dut.valid"});
    REQUIRE(expandWavePattern(st, "/TB/DUT/VALID", false) == Words{"tb.dut.valid"}); // case-insensitive
    REQUIRE(expandWavePattern(st, "/some_pkg/pkgsig", false) == Words{"some_pkg.pkgsig"});
}

TEST_CASE("wildcards and recursion") {
    const WaveStore st = makeStore();
    REQUIRE(expandWavePattern(st, "/*", false) == Words{"tb.clk", "tb.rst", "tb.data[0]", "tb.data[1]"});
    REQUIRE(expandWavePattern(st, "/data*", false) == Words{"tb.data[0]", "tb.data[1]"});
    REQUIRE(expandWavePattern(st, "/data[?]", false) ==
            Words{"tb.data[0]", "tb.data[1]"}); // brackets are literal
    REQUIRE(expandWavePattern(st, "/tb/dut/*", false) == Words{"tb.dut.count", "tb.dut.valid"});
    REQUIRE(expandWavePattern(st, "/dut/*", false) == Words{"tb.dut.count", "tb.dut.valid"});

    const auto all = expandWavePattern(st, "/*", true);
    REQUIRE(all.size() == 7); // everything below the top
    REQUIRE(all.back() == "tb.dut.core.state");
    REQUIRE(expandWavePattern(st, "/tb/dut/*", true).size() == 3);
    REQUIRE(expandWavePattern(st, "/*/count", false) == Words{"tb.dut.count"}); // a wildcard scope component

    REQUIRE(expandWavePattern(st, "/C*", false) == Words{"tb.clk"}); // case-insensitive
}

TEST_CASE("an empty store matches nothing") {
    const WaveStore empty;
    REQUIRE(expandWavePattern(empty, "/*", true).empty());
}

TEST_CASE("applying add wave to the display model") {
    const WaveStore st = makeStore();
    DisplayModel m;

    auto req = parseAddWave({"-radix", "hex", "-color", "#112233", "/dut/*"}).value();
    REQUIRE(applyAddWave(m, st, req) == 2);
    REQUIRE(m.root().children.size() == 2);
    REQUIRE(m.root().children[0].path == "tb.dut.count");
    REQUIRE(m.root().children[0].radix == Radix::Hex);
    REQUIRE(m.root().children[0].color.value() == 0x112233);

    auto labelled = parseAddWave({"-label", "Clock", "/clk"}).value();
    REQUIRE(applyAddWave(m, st, labelled) == 1);
    REQUIRE(m.root().children.back().label == "Clock");

    auto notLabelled =
        parseAddWave({"-label", "Ignored", "/data*"}).value(); // label only for a single signal
    REQUIRE(applyAddWave(m, st, notLabelled) == 2);
    REQUIRE(m.root().children[m.root().children.size() - 1].label.empty());

    auto grouped = parseAddWave({"-group", "Core", "/dut/core/state"}).value();
    REQUIRE(applyAddWave(m, st, grouped) == 1);
    REQUIRE(applyAddWave(m, st, parseAddWave({"-group", "Core", "/rst"}).value()) == 1); // reuses the group
    int groups = 0;
    for (const auto& c : m.root().children)
        if (c.kind == ItemKind::Group) {
            ++groups;
            REQUIRE(c.label == "Core");
            REQUIRE(c.children.size() == 2);
        }
    REQUIRE(groups == 1);

    REQUIRE(applyAddWave(m, st, parseAddWave({"-divider", "Control"}).value()) == 1);
    REQUIRE(m.root().children.back().kind == ItemKind::Divider);
    REQUIRE(m.root().children.back().label == "Control");

    REQUIRE(applyAddWave(m, st, parseAddWave({"/nothere"}).value()) == 0);

    auto analog = parseAddWave({"-format", "analog", "-min", "0", "-max", "10", "/dut/count"}).value();
    REQUIRE(applyAddWave(m, st, analog) == 1);
    const DisplayItem& a = m.root().children.back();
    REQUIRE(a.analog.enabled);
    REQUIRE_FALSE(a.analog.autoRange);
    REQUIRE(a.analog.max == 10.0);
}

TEST_CASE("a vector named with its range matches the plain name, as in common simulator scripts") {
    WaveStore st;
    const ScopeId top = st.addScope(kRootScope, "demo");
    st.addSignal(top, "count[7:0]", SignalKind::Logic, 8);
    st.addSignal(top, "bus16[15:0]", SignalKind::Logic, 16);
    st.addSignal(top, "data[0]", SignalKind::Logic, 1);
    st.addSignal(top, "[3:0]", SignalKind::Logic, 4);  // odd name: nothing before the bracket

    REQUIRE(expandWavePattern(st, "/count", false) == Words{"demo.count[7:0]"});
    REQUIRE(expandWavePattern(st, "/demo/count", false) == Words{"demo.count[7:0]"});
    REQUIRE(expandWavePattern(st, "/count[7:0]", false) == Words{"demo.count[7:0]"});  // the full name still works
    REQUIRE(expandWavePattern(st, "/COUNT", false) == Words{"demo.count[7:0]"});
    REQUIRE(expandWavePattern(st, "/bus*", false) == Words{"demo.bus16[15:0]"});
    REQUIRE(expandWavePattern(st, "/data", false).empty());  // an element index is not a range
    REQUIRE(expandWavePattern(st, "/data[0]", false) == Words{"demo.data[0]"});
    REQUIRE(expandWavePattern(st, "/*", false).size() == 4);  // wildcards still see every signal once
}

// ---------------------------------------------------------------------------------------------
// .do export and import

namespace {

WaveStore doStore() {
    WaveStore st;
    const ScopeId top = st.addScope(kRootScope, "tb");
    st.addSignal(top, "clk", SignalKind::Logic, 1);
    st.addSignal(top, "count[7:0]", SignalKind::Logic, 8);
    st.addSignal(top, "data[3]", SignalKind::Logic, 1);
    st.addSignal(top, "volt", SignalKind::Real, 64);
    const ScopeId dut = st.addScope(top, "dut");
    st.addSignal(dut, "state", SignalKind::String, 0);
    st.addSignal(dut, "q", SignalKind::Logic, 4);
    return st;
}

}  // namespace

TEST_CASE("the exported script is made of add wave commands") {
    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeSignal("tb.clk"));
    DisplayItem c = DisplayModel::makeSignal("tb.count[7:0]", Radix::Hex);
    c.color = 0xff8000;
    c.label = "Two words";
    m.root().children.push_back(std::move(c));
    m.root().children.push_back(DisplayModel::makeDivider("alu stage"));

    const std::string script = exportDoScript(m);
    REQUIRE(script.find("# lvdsim wave layout") == 0);
    REQUIRE(script.find("add wave /tb/clk\n") != std::string::npos);
    REQUIRE(script.find("add wave -radix hex -color #ff8000 -label {Two words} {/tb/count[7:0]}\n") != std::string::npos);
    REQUIRE(script.find("add wave -divider {alu stage}\n") != std::string::npos);
}

TEST_CASE("export then import rebuilds the same rows") {
    const WaveStore st = doStore();
    DisplayModel m;

    m.root().children.push_back(DisplayModel::makeSignal("tb.clk", Radix::Binary));

    DisplayItem count = DisplayModel::makeSignal("tb.count[7:0]", Radix::Unsigned);
    count.color = 0x40e040;
    count.label = "Counter value";
    count.showBits = true;
    m.root().children.push_back(std::move(count));

    DisplayItem volt = DisplayModel::makeSignal("tb.volt");
    volt.analog.enabled = true;
    volt.analog.autoRange = false;
    volt.analog.min = -1.5;
    volt.analog.max = 3.25;
    volt.analog.heightRows = 4;
    volt.analog.interpolation = Interpolation::Linear;
    m.root().children.push_back(std::move(volt));

    DisplayItem g = DisplayModel::makeGroup("Core", false);  // collapsed
    g.children.push_back(DisplayModel::makeSignal("tb.dut.state"));
    g.children.push_back(DisplayModel::makeDivider("regs"));
    g.children.push_back(DisplayModel::makeSignal("tb.dut.q", Radix::Hex));
    m.root().children.push_back(std::move(g));

    DisplayItem bus = DisplayModel::makeBus("sparse", {{"tb.count[7:0]", 7}, {"tb.clk"}, {"tb.count[7:0]", 0}}, Radix::Binary);
    bus.color = 0x00aaff;
    m.root().children.push_back(std::move(bus));

    m.root().children.push_back(DisplayModel::makeSignal("tb.data[3]"));

    const std::string script = exportDoScript(m);
    DisplayModel back;
    const ImportResult r = importDoScript(script, back, st);
    INFO(script);
    REQUIRE(r.errors.empty());
    REQUIRE(r.skipped == 0);
    REQUIRE(back.serialize() == m.serialize());
}

TEST_CASE("nested groups are exported as a path and imported as nested groups") {
    const WaveStore st = doStore();
    DisplayModel m;
    DisplayItem outer = DisplayModel::makeGroup("A");
    DisplayItem inner = DisplayModel::makeGroup("B");
    inner.children.push_back(DisplayModel::makeSignal("tb.clk"));
    outer.children.push_back(std::move(inner));
    m.root().children.push_back(std::move(outer));

    const std::string script = exportDoScript(m);
    REQUIRE(script.find("-group A/B") != std::string::npos);
    DisplayModel back;
    REQUIRE(importDoScript(script, back, st).errors.empty());
    REQUIRE(back.serialize() == m.serialize());
    REQUIRE(back.root().children.size() == 1);
    REQUIRE(back.root().children[0].label == "A");
    REQUIRE(back.root().children[0].children[0].label == "B");
}

TEST_CASE("the first row of a collapsed group carries -collapse once") {
    DisplayModel m;
    DisplayItem g = DisplayModel::makeGroup("G", false);
    g.children.push_back(DisplayModel::makeSignal("tb.clk"));
    g.children.push_back(DisplayModel::makeSignal("tb.volt"));
    m.root().children.push_back(std::move(g));
    const std::string script = exportDoScript(m);
    REQUIRE(script.find("-collapse") == script.rfind("-collapse"));
    REQUIRE(script.find("-collapse") != std::string::npos);
}

TEST_CASE("importing a hand written do file") {
    const WaveStore st = doStore();
    DisplayModel m;
    const std::string script =
        "# comment\r\n"
        "\r\n"
        "vsim work.tb\r\n"                                 // not an add wave command
        "add wave -noupdate -radix hex /tb/count\r\n"
        "add wave -noupdate -divider {Control signals}\r\n"
        "add wave -group {Core} /tb/dut/* \\\r\n"           // a continued line
        "   -r\r\n"
        "wave zoom full\r\n"
        "add wave /tb/nothing\r\n"
        "add wave -radix base9 /tb/clk\r\n";
    const ImportResult r = importDoScript(script, m, st);
    REQUIRE(r.rowsAdded == 4);  // count, divider, state, q
    REQUIRE(r.skipped == 2);    // vsim, wave zoom
    REQUIRE(r.errors.size() == 2);
    REQUIRE(r.errors[0].find("line 9") != std::string::npos);
    REQUIRE(r.errors[0].find("no signal matches") != std::string::npos);
    REQUIRE(r.errors[1].find("line 10") != std::string::npos);
    REQUIRE(m.root().children.size() == 3);  // count, divider, group Core
    REQUIRE(m.root().children[2].children.size() == 2);
    REQUIRE(m.root().children[0].path == "tb.count[7:0]");
}

TEST_CASE("add wave extensions parse") {
    auto r = parseAddWave({"-vbus", "mybus", "-radix", "binary", "-group", "G", "-collapse", "/tb/clk", "/tb/count@3"});
    REQUIRE(r.ok());
    REQUIRE(r.value().vbus.value() == "mybus");
    REQUIRE(r.value().collapse);
    REQUIRE(r.value().patterns == Words{"/tb/clk", "/tb/count@3"});
    REQUIRE(parseAddWave({"-bits", "/tb/count"}).value().showBits);
    REQUIRE(parseAddWave({"-format", "analog-interpolated", "/tb/volt"}).value().linear);
    REQUIRE_FALSE(parseAddWave({"-format", "analog-step", "/tb/volt"}).value().linear);
    REQUIRE_FALSE(parseAddWave({"-vbus"}).ok());
}

TEST_CASE("a virtual bus whose members do not exist is not added") {
    const WaveStore st = doStore();
    DisplayModel m;
    REQUIRE(applyAddWave(m, st, parseAddWave({"-vbus", "x", "/tb/nothing@1"}).value()) == 0);
    REQUIRE(m.root().children.empty());
}

// ---------------------------------------------------------------------------------------------
// Arranging rows: the exported script keeps order, group membership, names and collapsed state

namespace {

DisplayModel roundTrip(const DisplayModel& m, const WaveStore& st, std::string* scriptOut = nullptr) {
    const std::string script = exportDoScript(m);
    if (scriptOut != nullptr)
        *scriptOut = script;
    DisplayModel back;
    const ImportResult r = importDoScript(script, back, st);
    INFO(script);
    REQUIRE(r.errors.empty());
    REQUIRE(r.skipped == 0);
    return back;
}

}  // namespace

TEST_CASE("an empty group and a group that starts with a group survive the .do round trip") {
    const WaveStore st = doStore();
    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeGroup("Empty"));
    m.root().children.push_back(DisplayModel::makeGroup("Closed", false));
    DisplayItem outer = DisplayModel::makeGroup("Outer", false);  // collapsed, first child is a group
    DisplayItem inner = DisplayModel::makeGroup("Inner");
    inner.children.push_back(DisplayModel::makeSignal("tb.clk"));
    outer.children.push_back(std::move(inner));
    outer.children.push_back(DisplayModel::makeSignal("tb.volt"));
    outer.children.push_back(DisplayModel::makeGroup("Last", false));
    m.root().children.push_back(std::move(outer));
    m.root().children.push_back(DisplayModel::makeSignal("tb.data[3]"));
    std::string script;
    const DisplayModel back = roundTrip(m, st, &script);
    REQUIRE(back.serialize() == m.serialize());
    REQUIRE(script.find("add wave -group Empty\n") != std::string::npos);
    REQUIRE(script.find("add wave -group Closed -collapse\n") != std::string::npos);
}

TEST_CASE("dividers keep their label, an empty one included, inside and outside groups") {
    const WaveStore st = doStore();
    DisplayModel m;
    m.root().children.push_back(DisplayModel::makeDivider(""));
    DisplayItem g = DisplayModel::makeGroup("G");
    g.children.push_back(DisplayModel::makeDivider("in group"));
    g.children.push_back(DisplayModel::makeDivider(""));
    g.children.push_back(DisplayModel::makeSignal("tb.clk"));
    m.root().children.push_back(std::move(g));
    const DisplayModel back = roundTrip(m, st);
    REQUIRE(back.serialize() == m.serialize());
}

TEST_CASE("a group, signal and divider with awkward names round trip") {
    const WaveStore st = doStore();
    DisplayModel m;
    DisplayItem g = DisplayModel::makeGroup("Name with {braces} and $dollar [x]");
    DisplayItem s = DisplayModel::makeSignal("tb.clk");
    s.label = "label \"quoted\" ; semi";
    g.children.push_back(std::move(s));
    g.children.push_back(DisplayModel::makeDivider("a b c"));
    m.root().children.push_back(std::move(g));
    const DisplayModel back = roundTrip(m, st);
    REQUIRE(back.serialize() == m.serialize());
}

TEST_CASE("a virtual bus keeps its radix, including default, through the .do script") {
    const WaveStore st = doStore();
    for (Radix r : {Radix::Hex, Radix::Default, Radix::Binary, Radix::Unsigned}) {
        DisplayModel m;
        m.root().children.push_back(DisplayModel::makeBus("b", {{"tb.clk"}, {"tb.count[7:0]", 2}}, r));
        REQUIRE(roundTrip(m, st).serialize() == m.serialize());
    }
}

TEST_CASE("rows moved, grouped, renamed and ungrouped export and re-import in the same order") {
    const WaveStore st = doStore();
    DisplayModel m;
    for (const char* p : {"tb.clk", "tb.count[7:0]", "tb.data[3]", "tb.volt", "tb.dut.q", "tb.dut.state"})
        m.root().children.push_back(DisplayModel::makeSignal(p));
    m.root().children.push_back(DisplayModel::makeDivider("tail"));
    auto idOfPath = [&](const char* p) {
        for (ItemId id : allItemIds(m))
            if (findItem(m, id)->path == p)
                return id;
        return ItemId{0};
    };

    REQUIRE(moveItems(m, {idOfPath("tb.volt")}, -2).ok());
    auto g1 = wrapInGroup(m, {idOfPath("tb.clk"), idOfPath("tb.data[3]")}, "Control");
    REQUIRE(g1.ok());
    auto g2 = wrapInGroup(m, {idOfPath("tb.dut.q"), idOfPath("tb.dut.state"), g1.value().created[0]}, "All");
    REQUIRE(g2.ok());
    REQUIRE(renameItem(m, g1.value().created[0], "Ctl renamed").ok());
    findItem(m, g1.value().created[0])->expanded = false;
    REQUIRE(moveItemsToGroup(m, {idOfPath("tb.count[7:0]")}, g1.value().created[0], 0).ok());
    auto g3 = wrapInGroup(m, {idOfPath("tb.volt")}, "Analog");
    REQUIRE(g3.ok());
    REQUIRE(ungroup(m, g3.value().created).ok());
    REQUIRE(renameItem(m, idOfPath("tb.volt"), "Voltage").ok());

    std::string script;
    const DisplayModel back = roundTrip(m, st, &script);
    REQUIRE(back.serialize() == m.serialize());
    REQUIRE(script.find("-group {All/Ctl renamed} -collapse") != std::string::npos);
    REQUIRE(script.find("-label Voltage") != std::string::npos);
}

TEST_CASE("add wave -group with a path creates the levels and reuses them") {
    const WaveStore st = doStore();
    DisplayModel m;
    REQUIRE(applyAddWave(m, st, parseAddWave({"-group", "A/B", "/tb/clk"}).value()) == 1);
    REQUIRE(applyAddWave(m, st, parseAddWave({"-group", "A", "/tb/volt"}).value()) == 1);
    REQUIRE(applyAddWave(m, st, parseAddWave({"-group", "A/B", "/tb/data[3]"}).value()) == 1);
    REQUIRE(m.root().children.size() == 1);
    REQUIRE(m.root().children[0].children.size() == 2);
    REQUIRE(m.root().children[0].children[0].children.size() == 2);
    // only the innermost group created by the request is collapsed
    DisplayModel c;
    REQUIRE(applyAddWave(c, st, parseAddWave({"-group", "X/Y", "-collapse", "/tb/clk"}).value()) == 1);
    REQUIRE(c.root().children[0].expanded);
    REQUIRE_FALSE(c.root().children[0].children[0].expanded);
}

TEST_CASE("add wave -group NAME without signals declares an empty group") {
    const WaveStore st = doStore();
    auto r = parseAddWave({"-group", "Empty", "-collapse"});
    REQUIRE(r.ok());
    DisplayModel m;
    REQUIRE(applyAddWave(m, st, r.value()) == 1);
    REQUIRE(m.root().children.size() == 1);
    REQUIRE(m.root().children[0].kind == ItemKind::Group);
    REQUIRE_FALSE(m.root().children[0].expanded);
    REQUIRE_FALSE(parseAddWave({}).ok());
    REQUIRE_FALSE(parseAddWave({"-radix", "hex"}).ok());
}
