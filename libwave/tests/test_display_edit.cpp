// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/display_edit.hpp>
#include <lvdwave/wave_commands.hpp>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace lvd::wave;

namespace {

// Compact text of a tree: signals by path tail, groups as "[Name: ...]", dividers as "-text-", buses as
// "{name}".
std::string shape(const DisplayItem& g) {
    std::string out;
    for (std::size_t i = 0; i < g.children.size(); ++i) {
        const DisplayItem& c = g.children[i];
        if (i != 0)
            out += ' ';
        switch (c.kind) {
        case ItemKind::Signal:
            out += c.path.substr(c.path.rfind('.') + 1);
            break;
        case ItemKind::Divider:
            out += "-" + c.label + "-";
            break;
        case ItemKind::VirtualBus:
            out += "{" + c.label + "}";
            break;
        case ItemKind::Group:
            out += "[" + c.label + (c.expanded ? ": " : "~ ") + shape(c) + "]";
            break;
        }
    }
    return out;
}
std::string shape(const DisplayModel& m) {
    return shape(m.root());
}

ItemId idOf(const DisplayModel& m, const std::string& name) {
    for (ItemId id : allItemIds(m)) {
        const DisplayItem* it = findItem(m, id);
        if (it->kind == ItemKind::Signal && it->path.substr(it->path.rfind('.') + 1) == name)
            return id;
        if ((it->kind == ItemKind::Group || it->kind == ItemKind::Divider) && it->label == name)
            return id;
        if (it->kind == ItemKind::VirtualBus && it->label == name)
            return id;
    }
    FAIL("no item " << name);
    return 0;
}

std::vector<ItemId> ids(const DisplayModel& m, std::initializer_list<const char*> names) {
    std::vector<ItemId> v;
    for (const char* n : names)
        v.push_back(idOf(m, n));
    return v;
}

// a b c d e
DisplayModel flat() {
    DisplayModel m;
    for (const char* n : {"a", "b", "c", "d", "e"})
        m.root().children.push_back(DisplayModel::makeSignal(std::string("top.") + n));
    return m;
}

// a [G1: b c [Inner: d]] -div- [G2: e f] g
DisplayModel nested() {
    DisplayModel m;
    auto sig = [](const char* n) { return DisplayModel::makeSignal(std::string("top.") + n); };
    m.root().children.push_back(sig("a"));
    DisplayItem g1 = DisplayModel::makeGroup("G1");
    g1.children.push_back(sig("b"));
    g1.children.push_back(sig("c"));
    DisplayItem inner = DisplayModel::makeGroup("Inner");
    inner.children.push_back(sig("d"));
    g1.children.push_back(std::move(inner));
    m.root().children.push_back(std::move(g1));
    m.root().children.push_back(DisplayModel::makeDivider("div"));
    DisplayItem g2 = DisplayModel::makeGroup("G2");
    g2.children.push_back(sig("e"));
    g2.children.push_back(sig("f"));
    m.root().children.push_back(std::move(g2));
    m.root().children.push_back(sig("g"));
    return m;
}

} // namespace

TEST_CASE("items get unique ids that copies keep") {
    DisplayItem a = DisplayModel::makeSignal("top.a");
    DisplayItem b = DisplayModel::makeSignal("top.a");
    REQUIRE(a.id != 0);
    REQUIRE(a.id != b.id);
    DisplayItem c = a;
    REQUIRE(c.id == a.id);
    DisplayModel m = flat();
    const DisplayModel copy = m;
    REQUIRE(allItemIds(copy) == allItemIds(m));
    auto parsed = DisplayModel::parse(m.serialize()); // ids are not part of the format
    REQUIRE(parsed.ok());
    REQUIRE(allItemIds(parsed.value()) != allItemIds(m));
}

