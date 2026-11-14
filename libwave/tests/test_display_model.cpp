// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <lvdwave/display_model.hpp>

#include <cmath>
#include <limits>

using namespace lvd::wave;

namespace {

DisplayModel sample() {
    DisplayModel m;
    DisplayItem clocks = DisplayModel::makeGroup("Clocks");
    clocks.children.push_back(DisplayModel::makeSignal("top.clk", Radix::Binary));
    DisplayItem inner = DisplayModel::makeGroup("Inner \"quoted\"", false);
    inner.children.push_back(DisplayModel::makeSignal("top.rst"));
    clocks.children.push_back(std::move(inner));
    m.root().children.push_back(std::move(clocks));
    m.root().children.push_back(DisplayModel::makeDivider("data path"));

    DisplayItem bus = DisplayModel::makeBus("addr", {"top.a_hi", "top.a_lo"}, Radix::Hex);
    m.root().children.push_back(std::move(bus));

    DisplayItem an = DisplayModel::makeSignal("top.adc", Radix::Unsigned);
    an.label = "ADC value";
    an.analog.enabled = true;
    an.analog.autoRange = false;
    an.analog.min = -1.5;
    an.analog.max = 3.25;
    an.analog.interpolation = Interpolation::Linear;
    an.analog.isSigned = true;
    an.analog.heightRows = 6;
    m.root().children.push_back(std::move(an));
    return m;
}

}  // namespace

TEST_CASE("serialize and parse round trip") {
    const DisplayModel m = sample();
    const std::string text = m.serialize();
    auto parsed = DisplayModel::parse(text);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value().serialize() == text);

    const auto& root = parsed.value().root();
    REQUIRE(root.children.size() == 4);
    REQUIRE(root.children[0].label == "Clocks");
    REQUIRE(root.children[0].children[1].label == "Inner \"quoted\"");
    REQUIRE_FALSE(root.children[0].children[1].expanded);
    REQUIRE(root.children[2].members.size() == 2);
    REQUIRE(root.children[2].members[0].path == "top.a_hi");
    const DisplayItem& an = root.children[3];
    REQUIRE(an.label == "ADC value");
    REQUIRE(an.analog.enabled);
    REQUIRE_FALSE(an.analog.autoRange);
    REQUIRE(an.analog.min == -1.5);
    REQUIRE(an.analog.max == 3.25);
    REQUIRE(an.analog.interpolation == Interpolation::Linear);
    REQUIRE(an.analog.isSigned);
    REQUIRE(an.analog.heightRows == 6);
    REQUIRE(an.radix == Radix::Unsigned);
}

TEST_CASE("visible rows skip collapsed groups") {
    const DisplayModel m = sample();
    const auto rows = m.visibleRows();
    // Clocks, clk, Inner (collapsed, children hidden), divider, addr, adc
    REQUIRE(rows.size() == 6);
    REQUIRE(rows[0].depth == 0);
    REQUIRE(rows[1].depth == 1);
    REQUIRE(rows[2].item->kind == ItemKind::Group);
    REQUIRE(rows[2].depth == 1);
    REQUIRE(rows[3].item->kind == ItemKind::Divider);
    REQUIRE(rows[4].item->kind == ItemKind::VirtualBus);
}

TEST_CASE("parse errors are reported with a line number") {
    REQUIRE_FALSE(DisplayModel::parse("").ok());
    REQUIRE_FALSE(DisplayModel::parse("garbage 1\n").ok());
    auto r = DisplayModel::parse("lvdwave-display 1\nsignal \"a\" radix=base9\n");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().find("line 2") != std::string::npos);
    REQUIRE_FALSE(DisplayModel::parse("lvdwave-display 1\ngroup \"g\"\n").ok());            // unterminated
    REQUIRE_FALSE(DisplayModel::parse("lvdwave-display 1\nend\n").ok());                    // unbalanced
    REQUIRE_FALSE(DisplayModel::parse("lvdwave-display 1\nsignal \"unterminated\n").ok());  // bad string
    REQUIRE_FALSE(DisplayModel::parse("lvdwave-display 2\n").ok());                         // version
}

TEST_CASE("comments and blank lines are ignored") {
    auto r = DisplayModel::parse("lvdwave-display 1\n\n# a comment\nsignal \"x\"\n");
    REQUIRE(r.ok());
    REQUIRE(r.value().root().children.size() == 1);
}

