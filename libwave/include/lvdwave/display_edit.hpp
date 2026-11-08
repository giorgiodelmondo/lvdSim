// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/display_model.hpp>
#include <lvdwave/result.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lvd::wave {

/// Structural editing of the display tree, by item id. Unlike the pointer based helpers in
/// display_model.hpp these operations never hand out pointers: ids survive every edit (and undo, which
/// restores a copy of the model), so views can keep a selection and caches keyed by id.
///
/// Rules shared by all operations:
///  - A selection is a list of ids in any order. Unknown ids make the operation fail. Duplicates are
///    dropped, and an item whose ancestor is also selected is dropped (it moves with the ancestor). The
///    remaining items are handled in display (depth first) order, which is also the order they keep
///    relative to each other.
///  - The model has nested groups, and an item can be a signal, a virtual bus, a group or a divider. The
///    per-bit rows of an expanded vector signal are not items and cannot be addressed.
///  - Group names are unique among the groups of one parent (the .do export names a group by its path).
///    An operation that would create a clash fails and changes nothing.
///  - A failed operation (Result error) leaves the model untouched. An operation with nothing to do (for
///    example moving the first item up) succeeds with changed == false.

/// The parent argument meaning the top level (the model root). The root's own id is accepted as well.
constexpr ItemId kTopLevel = 0;

/// What an operation changed, so a view can update minimally.
struct EditOutcome {
    bool changed = false;
    std::vector<ItemId> moved;   ///< requested items whose parent or position changed, display order
    std::vector<ItemId> created; ///< new items (the group made by wrapInGroup, an inserted item)
    std::vector<ItemId> removed; ///< items deleted from the tree (ungrouped groups, removed rows)
    std::vector<ItemId> renamed; ///< items whose label changed
    std::vector<ItemId> parents; ///< groups whose child list changed; kTopLevel stands for the root
};

struct ItemLocation {
    ItemId parent = kTopLevel; ///< kTopLevel for items directly under the root
    std::size_t index = 0;     ///< position among the parent's children
    int depth = 0;             ///< 0 for items directly under the root
};

DisplayItem* findItem(DisplayModel& model, ItemId id);
const DisplayItem* findItem(const DisplayModel& model, ItemId id);
std::optional<ItemLocation> locate(const DisplayModel& model, ItemId id);

/// True if `candidate` is `ancestor` itself or lies below it. O(items).
bool isSameOrDescendant(const DisplayModel& model, ItemId ancestor, ItemId candidate);

/// Ids in depth first order of every item, groups before their children (not the root).
std::vector<ItemId> allItemIds(const DisplayModel& model);

/// The selection rules above applied: known ids only, no duplicates, no descendants of selected items,
/// display order. Unknown ids are dropped here; the editing functions fail on them instead.
std::vector<ItemId> normalizeSelection(const DisplayModel& model, const std::vector<ItemId>& ids);

/// "Group" if no group of that name exists anywhere in the model, otherwise "Group 2", "Group 3", ...
std::string uniqueGroupName(const DisplayModel& model, const std::string& base = "Group");

/// Moves the items up (delta < 0) or down (delta > 0) by |delta| places among their siblings, each
/// item inside its own parent. A selected block that reaches the top or bottom of its parent stays there.
/// Items never leave their group; use moveItemsToGroup for that.
Result<EditOutcome> moveItems(DisplayModel& model, const std::vector<ItemId>& ids, int delta);

/// Moves the items to the top or the bottom of their own parent, keeping their relative order.
Result<EditOutcome> moveItemsToEdge(DisplayModel& model, const std::vector<ItemId>& ids, bool toTop);

/// Why moveItemsToGroup(model, ids, parent, ...) would fail, or an empty string if it would work. Cheap
/// enough to call while the pointer moves over a drop target (no copy of the model).
std::string checkMoveToGroup(const DisplayModel& model, const std::vector<ItemId>& ids, ItemId parent);

/// Moves the items (possibly from different parents) into group `parent` (kTopLevel for the root) so that
/// the first of them ends up at `index`, counted in the parent's child list before the move. The index is
/// clamped. Fails if the parent is not a group, is one of the items or below one of them, or if a moved
/// group would get the name of a group already in the parent.
Result<EditOutcome> moveItemsToGroup(DisplayModel& model, const std::vector<ItemId>& ids, ItemId parent,
                                     std::size_t index);

/// Wraps the items in a new expanded group placed where the first of them was. An empty label gets a
/// unique default ("Group"). Items may come from different parents; the group goes into the parent of the
/// first one. The id of the new group is in created.
Result<EditOutcome> wrapInGroup(DisplayModel& model, const std::vector<ItemId>& ids, std::string label = {});

/// Replaces every group in the selection by its children, in place. Fails if the selection holds no group
/// or a child group would clash with the name of a group in the parent.
Result<EditOutcome> ungroup(DisplayModel& model, const std::vector<ItemId>& ids);

/// Renames a group, divider, virtual bus or signal row. Groups and buses need a non-empty name, groups
/// cannot contain '/' (it separates levels in the .do export), no label may contain a line break.
/// A divider may be empty. An empty signal label means "show the path again". Leading and trailing
/// blanks are trimmed.
Result<EditOutcome> renameItem(DisplayModel& model, ItemId id, std::string label);

/// Inserts a copy of `item` as child `index` of `parent` (clamped). A group inserted this way gets
/// a unique name if its own would clash.
Result<EditOutcome> insertItem(DisplayModel& model, ItemId parent, std::size_t index, DisplayItem item);

/// Deletes the items and everything below them.
Result<EditOutcome> removeItemsById(DisplayModel& model, const std::vector<ItemId>& ids);

} // namespace lvd::wave