TEST_CASE("locate, findItem and descendants") {
    DisplayModel m = nested();
    const auto d = locate(m, idOf(m, "d"));
    REQUIRE(d);
    REQUIRE(d->parent == idOf(m, "Inner"));
    REQUIRE(d->index == 0);
    REQUIRE(d->depth == 2);
    REQUIRE(locate(m, idOf(m, "a"))->parent == kTopLevel);
    REQUIRE_FALSE(locate(m, 999999999));
    REQUIRE(findItem(m, 999999999) == nullptr);
    REQUIRE(isSameOrDescendant(m, idOf(m, "G1"), idOf(m, "d")));
    REQUIRE(isSameOrDescendant(m, idOf(m, "G1"), idOf(m, "G1")));
    REQUIRE_FALSE(isSameOrDescendant(m, idOf(m, "G2"), idOf(m, "d")));
}

TEST_CASE("normalizeSelection drops duplicates, unknown ids and descendants, in display order") {
    DisplayModel m = nested();
    const auto sel = normalizeSelection(
        m, {idOf(m, "g"), idOf(m, "d"), idOf(m, "G1"), idOf(m, "g"), 12345678, idOf(m, "a")});
    REQUIRE(sel == (std::vector<ItemId>{idOf(m, "a"), idOf(m, "G1"), idOf(m, "g")}));
}

TEST_CASE("moveItems one step up and down") {
    DisplayModel m = flat();
    const auto idsBefore = allItemIds(m);
    auto r = moveItems(m, ids(m, {"c"}), -1);
    REQUIRE(r.ok());
    REQUIRE(r.value().changed);
    REQUIRE(shape(m) == "a c b d e");
    REQUIRE(r.value().moved == ids(m, {"c"}));
    REQUIRE(r.value().parents == std::vector<ItemId>{kTopLevel});
    REQUIRE(r.value().created.empty());
    REQUIRE(r.value().removed.empty());

    REQUIRE(moveItems(m, ids(m, {"c"}), 1).ok());
    REQUIRE(shape(m) == "a b c d e");
    auto after = allItemIds(m);
    std::sort(after.begin(), after.end());
    auto sortedBefore = idsBefore;
    std::sort(sortedBefore.begin(), sortedBefore.end());
    REQUIRE(after == sortedBefore); // ids are stable
}

TEST_CASE("moveItems at the edge is a successful no-op") {
    DisplayModel m = flat();
    auto up = moveItems(m, ids(m, {"a"}), -1);
    REQUIRE(up.ok());
    REQUIRE_FALSE(up.value().changed);
    REQUIRE(up.value().moved.empty());
    auto down = moveItems(m, ids(m, {"e"}), 1);
    REQUIRE(down.ok());
    REQUIRE_FALSE(down.value().changed);
    REQUIRE(shape(m) == "a b c d e");
    REQUIRE(moveItems(m, ids(m, {"b"}), 0).ok());
}

TEST_CASE("moveItems by several places clamps") {
    DisplayModel m = flat();
    REQUIRE(moveItems(m, ids(m, {"b"}), 2).ok());
    REQUIRE(shape(m) == "a c d b e");
    REQUIRE(moveItems(m, ids(m, {"b"}), 100).ok());
    REQUIRE(shape(m) == "a c d e b");
    REQUIRE(moveItems(m, ids(m, {"b"}), -100).ok());
    REQUIRE(shape(m) == "b a c d e");
}

TEST_CASE("moveItems with a multi selection keeps the relative order") {
    DisplayModel m = flat();
    REQUIRE(moveItems(m, ids(m, {"d", "b"}), -1).ok()); // argument order does not matter
    REQUIRE(shape(m) == "b a d c e");
    REQUIRE(moveItems(m, ids(m, {"b", "d"}), 1).ok());
    REQUIRE(shape(m) == "a b c d e");
}

TEST_CASE("a selected block that hits the top stays together while the rest moves") {
    DisplayModel m = flat();
    REQUIRE(moveItems(m, ids(m, {"a", "b", "d"}), -1).ok());
    REQUIRE(shape(m) == "a b d c e"); // a and b are already on top, d moved
    REQUIRE(moveItems(m, ids(m, {"d", "e"}), 1).ok());
    REQUIRE(shape(m) == "a b c d e");
    // block at the bottom
    REQUIRE(moveItems(m, ids(m, {"d", "e", "b"}), 1).ok());
    REQUIRE(shape(m) == "a c b d e");
}

