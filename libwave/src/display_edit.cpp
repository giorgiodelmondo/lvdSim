// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/display_edit.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace lvd::wave {

namespace {

using Out = Result<EditOutcome>;

DisplayItem* findIn(DisplayItem& group, ItemId id) {
    for (DisplayItem& c : group.children) {
        if (c.id == id)
            return &c;
        if (c.kind == ItemKind::Group)
            if (DisplayItem* f = findIn(c, id))
                return f;
    }
    return nullptr;
}

const DisplayItem* findIn(const DisplayItem& group, ItemId id) {
    return findIn(const_cast<DisplayItem&>(group), id);
}

bool locateIn(const DisplayItem& group, ItemId groupId, ItemId id, int depth, ItemLocation& out) {
    for (std::size_t i = 0; i < group.children.size(); ++i) {
        const DisplayItem& c = group.children[i];
        if (c.id == id) {
            out = {groupId, i, depth};
            return true;
        }
        if (c.kind == ItemKind::Group && locateIn(c, c.id, id, depth + 1, out))
            return true;
    }
    return false;
}

void collectIds(const DisplayItem& group, std::vector<ItemId>& out) {
    for (const DisplayItem& c : group.children) {
        out.push_back(c.id);
        if (c.kind == ItemKind::Group)
            collectIds(c, out);
    }
}

// Position of every item as (parent id or kTopLevel, index), used to find out what an operation changed.
using Placement = std::unordered_map<ItemId, std::pair<ItemId, std::size_t>>;

void placementIn(const DisplayItem& group, ItemId groupId, Placement& out) {
    for (std::size_t i = 0; i < group.children.size(); ++i) {
        const DisplayItem& c = group.children[i];
        out[c.id] = {groupId, i};
        if (c.kind == ItemKind::Group)
            placementIn(c, c.id, out);
    }
}

Placement placementOf(const DisplayModel& m) {
    Placement p;
    placementIn(m.root(), kTopLevel, p);
    return p;
}

ItemId canonicalParent(const DisplayModel& m, ItemId parent) {
    return parent == m.root().id ? kTopLevel : parent;
}

DisplayItem* groupFor(DisplayModel& m, ItemId parent) {
    if (parent == kTopLevel || parent == m.root().id)
        return &m.root();
    DisplayItem* g = findIn(m.root(), parent);
    return g != nullptr && g->kind == ItemKind::Group ? g : nullptr;
}

bool hasGroupNamed(const DisplayItem& parent, const std::string& label,
                   const std::unordered_set<ItemId>& ignore) {
    for (const DisplayItem& c : parent.children)
        if (c.kind == ItemKind::Group && c.label == label && ignore.count(c.id) == 0)
            return true;
    return false;
}

std::string trimmed(const std::string& s) {
    const char* ws = " \t";
    const std::size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

// Removes the marked items (not descending into marked groups) from the tree and stores them by id.
void extractMarked(DisplayItem& group, const std::unordered_set<ItemId>& marked,
                   std::unordered_map<ItemId, DisplayItem>& out) {
    std::vector<DisplayItem> keep;
    keep.reserve(group.children.size());
    for (DisplayItem& c : group.children) {
        if (marked.count(c.id) != 0) {
            out.emplace(c.id, std::move(c));
        } else {
            if (c.kind == ItemKind::Group)
                extractMarked(c, marked, out);
            keep.push_back(std::move(c));
        }
    }
    group.children = std::move(keep);
}

Out unknownItem() {
    return Out::failure("the item does not exist any more");
}

// Fills the outcome by comparing the placement before and after.
EditOutcome diff(const DisplayModel& after, const Placement& before, const std::vector<ItemId>& requested) {
    EditOutcome o;
    const Placement now = placementOf(after);
    std::set<ItemId> parents;
    for (const auto& [id, was] : before) {
        const auto it = now.find(id);
        if (it == now.end())
            continue;
        if (it->second != was) {
            parents.insert(was.first);
            parents.insert(it->second.first);
        }
    }
    for (ItemId id : requested) {
        const auto b = before.find(id);
        const auto a = now.find(id);
        if (b != before.end() && a != now.end() && b->second != a->second)
            o.moved.push_back(id);
    }
    o.parents.assign(parents.begin(), parents.end());
    o.changed = !parents.empty();
    return o;
}

// Fails on an unknown id. Otherwise returns the normalized selection.
bool resolve(const DisplayModel& m, const std::vector<ItemId>& ids, std::vector<ItemId>& out) {
    for (ItemId id : ids)
        if (findIn(m.root(), id) == nullptr)
            return false;
    out = normalizeSelection(m, ids);
    return true;
}

} // namespace

DisplayItem* findItem(DisplayModel& model, ItemId id) {
    return findIn(model.root(), id);
}
const DisplayItem* findItem(const DisplayModel& model, ItemId id) {
    return findIn(model.root(), id);
}

std::optional<ItemLocation> locate(const DisplayModel& model, ItemId id) {
    ItemLocation loc;
    if (locateIn(model.root(), kTopLevel, id, 0, loc))
        return loc;
    return std::nullopt;
}

bool isSameOrDescendant(const DisplayModel& model, ItemId ancestor, ItemId candidate) {
    if (ancestor == candidate)
        return true;
    const DisplayItem* a = findIn(model.root(), ancestor);
    return a != nullptr && findIn(*a, candidate) != nullptr;
}

std::vector<ItemId> allItemIds(const DisplayModel& model) {
    std::vector<ItemId> v;
    collectIds(model.root(), v);
    return v;
}

std::vector<ItemId> normalizeSelection(const DisplayModel& model, const std::vector<ItemId>& ids) {
    const std::unordered_set<ItemId> want(ids.begin(), ids.end());
    std::vector<ItemId> out;
    // Depth first walk: a selected group swallows its subtree, so its descendants are never visited.
    struct Walk {
        const std::unordered_set<ItemId>& want;
        std::vector<ItemId>& out;
        void run(const DisplayItem& g) {
            for (const DisplayItem& c : g.children) {
                if (want.count(c.id) != 0) {
                    out.push_back(c.id);
                    continue;
                }
                if (c.kind == ItemKind::Group)
                    run(c);
            }
        }
    } w{want, out};
    w.run(model.root());
    return out;
}

std::string uniqueGroupName(const DisplayModel& model, const std::string& base) {
    std::set<std::string> used;
    struct Walk {
        std::set<std::string>& used;
        void run(const DisplayItem& g) {
            for (const DisplayItem& c : g.children)
                if (c.kind == ItemKind::Group) {
                    used.insert(c.label);
                    run(c);
                }
        }
    } w{used};
    w.run(model.root());
    if (used.count(base) == 0)
        return base;
    for (int n = 2;; ++n) {
        std::string s = base + " " + std::to_string(n);
        if (used.count(s) == 0)
            return s;
    }
}

// ---------------------------------------------------------------------------------------------

Out moveItems(DisplayModel& model, const std::vector<ItemId>& ids, int delta) {
    std::vector<ItemId> sel;
    if (!resolve(model, ids, sel))
        return unknownItem();
    if (sel.empty())
        return Out::failure("nothing selected to move");
    const Placement before = placementOf(model);
    if (delta != 0) {
        // Per parent, the set of selected child ids.
        std::map<ItemId, std::unordered_set<ItemId>> perParent;
        for (ItemId id : sel)
            perParent[before.at(id).first].insert(id);
        const int steps = delta < 0 ? -delta : delta;
        for (auto& [parentId, chosen] : perParent) {
            DisplayItem* parent = groupFor(model, parentId);
            if (parent == nullptr)
                continue;
            auto& ch = parent->children;
            for (int step = 0; step < steps; ++step) {
                bool any = false;
                std::vector<char> fixed(ch.size(), 0); // selected items that cannot move any further
                if (delta < 0) {
                    for (std::size_t i = 0; i < ch.size(); ++i) {
                        if (chosen.count(ch[i].id) == 0)
                            continue;
                        if (i == 0 || fixed[i - 1] != 0) {
                            fixed[i] = 1;
                            continue;
                        }
                        std::swap(ch[i], ch[i - 1]);
                        any = true;
                    }
                } else {
                    for (std::size_t i = ch.size(); i-- > 0;) {
                        if (chosen.count(ch[i].id) == 0)
                            continue;
                        if (i + 1 == ch.size() || fixed[i + 1] != 0) {
                            fixed[i] = 1;
                            continue;
                        }
                        std::swap(ch[i], ch[i + 1]);
                        any = true;
                    }
                }
                if (!any)
                    break;
            }
        }
    }
    return diff(model, before, sel);
}

Out moveItemsToEdge(DisplayModel& model, const std::vector<ItemId>& ids, bool toTop) {
    std::vector<ItemId> sel;
    if (!resolve(model, ids, sel))
        return unknownItem();
    if (sel.empty())
        return Out::failure("nothing selected to move");
    const Placement before = placementOf(model);
    std::map<ItemId, std::unordered_set<ItemId>> perParent;
    for (ItemId id : sel)
        perParent[before.at(id).first].insert(id);
    for (auto& [parentId, chosen] : perParent) {
        DisplayItem* parent = groupFor(model, parentId);
        if (parent == nullptr)
            continue;
        auto& ch = parent->children;
        auto isChosen = [&](const DisplayItem& d) { return chosen.count(d.id) != 0; };
        if (toTop)
            std::stable_partition(ch.begin(), ch.end(), isChosen);
        else
            std::stable_partition(ch.begin(), ch.end(), [&](const DisplayItem& d) { return !isChosen(d); });
    }
    return diff(model, before, sel);
}

std::string checkMoveToGroup(const DisplayModel& model, const std::vector<ItemId>& ids, ItemId parentId) {
    for (ItemId id : ids)
        if (findIn(model.root(), id) == nullptr)
            return "the item does not exist any more";
    const std::vector<ItemId> sel = normalizeSelection(model, ids);
    if (sel.empty())
        return "nothing selected to move";
    parentId = canonicalParent(model, parentId);
    const DisplayItem* target = parentId == kTopLevel ? &model.root() : findIn(model.root(), parentId);
    if (target == nullptr || target->kind != ItemKind::Group)
        return "the destination is not a group";
    if (parentId != kTopLevel)
        for (ItemId id : sel)
            if (isSameOrDescendant(model, id, parentId))
                return "a group cannot be moved into itself or into one of its own groups";
    const std::unordered_set<ItemId> chosen(sel.begin(), sel.end());
    std::set<std::string>
        arriving; // names of moved groups: they must differ from each other and from the parent's
    for (ItemId id : sel) {
        const DisplayItem* it = findIn(model.root(), id);
        if (it->kind != ItemKind::Group)
            continue;
        if (hasGroupNamed(*target, it->label, chosen) || !arriving.insert(it->label).second)
            return "a group named \"" + it->label + "\" already exists in the destination";
    }
    return {};
}

Out moveItemsToGroup(DisplayModel& model, const std::vector<ItemId>& ids, ItemId parentId,
                     std::size_t index) {
    if (const std::string why = checkMoveToGroup(model, ids, parentId); !why.empty())
        return Out::failure(why);
    const std::vector<ItemId> sel = normalizeSelection(model, ids);
    parentId = canonicalParent(model, parentId);
    DisplayItem* target = groupFor(model, parentId);
    const std::unordered_set<ItemId> chosen(sel.begin(), sel.end());

    const Placement before = placementOf(model);
    index = std::min(index, target->children.size());
    std::size_t adjusted = index;
    for (std::size_t i = 0; i < index; ++i)
        if (chosen.count(target->children[i].id) != 0)
            --adjusted;

    std::unordered_map<ItemId, DisplayItem> taken;
    extractMarked(model.root(), chosen, taken);
    target = groupFor(model, parentId); // extraction rebuilt the child vectors
    adjusted = std::min(adjusted, target->children.size());
    std::vector<DisplayItem> block;
    for (ItemId id : sel)
        block.push_back(std::move(taken.at(id)));
    target->children.insert(target->children.begin() + static_cast<std::ptrdiff_t>(adjusted),
                            std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
    return diff(model, before, sel);
}

Out wrapInGroup(DisplayModel& model, const std::vector<ItemId>& ids, std::string label) {
    std::vector<ItemId> sel;
    if (!resolve(model, ids, sel))
        return unknownItem();
    if (sel.empty())
        return Out::failure("nothing selected to group");
    label = trimmed(label);
    if (label.find_first_of("\r\n/") != std::string::npos)
        return Out::failure("a group name cannot contain '/' or a line break");
    const Placement before = placementOf(model);
    const auto [parentId, firstIndex] = before.at(sel.front());
    DisplayItem* parent = groupFor(model, parentId);
    const std::unordered_set<ItemId> chosen(sel.begin(), sel.end());
    if (label.empty()) {
        label = uniqueGroupName(model);
    } else if (hasGroupNamed(*parent, label, chosen)) {
        return Out::failure("a group named \"" + label + "\" already exists here");
    }
    {
        std::set<std::string> seen; // wrapped groups keep their names inside the new group: they must differ
        for (ItemId id : sel) {
            const DisplayItem* it = findIn(model.root(), id);
            if (it->kind == ItemKind::Group && !seen.insert(it->label).second)
                return Out::failure("two of the groups to wrap have the name \"" + it->label + "\"");
        }
    }

    // Nothing before the first selected item of its parent is selected, so its index stays valid.
    std::unordered_map<ItemId, DisplayItem> taken;
    extractMarked(model.root(), chosen, taken);
    parent = groupFor(model, parentId);
    DisplayItem group = DisplayModel::makeGroup(std::move(label));
    const ItemId groupId = group.id;
    for (ItemId id : sel)
        group.children.push_back(std::move(taken.at(id)));
    const std::size_t at = std::min(firstIndex, parent->children.size());
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(at), std::move(group));

    EditOutcome o = diff(model, before, sel);
    o.changed = true;
    o.created.push_back(groupId);
    o.parents.push_back(parentId);
    std::sort(o.parents.begin(), o.parents.end());
    o.parents.erase(std::unique(o.parents.begin(), o.parents.end()), o.parents.end());
    return o;
}

Out ungroup(DisplayModel& model, const std::vector<ItemId>& ids) {
    for (ItemId id : ids)
        if (findIn(model.root(), id) == nullptr)
            return unknownItem();
    std::vector<ItemId> groups;
    {
        const std::unordered_set<ItemId> want(ids.begin(), ids.end());
        for (ItemId id : allItemIds(model)) { // outer groups first
            if (want.count(id) == 0)
                continue;
            if (findIn(model.root(), id)->kind == ItemKind::Group)
                groups.push_back(id);
        }
    }
    if (groups.empty())
        return Out::failure("select a group to ungroup");

    const Placement before = placementOf(model);
    DisplayModel work = model; // the clash check depends on the order of the steps: trial on a copy
    EditOutcome o;
    std::vector<ItemId> kids;
    for (ItemId gid : groups) {
        const auto loc = locate(work, gid);
        DisplayItem* parent = groupFor(work, loc->parent);
        DisplayItem* g = &parent->children[loc->index];
        for (const DisplayItem& c : g->children)
            if (c.kind == ItemKind::Group && hasGroupNamed(*parent, c.label, {gid}))
                return Out::failure("cannot ungroup: a group named \"" + c.label +
                                    "\" already exists in the parent");
        std::vector<DisplayItem> inner = std::move(g->children);
        for (const DisplayItem& c : inner)
            kids.push_back(c.id);
        const auto pos = parent->children.begin() + static_cast<std::ptrdiff_t>(loc->index);
        const auto at = parent->children.erase(pos);
        parent->children.insert(at, std::make_move_iterator(inner.begin()),
                                std::make_move_iterator(inner.end()));
        o.removed.push_back(gid);
    }
    model = std::move(work);
    std::set<ItemId> parents;
    for (ItemId gid : groups)
        parents.insert(before.at(gid).first);
    for (ItemId k : kids) { // the former children that are still in the tree and not removed groups
        if (std::find(o.removed.begin(), o.removed.end(), k) == o.removed.end())
            o.moved.push_back(k);
    }
    for (ItemId gid : o.removed)
        parents.erase(gid);
    o.parents.assign(parents.begin(), parents.end());
    o.changed = true;
    return o;
}

Out renameItem(DisplayModel& model, ItemId id, std::string label) {
    DisplayItem* it = findIn(model.root(), id);
    if (it == nullptr)
        return unknownItem();
    label = trimmed(label);
    if (label.find_first_of("\r\n") != std::string::npos)
        return Out::failure("a name cannot contain a line break");
    switch (it->kind) {
    case ItemKind::Group: {
        if (label.empty())
            return Out::failure("a group needs a name");
        if (label.find('/') != std::string::npos)
            return Out::failure("a group name cannot contain '/'");
        const auto loc = locate(model, id);
        const DisplayItem* parent = groupFor(model, loc->parent);
        if (hasGroupNamed(*parent, label, {id}))
            return Out::failure("a group named \"" + label + "\" already exists here");
        break;
    }
    case ItemKind::VirtualBus:
        if (label.empty())
            return Out::failure("a bus needs a name");
        break;
    case ItemKind::Signal:
        if (label == it->path)
            label.clear(); // the path is the default display name
        break;
    case ItemKind::Divider:
        break;
    }
    EditOutcome o;
    if (it->label == label)
        return o;
    it->label = std::move(label);
    o.changed = true;
    o.renamed.push_back(id);
    return o;
}

Out insertItem(DisplayModel& model, ItemId parentId, std::size_t index, DisplayItem item) {
    parentId = canonicalParent(model, parentId);
    DisplayItem* parent = groupFor(model, parentId);
    if (parent == nullptr)
        return Out::failure("the destination is not a group");
    if (item.kind == ItemKind::Group && hasGroupNamed(*parent, item.label, {})) {
        const std::string base = item.label;
        int n = 2;
        while (hasGroupNamed(*parent, base + " " + std::to_string(n), {}))
            ++n;
        item.label = base + " " + std::to_string(n);
    }
    index = std::min(index, parent->children.size());
    EditOutcome o;
    o.changed = true;
    o.created.push_back(item.id);
    o.parents.push_back(parentId);
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
    return o;
}

Out removeItemsById(DisplayModel& model, const std::vector<ItemId>& ids) {
    std::vector<ItemId> sel;
    if (!resolve(model, ids, sel))
        return unknownItem();
    if (sel.empty())
        return Out::failure("nothing selected to remove");
    const Placement before = placementOf(model);
    EditOutcome o;
    std::set<ItemId> parents;
    for (ItemId id : sel)
        parents.insert(before.at(id).first);
    const std::unordered_set<ItemId> chosen(sel.begin(), sel.end());
    std::unordered_map<ItemId, DisplayItem> taken;
    extractMarked(model.root(), chosen, taken);
    o.removed = sel;
    o.parents.assign(parents.begin(), parents.end());
    o.changed = true;
    return o;
}

} // namespace lvd::wave