TEST_CASE("virtual bus composes member values at a time") {
    WaveStore st;
    const SignalId hi = st.addSignal(kRootScope, "hi", SignalKind::Logic, 4);
    const SignalId lo = st.addSignal(kRootScope, "lo", SignalKind::Logic, 2);
    const SignalId b = st.addSignal(kRootScope, "b", SignalKind::Logic, 1);
    st.mutableSignal(hi).appendLogic(0, "0000");
    st.mutableSignal(hi).appendLogic(10, "1X01");
    st.mutableSignal(lo).appendLogic(5, "11");
    st.mutableSignal(b).appendLogic(0, "1");
    (void)b;

    const std::vector<BusMember> members{"hi", "lo", "b", "missing"};
    REQUIRE(busValueAt(st, members, 0) == "0000" "UU" "1" "U");  // lo has no value yet
    REQUIRE(busValueAt(st, members, 7) == "0000" "11" "1" "U");
    REQUIRE(busValueAt(st, members, 10) == "1X01" "11" "1" "U");
}

namespace {

DisplayModel flatModel() {
    DisplayModel m;
    for (const char* p : {"top.a", "top.b", "top.c", "top.d"}) m.root().children.push_back(DisplayModel::makeSignal(p));
    return m;
}

}  // namespace

TEST_CASE("groupItems moves items into a group at the first position") {
    DisplayModel m = flatModel();
    const DisplayItem* b = &m.root().children[1];
    const DisplayItem* d = &m.root().children[3];
    REQUIRE(groupItems(m, {d, b}, "G"));  // order of the argument does not matter
    REQUIRE(m.root().children.size() == 3);
    REQUIRE(m.root().children[0].path == "top.a");
    REQUIRE(m.root().children[1].kind == ItemKind::Group);
    REQUIRE(m.root().children[1].label == "G");
    REQUIRE(m.root().children[1].children.size() == 2);
    REQUIRE(m.root().children[1].children[0].path == "top.b");
    REQUIRE(m.root().children[1].children[1].path == "top.d");
    REQUIRE(m.root().children[2].path == "top.c");
}

TEST_CASE("groupItems refuses items with different parents") {
    DisplayModel m = flatModel();
    REQUIRE(groupItems(m, {&m.root().children[0], &m.root().children[1]}, "G"));
    const DisplayItem* inner = &m.root().children[0].children[0];
    const DisplayItem* outer = &m.root().children[1];
    const std::string before = m.serialize();
    REQUIRE_FALSE(groupItems(m, {inner, outer}, "X"));
    REQUIRE(m.serialize() == before);
    REQUIRE_FALSE(groupItems(m, {}, "X"));
}

TEST_CASE("removeItems removes from several parents") {
    DisplayModel m = flatModel();
    REQUIRE(groupItems(m, {&m.root().children[0], &m.root().children[1]}, "G"));
    const DisplayItem* inner = &m.root().children[0].children[1];  // top.b inside G
    const DisplayItem* outer = &m.root().children[2];              // top.d
    REQUIRE(removeItems(m, {inner, outer}) == 2);
    REQUIRE(m.root().children.size() == 2);
    REQUIRE(m.root().children[0].children.size() == 1);
    REQUIRE(m.root().children[0].children[0].path == "top.a");
    REQUIRE(m.root().children[1].path == "top.c");
}

TEST_CASE("busFromSignals builds a bus after the last selected item") {
    DisplayModel m = flatModel();
    REQUIRE(busFromSignals(m, {&m.root().children[2], &m.root().children[0]}, "bus", Radix::Hex));
    REQUIRE(m.root().children.size() == 5);
    const DisplayItem& bus = m.root().children[3];
    REQUIRE(bus.kind == ItemKind::VirtualBus);
    REQUIRE(bus.members == std::vector<BusMember>{"top.a", "top.c"});  // display order, not selection order
    REQUIRE(m.root().children[4].path == "top.d");

    REQUIRE_FALSE(busFromSignals(m, {&m.root().children[0]}, "one"));  // needs two signals
}

TEST_CASE("bus members can select single bits of different signals") {
    WaveStore st;
    const SignalId a = st.addSignal(kRootScope, "a", SignalKind::Logic, 4);
    const SignalId b = st.addSignal(kRootScope, "b", SignalKind::Logic, 8);
    st.mutableSignal(a).appendLogic(0, "1010");  // a[3]=1 a[2]=0 a[1]=1 a[0]=0
    st.mutableSignal(b).appendLogic(5, "0000X001");

    const std::vector<BusMember> m{{"a", 3}, {"b", 0}, {"a", 0}, {"b", 4}, {"a", 9}, {"nope", 1}};
    REQUIRE(busValueAt(st, m, 0) == "1" "U" "0" "U" "U" "U");  // b has no value yet; a[9] and nope are missing
    REQUIRE(busValueAt(st, m, 5) == "1" "1" "0" "0" "U" "U");
    REQUIRE(busValueAt(st, {{"b", 3}}, 5) == "X");
    // Sparse selection in the order given, not in bit order.
    REQUIRE(busValueAt(st, {{"a", 0}, {"a", 1}, {"a", 2}, {"a", 3}}, 0) == "0101");
}