TEST_CASE("moveItems works among siblings of different parents at once") {
    DisplayModel m = nested();
    auto r = moveItems(m, ids(m, {"c", "f"}), -1); // c inside G1, f inside G2
    REQUIRE(r.ok());
    REQUIRE(shape(m) == "a [G1: c b [Inner: d]] -div- [G2: f e] g");
    REQUIRE(r.value().parents.size() == 2);
    REQUIRE(r.value().moved.size() == 2);
}

TEST_CASE("an item never leaves its group by moving") {
    DisplayModel m = nested();
    auto r = moveItems(m, ids(m, {"b"}), -1);
    REQUIRE(r.ok());
    REQUIRE_FALSE(r.value().changed);
    REQUIRE(shape(m) == "a [G1: b c [Inner: d]] -div- [G2: e f] g");
}

TEST_CASE("moving a group moves its children with it") {
    DisplayModel m = nested();
    REQUIRE(moveItems(m, ids(m, {"G1"}), 1).ok());
    REQUIRE(shape(m) == "a -div- [G1: b c [Inner: d]] [G2: e f] g");
    REQUIRE(moveItems(m, ids(m, {"G2"}), -2).ok());
    REQUIRE(shape(m) == "a [G2: e f] -div- [G1: b c [Inner: d]] g");
}

TEST_CASE("a divider moves like a signal") {
    DisplayModel m = nested();
    REQUIRE(moveItems(m, ids(m, {"div"}), -1).ok());
    REQUIRE(shape(m) == "a -div- [G1: b c [Inner: d]] [G2: e f] g");
}

TEST_CASE("selecting a group and its child moves the group only") {
    DisplayModel m = nested();
    REQUIRE(moveItems(m, ids(m, {"G1", "c"}), 1).ok());
    REQUIRE(shape(m) == "a -div- [G1: b c [Inner: d]] [G2: e f] g");
}

TEST_CASE("moveItems and friends fail on unknown ids and empty selections without changes") {
    DisplayModel m = flat();
    const std::string s = shape(m);
    REQUIRE_FALSE(moveItems(m, {424242}, -1).ok());
    REQUIRE_FALSE(moveItems(m, {}, -1).ok());
    REQUIRE_FALSE(moveItemsToEdge(m, {424242}, true).ok());
    REQUIRE_FALSE(moveItemsToGroup(m, {424242}, kTopLevel, 0).ok());
    REQUIRE_FALSE(wrapInGroup(m, {424242}).ok());
    REQUIRE_FALSE(wrapInGroup(m, {}).ok());
    REQUIRE_FALSE(renameItem(m, 424242, "x").ok());
    REQUIRE_FALSE(removeItemsById(m, {424242}).ok());
    REQUIRE_FALSE(ungroup(m, {424242}).ok());
    REQUIRE(shape(m) == s);
    REQUIRE_FALSE(moveItems(m, {424242}, -1).error().empty());
}

TEST_CASE("moveItemsToEdge") {
    DisplayModel m = flat();
    auto r = moveItemsToEdge(m, ids(m, {"c", "d"}), true);
    REQUIRE(r.ok());
    REQUIRE(shape(m) == "c d a b e");
    REQUIRE(r.value().changed);
    REQUIRE(moveItemsToEdge(m, ids(m, {"c", "d"}), true).value().changed == false);
    REQUIRE(moveItemsToEdge(m, ids(m, {"c", "a"}), false).ok());
    REQUIRE(shape(m) == "d b e c a"); // relative order of c and a is the order they had: c before a
}

TEST_CASE("moveItemsToEdge acts inside the parent") {
    DisplayModel m = nested();
    REQUIRE(moveItemsToEdge(m, ids(m, {"c"}), true).ok());
    REQUIRE(shape(m) == "a [G1: c b [Inner: d]] -div- [G2: e f] g");
    REQUIRE(moveItemsToEdge(m, ids(m, {"b"}), false).ok());
    REQUIRE(shape(m) == "a [G1: c [Inner: d] b] -div- [G2: e f] g");
}

TEST_CASE("moveItemsToGroup into a group at an index") {
    DisplayModel m = nested();
    auto r = moveItemsToGroup(m, ids(m, {"a", "g"}), idOf(m, "G2"), 1);
    REQUIRE(r.ok());
    REQUIRE(shape(m) == "[G1: b c [Inner: d]] -div- [G2: e a g f]");
    REQUIRE(r.value().changed);
    REQUIRE(r.value().moved == ids(m, {"a", "g"}));
    REQUIRE(r.value().parents.size() >= 2);
}

TEST_CASE("moveItemsToGroup to the top level, before and after a group") {
    DisplayModel m = nested();
    REQUIRE(moveItemsToGroup(m, ids(m, {"d"}), kTopLevel, 0).ok());
    REQUIRE(shape(m) == "d a [G1: b c [Inner: ]] -div- [G2: e f] g");
    REQUIRE(
        moveItemsToGroup(m, ids(m, {"b", "e"}), m.root().id, 100).ok()); // root id is accepted, index clamps
    REQUIRE(shape(m) == "d a [G1: c [Inner: ]] -div- [G2: f] g b e");
}

TEST_CASE("moveItemsToGroup keeps the relative display order of items from different parents") {
    DisplayModel m = nested();
    REQUIRE(moveItemsToGroup(m, ids(m, {"g", "e", "b"}), idOf(m, "Inner"), 0).ok());
    REQUIRE(shape(m) == "a [G1: c [Inner: b e g d]] -div- [G2: f]");
}

TEST_CASE("moveItemsToGroup index counts positions before the move") {
    DisplayModel m = flat();
    // move b to index 3 of the root: lands between c and d (index 3 before removal means after c)
    REQUIRE(moveItemsToGroup(m, ids(m, {"b"}), kTopLevel, 3).ok());
    REQUIRE(shape(m) == "a c b d e");
    // moving to its own place is a no-op
    DisplayModel k = flat();
    auto same = moveItemsToGroup(k, ids(k, {"b"}), kTopLevel, 1);
    REQUIRE(same.ok());
    REQUIRE_FALSE(same.value().changed);
    auto same2 = moveItemsToGroup(k, ids(k, {"b"}), kTopLevel, 2);
    REQUIRE(same2.ok());
    REQUIRE_FALSE(same2.value().changed);
    REQUIRE(shape(k) == "a b c d e");
}

TEST_CASE("a group cannot move into itself or its descendants") {
    DisplayModel m = nested();
    const std::string s = shape(m);
    REQUIRE_FALSE(moveItemsToGroup(m, ids(m, {"G1"}), idOf(m, "G1"), 0).ok());
    REQUIRE_FALSE(moveItemsToGroup(m, ids(m, {"G1"}), idOf(m, "Inner"), 0).ok());
    REQUIRE_FALSE(
        moveItemsToGroup(m, ids(m, {"G1", "a"}), idOf(m, "Inner"), 0).ok()); // one bad item refuses all
    REQUIRE(shape(m) == s);
    REQUIRE(moveItemsToGroup(m, ids(m, {"Inner"}), idOf(m, "G2"), 0).ok()); // sideways is fine
    REQUIRE(shape(m) == "a [G1: b c] -div- [G2: [Inner: d] e f] g");
}

TEST_CASE("the destination must be a group") {
    DisplayModel m = nested();
    REQUIRE_FALSE(moveItemsToGroup(m, ids(m, {"a"}), idOf(m, "g"), 0).ok());
    REQUIRE_FALSE(moveItemsToGroup(m, ids(m, {"a"}), idOf(m, "div"), 0).ok());
    REQUIRE_FALSE(moveItemsToGroup(m, ids(m, {"a"}), 777777, 0).ok());
}

TEST_CASE("moving a group into a parent that has a group of the same name is refused") {
    DisplayModel m;
    DisplayItem a = DisplayModel::makeGroup("A");
    a.children.push_back(DisplayModel::makeGroup("X"));
    m.root().children.push_back(std::move(a));
    m.root().children.push_back(DisplayModel::makeGroup("X"));
    const std::string s = shape(m);
    auto r = moveItemsToGroup(m, {m.root().children[1].id}, m.root().children[0].id, 0);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().find("already exists") != std::string::npos);
    REQUIRE(shape(m) == s);
}