TEST_CASE("colour, show-bits and bit members round trip") {
    DisplayModel m;
    DisplayItem s = DisplayModel::makeSignal("top.v", Radix::Hex);
    s.showBits = true;
    s.color = 0xFF8000u;
    m.root().children.push_back(std::move(s));
    DisplayItem bus = DisplayModel::makeBus("sparse", {{"top.v", 7}, {"top.w"}, {"top.v", 0}}, Radix::Binary);
    bus.color = 0x00AAFFu;
    m.root().children.push_back(std::move(bus));

    const std::string text = m.serialize();
    auto parsed = DisplayModel::parse(text);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value().serialize() == text);
    const auto& root = parsed.value().root();
    REQUIRE(root.children[0].showBits);
    REQUIRE(root.children[0].color.value() == 0xFF8000u);
    REQUIRE(root.children[1].color.value() == 0x00AAFFu);
    REQUIRE(root.children[1].members.size() == 3);
    REQUIRE(root.children[1].members[0].bit == 7);
    REQUIRE(root.children[1].members[1].bit == -1);
    REQUIRE(root.children[1].members[2].bit == 0);

    REQUIRE_FALSE(DisplayModel::parse("lvdwave-display 1\nsignal \"a\" color=red\n").ok());
    REQUIRE_FALSE(DisplayModel::parse("lvdwave-display 1\nbus \"b\"\n  member \"x\" wrong=1\nend\n").ok());
}

TEST_CASE("insertAfter places an item next to another") {
    DisplayModel m = flatModel();
    const DisplayItem* b = &m.root().children[1];
    insertAfter(m, b, DisplayModel::makeDivider("here"));
    REQUIRE(m.root().children.size() == 5);
    REQUIRE(m.root().children[2].kind == ItemKind::Divider);
    REQUIRE(m.root().children[2].label == "here");

    insertAfter(m, nullptr, DisplayModel::makeDivider("end"));  // appended to the root
    REQUIRE(m.root().children.back().label == "end");

    REQUIRE(groupItems(m, {&m.root().children[0]}, "G"));
    const DisplayItem* inner = &m.root().children[0].children[0];
    insertAfter(m, inner, DisplayModel::makeDivider("inside"));
    REQUIRE(m.root().children[0].children.size() == 2);
    REQUIRE(m.root().children[0].children[1].label == "inside");
}

TEST_CASE("analog range: a manual range is validated, an auto range comes from the data") {
    REQUIRE(analogRangeError(-1.5, 3.25).empty());
    REQUIRE(analogRangeError(0.0, 1e-30).empty());
    REQUIRE_FALSE(analogRangeError(1.0, 1.0).empty());
    REQUIRE_FALSE(analogRangeError(2.0, -2.0).empty());
    REQUIRE_FALSE(analogRangeError(std::nan(""), 1.0).empty());
    REQUIRE_FALSE(analogRangeError(0.0, std::numeric_limits<double>::infinity()).empty());

    AnalogSettings a;  // auto by default
    REQUIRE(a.autoRange);
    REQUIRE_FALSE(resolveAnalogRange(a, std::nullopt).has_value());  // no data yet, no range
    auto r = resolveAnalogRange(a, AnalogRange{-2.0, 6.0});
    REQUIRE(r.has_value());
    REQUIRE(r->min == -2.0);
    REQUIRE(r->max == 6.0);
    // one constant value: a range around it, never a division by zero
    r = resolveAnalogRange(a, AnalogRange{4.0, 4.0});
    REQUIRE(r->min == 3.5);
    REQUIRE(r->max == 4.5);

    a.autoRange = false;
    a.min = 0.0;
    a.max = 3.3;
    r = resolveAnalogRange(a, AnalogRange{-100.0, 100.0});  // the data does not matter for a manual range
    REQUIRE(r->min == 0.0);
    REQUIRE(r->max == 3.3);
    r = resolveAnalogRange(a, std::nullopt);  // and neither does its absence
    REQUIRE(r.has_value());
    a.min = 5.0;  // a bad manual range is repaired, not trusted
    a.max = 1.0;
    r = resolveAnalogRange(a, std::nullopt);
    REQUIRE(r->max > r->min);
}