TEST_CASE("wrapInGroup puts items in a new group where the first was") {
    DisplayModel m = flat();
    auto r = wrapInGroup(m, ids(m, {"d", "b"}), "Pair");
    REQUIRE(r.ok());
    REQUIRE(shape(m) == "a [Pair: b d] c e");
    REQUIRE(r.value().created.size() == 1);
    REQUIRE(findItem(m, r.value().created[0])->label == "Pair");
    REQUIRE(findItem(m, r.value().created[0])->expanded);
    REQUIRE(r.value().changed);
    REQUIRE(r.value().moved == ids(m, {"b", "d"}));
}

TEST_CASE("the default group name is Group and unique") {
    DisplayModel m = flat();
    REQUIRE(wrapInGroup(m, ids(m, {"a"})).ok());
    REQUIRE(shape(m) == "[Group: a] b c d e");
    REQUIRE(wrapInGroup(m, ids(m, {"b"})).ok());
    REQUIRE(wrapInGroup(m, ids(m, {"c"})).ok());
    REQUIRE(shape(m) == "[Group: a] [Group 2: b] [Group 3: c] d e");
    REQUIRE(uniqueGroupName(m) == "Group 4");
    REQUIRE(uniqueGroupName(m, "Bus") == "Bus");
    // a nested group counts too
    REQUIRE(wrapInGroup(m, ids(m, {"d", "e"}), "Outer").ok());
    REQUIRE(moveItemsToGroup(m, ids(m, {"Group 3"}), idOf(m, "Outer"), 0).ok());
    REQUIRE(uniqueGroupName(m) == "Group 4");
}

TEST_CASE("wrapInGroup across parents puts the group into the parent of the first item") {
    DisplayModel m = nested();
    auto r = wrapInGroup(m, ids(m, {"c", "g"}), "Mix");
    REQUIRE(r.ok());
    REQUIRE(shape(m) == "a [G1: b [Mix: c g] [Inner: d]] -div- [G2: e f]");
}

TEST_CASE("wrapInGroup wraps groups and dividers, nesting them") {
    DisplayModel m = nested();
    REQUIRE(wrapInGroup(m, ids(m, {"div", "G2"}), "Tail").ok());
    REQUIRE(shape(m) == "a [G1: b c [Inner: d]] [Tail: -div- [G2: e f]] g");
}

TEST_CASE("wrapInGroup validates the name") {
    DisplayModel m = nested();
    const std::string s = shape(m);
    REQUIRE_FALSE(wrapInGroup(m, ids(m, {"a"}), "G1").ok());  // clash at the same level
    REQUIRE_FALSE(wrapInGroup(m, ids(m, {"a"}), "x/y").ok()); // '/' separates levels in the .do export
    REQUIRE_FALSE(wrapInGroup(m, ids(m, {"a"}), "x\ny").ok());
    REQUIRE(shape(m) == s);
    REQUIRE(wrapInGroup(m, ids(m, {"d"}), "G1").ok()); // another level: allowed
    REQUIRE(wrapInGroup(m, ids(m, {"G2"}), "  padded  ").ok());
    REQUIRE(findItem(m, idOf(m, "padded")) != nullptr);
}

TEST_CASE("ungroup replaces a group by its children in place") {
    DisplayModel m = nested();
    auto r = ungroup(m, ids(m, {"G1"}));
    REQUIRE(r.ok());
    REQUIRE(shape(m) == "a b c [Inner: d] -div- [G2: e f] g");
    REQUIRE(r.value().removed == std::vector<ItemId>{r.value().removed[0]});
    REQUIRE(findItem(m, r.value().removed[0]) == nullptr);
    REQUIRE(r.value().moved.size() == 3);
    REQUIRE(r.value().changed);
}

TEST_CASE("ungroup of several groups, a mixed selection and an empty group") {
    DisplayModel m = nested();
    REQUIRE(ungroup(m, ids(m, {"G1", "G2", "a"})).ok()); // a is not a group: ignored
    REQUIRE(shape(m) == "a b c [Inner: d] -div- e f g");
    DisplayModel k;
    k.root().children.push_back(DisplayModel::makeGroup("Empty"));
    k.root().children.push_back(DisplayModel::makeSignal("top.s"));
    REQUIRE(ungroup(k, ids(k, {"Empty"})).ok());
    REQUIRE(shape(k) == "s");
}

TEST_CASE("ungroup of a group and the group inside it") {
    DisplayModel m = nested();
    REQUIRE(ungroup(m, ids(m, {"Inner", "G1"})).ok());
    REQUIRE(shape(m) == "a b c d -div- [G2: e f] g");
}

TEST_CASE("ungroup fails without a group and on a name clash, leaving the model alone") {
    DisplayModel m = flat();
    REQUIRE_FALSE(ungroup(m, ids(m, {"a"})).ok());
    DisplayModel k;
    DisplayItem x = DisplayModel::makeGroup("X");
    x.children.push_back(DisplayModel::makeGroup("Y"));
    k.root().children.push_back(std::move(x));
    k.root().children.push_back(DisplayModel::makeGroup("Y"));
    const std::string s = shape(k);
    REQUIRE_FALSE(ungroup(k, {k.root().children[0].id}).ok());
    REQUIRE(shape(k) == s);
}

TEST_CASE("wrap then ungroup restores the tree") {
    DisplayModel m = nested();
    const std::string s = shape(m);
    auto w = wrapInGroup(m, ids(m, {"e", "f"}), "W");
    REQUIRE(w.ok());
    REQUIRE(ungroup(m, w.value().created).ok());
    REQUIRE(shape(m) == s);
}

TEST_CASE("renameItem on groups, dividers, buses and signals") {
    DisplayModel m = nested();
    m.root().children.push_back(DisplayModel::makeBus("addr", {"top.a"}));
    auto g = renameItem(m, idOf(m, "G1"), "Renamed");
    REQUIRE(g.ok());
    REQUIRE(g.value().changed);
    REQUIRE(g.value().renamed == std::vector<ItemId>{findItem(m, g.value().renamed[0])->id});
    REQUIRE(shape(m).find("[Renamed: ") != std::string::npos);

    REQUIRE(renameItem(m, idOf(m, "div"), "section").ok());
    REQUIRE(renameItem(m, idOf(m, "addr"), "address").ok());
    REQUIRE(renameItem(m, idOf(m, "a"), "alpha").ok());
    REQUIRE(findItem(m, idOf(m, "a"))->label == "alpha");
    REQUIRE(findItem(m, idOf(m, "a"))->displayName() == "alpha");
    REQUIRE(findItem(m, idOf(m, "a"))->path == "top.a"); // the path is untouched
    REQUIRE(renameItem(m, idOf(m, "a"), "").ok());       // empty signal label: show the path again
    REQUIRE(findItem(m, idOf(m, "a"))->displayName() == "top.a");
    REQUIRE(renameItem(m, idOf(m, "a"), "top.a").ok());
    REQUIRE(findItem(m, idOf(m, "a"))->label.empty());
}

TEST_CASE("renameItem validation") {
    DisplayModel m = nested();
    const std::string s = shape(m);
    REQUIRE_FALSE(renameItem(m, idOf(m, "G1"), "").ok());
    REQUIRE_FALSE(renameItem(m, idOf(m, "G1"), "   ").ok());
    REQUIRE_FALSE(renameItem(m, idOf(m, "G1"), "a/b").ok());
    REQUIRE_FALSE(renameItem(m, idOf(m, "G1"), "G2").ok()); // sibling clash
    REQUIRE_FALSE(renameItem(m, idOf(m, "G1"), "x\ny").ok());
    REQUIRE_FALSE(renameItem(m, idOf(m, "div"), "x\ny").ok());
    REQUIRE(shape(m) == s);
    REQUIRE(renameItem(m, idOf(m, "G1"), "G1").ok()); // same name: no change, no error
    REQUIRE_FALSE(renameItem(m, idOf(m, "G1"), "G1").value().changed);
    REQUIRE(renameItem(m, idOf(m, "Inner"), "G1").ok()); // other level
    REQUIRE(renameItem(m, idOf(m, "div"), "").ok());     // a divider may be empty
    REQUIRE(findItem(m, idOf(m, "g")) != nullptr);
    REQUIRE(shape(m).find("--") != std::string::npos);
}