TEST_CASE("analog fraction clamps to the row edges") {
    const AnalogRange r{2.0, 6.0};
    REQUIRE(analogFraction(r, 2.0) == 0.0);
    REQUIRE(analogFraction(r, 6.0) == 1.0);
    REQUIRE(analogFraction(r, 4.0) == 0.5);
    REQUIRE(analogFraction(r, -1e9) == 0.0);
    REQUIRE(analogFraction(r, 1e9) == 1.0);
    REQUIRE(analogFraction(r, std::numeric_limits<double>::infinity()) == 1.0);
    REQUIRE(analogFraction(r, std::nan("")) == 0.0);
}

TEST_CASE("a manual analog range round-trips through serialize and parse with scientific values") {
    DisplayModel m;
    DisplayItem an = DisplayModel::makeSignal("top.adc");
    an.analog.enabled = true;
    an.analog.autoRange = false;
    an.analog.min = -2.5e-9;
    an.analog.max = 1.25e6;
    m.root().children.push_back(std::move(an));
    const auto back = DisplayModel::parse(m.serialize());
    REQUIRE(back.ok());
    const AnalogSettings& a = back.value().root().children[0].analog;
    REQUIRE_FALSE(a.autoRange);
    REQUIRE(a.min == -2.5e-9);
    REQUIRE(a.max == 1.25e6);
    // and an auto row stays auto
    m.root().children[0].analog.autoRange = true;
    const auto autoBack = DisplayModel::parse(m.serialize());
    REQUIRE(autoBack.value().root().children[0].analog.autoRange);
}

TEST_CASE("row heights in pixels: defaults, limits and the layout file") {
    CHECK(clampRowHeight(0) == 0);
    CHECK(clampRowHeight(-5) == 0);
    CHECK(clampRowHeight(3) == kMinRowHeightPx);
    CHECK(clampRowHeight(40) == 40);
    CHECK(clampRowHeight(100000) == kMaxRowHeightPx);

    DisplayItem sig = DisplayModel::makeSignal("tb.clk");
    CHECK(rowHeightPx(sig, false) == kDefaultRowHeightPx);
    CHECK(rowHeightPx(sig, true) == 4 * kDefaultRowHeightPx); // analog default: heightRows lines
    sig.heightPx = 31;
    CHECK(rowHeightPx(sig, false) == 31);
    CHECK(rowHeightPx(sig, true) == 31);

    DisplayModel m;
    DisplayItem a = DisplayModel::makeSignal("tb.a");
    a.heightPx = 40;
    m.root().children.push_back(a);
    DisplayItem bus = DisplayModel::makeBus("vb", {"tb.a", BusMember("tb.b", 2)});
    bus.heightPx = 120;
    bus.analog.enabled = true;
    m.root().children.push_back(bus);
    DisplayItem ex = DisplayModel::makeExpression("e", "tb.a and tb.b");
    ex.heightPx = 14;
    m.root().children.push_back(ex);
    DisplayItem cmp = DisplayModel::makeCompare("c", "tb.a", "ref:tb.a");
    cmp.heightPx = 25;
    m.root().children.push_back(cmp);
    DisplayItem div = DisplayModel::makeDivider("d");
    div.heightPx = 30;
    m.root().children.push_back(div);
    m.root().children.push_back(DisplayModel::makeSignal("tb.plain"));

    const std::string text = m.serialize();
    CHECK(text.find("heightpx=40") != std::string::npos);
    CHECK(text.find("signal \"tb.plain\" radix=default\n") != std::string::npos); // nothing for the default
    auto back = DisplayModel::parse(text);
    REQUIRE(back.ok());
    const auto& c = back.value().root().children;
    REQUIRE(c.size() == 6);
    CHECK(c[0].heightPx == 40);
    CHECK(c[1].heightPx == 120);
    CHECK(c[1].analog.enabled);
    CHECK(c[2].heightPx == 14);
    CHECK(c[3].heightPx == 25);
    CHECK(c[4].heightPx == 30);
    CHECK(c[5].heightPx == 0);
    CHECK(back.value().serialize() == text);
    // out of range values in a file are brought into range
    auto clamped = DisplayModel::parse("lvdwave-display 1\nsignal \"x\" heightpx=2\nsignal \"y\" heightpx=99999\n");
    REQUIRE(clamped.ok());
    CHECK(clamped.value().root().children[0].heightPx == kMinRowHeightPx);
    CHECK(clamped.value().root().children[1].heightPx == kMaxRowHeightPx);
}