TEST_CASE("insertItem and removeItemsById") {
    DisplayModel m = nested();
    DisplayItem d = DisplayModel::makeDivider("new");
    const ItemId did = d.id;
    auto r = insertItem(m, idOf(m, "G2"), 1, std::move(d));
    REQUIRE(r.ok());
    REQUIRE(r.value().created == std::vector<ItemId>{did});
    REQUIRE(shape(m) == "a [G1: b c [Inner: d]] -div- [G2: e -new- f] g");
    REQUIRE(insertItem(m, kTopLevel, 1000, DisplayModel::makeDivider("end")).ok());
    REQUIRE_FALSE(insertItem(m, idOf(m, "a"), 0, DisplayModel::makeDivider("x")).ok());
    auto dup = insertItem(m, kTopLevel, 0, DisplayModel::makeGroup("G1"));
    REQUIRE(dup.ok());
    REQUIRE(findItem(m, dup.value().created[0])->label == "G1 2");

    auto rm = removeItemsById(m, ids(m, {"G1", "c", "new"})); // c is below G1 and goes with it
    REQUIRE(rm.ok());
    REQUIRE(rm.value().removed.size() == 2);
    REQUIRE(shape(m) == "[G1 2: ] a -div- [G2: e f] g -end-");
}

TEST_CASE("operations keep the content of items intact") {
    DisplayModel m = flat();
    DisplayItem& b = m.root().children[1];
    b.radix = Radix::Hex;
    b.color = 0x112233u;
    b.analog.enabled = true;
    b.analog.heightRows = 7;
    b.showBits = true;
    const ItemId id = b.id;
    REQUIRE(wrapInGroup(m, {id}, "G").ok());
    REQUIRE(moveItemsToGroup(m, {id}, kTopLevel, 0).ok());
    REQUIRE(moveItems(m, {id}, 3).ok());
    const DisplayItem* it = findItem(m, id);
    REQUIRE(it != nullptr);
    REQUIRE(it->radix == Radix::Hex);
    REQUIRE(it->color == 0x112233u);
    REQUIRE(it->analog.enabled);
    REQUIRE(it->analog.heightRows == 7);
    REQUIRE(it->showBits);
}

TEST_CASE("serialize and parse keep the order after edits") {
    DisplayModel m = nested();
    REQUIRE(moveItems(m, ids(m, {"G2"}), -3).ok());
    REQUIRE(wrapInGroup(m, ids(m, {"a", "g"}), "Ends").ok());
    REQUIRE(renameItem(m, idOf(m, "Inner"), "Deep name").ok());
    REQUIRE(renameItem(m, idOf(m, "div"), "").ok());
    findItem(m, idOf(m, "G1"))->expanded = false;
    auto parsed = DisplayModel::parse(m.serialize());
    REQUIRE(parsed.ok());
    REQUIRE(shape(parsed.value()) == shape(m));
    REQUIRE(parsed.value().serialize() == m.serialize());
}

TEST_CASE("moving one row in a 5000 row model is fast") {
    DisplayModel m;
    for (int i = 0; i < 5000; ++i)
        m.root().children.push_back(DisplayModel::makeSignal("top.s" + std::to_string(i)));
    const ItemId id = m.root().children[2500].id;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
        REQUIRE(moveItems(m, {id}, i % 2 == 0 ? -1 : 1).ok());
    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0;
    WARN("move of one row among 5000: " << ms << " ms");
    REQUIRE(ms < 100.0);
}

TEST_CASE("checkMoveToGroup explains refusals without touching the model") {
    DisplayModel m = nested();
    const std::string s = shape(m);
    REQUIRE(checkMoveToGroup(m, ids(m, {"a"}), idOf(m, "G2")).empty());
    REQUIRE(checkMoveToGroup(m, ids(m, {"a"}), kTopLevel).empty());
    REQUIRE_FALSE(checkMoveToGroup(m, ids(m, {"G1"}), idOf(m, "Inner")).empty());
    REQUIRE_FALSE(checkMoveToGroup(m, ids(m, {"a"}), idOf(m, "g")).empty());
    REQUIRE_FALSE(checkMoveToGroup(m, {31337}, kTopLevel).empty());
    REQUIRE_FALSE(checkMoveToGroup(m, {}, kTopLevel).empty());
    REQUIRE(shape(m) == s);
}
