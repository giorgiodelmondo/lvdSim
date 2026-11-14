// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/radix.hpp>
#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// Identity of a DisplayItem. Unique within the process, assigned when the item is created and kept by
/// copies and moves, so a row keeps its id when the tree is reordered, grouped or restored by undo. Ids are
/// not part of the saved formats: loading a layout creates new items. 0 is never an id.
using ItemId = std::uint64_t;
ItemId nextItemId() noexcept;

/// Expression: a row computed from other signals (lvdwave/expression.hpp); `expr` holds the text and `label`
/// the name. Compare: the differences between two signals (lvdwave/compare.hpp); `members` holds the
/// reference and the test path, `tolerance` the time tolerance.
enum class ItemKind : std::uint8_t { Signal, Group, Divider, VirtualBus, Expression, Compare };
enum class Interpolation : std::uint8_t { Step, Linear };

/// Oscilloscope style rendering of a numeric signal.
struct AnalogSettings {
    bool enabled = false;
    bool autoRange = true;
    double min = 0.0; ///< Used when autoRange is false.
    double max = 1.0;
    Interpolation interpolation = Interpolation::Step;
    int heightRows = 4; ///< Height in multiples of a normal row.
    bool isSigned = false;
};

/// The value interval an analog row maps onto its height (min at the bottom edge, max at the top edge).
struct AnalogRange {
    double min = 0.0;
    double max = 1.0;
};

/// Why a manual range is refused ("" when it is usable): both values must be finite and min < max.
std::string analogRangeError(double min, double max);

/// The range an analog row is drawn with. Manual: the settings' min and max. Auto: the extent of the data
/// (`dataRange`: the minimum and maximum of every value of the signal; without data there is no range). An
/// empty or reversed range (one constant value, or a bad manual range) is widened to [v - 0.5, v + 0.5]
/// around its lower edge. It never depends on the zoom or the time window, so a row has one range for all of
/// its tiles.
std::optional<AnalogRange> resolveAnalogRange(const AnalogSettings& settings,
                                              std::optional<AnalogRange> dataRange);

/// Where `value` falls on a row of the range: 0 at min, 1 at max, clamped, so values outside the range are
/// drawn on the nearest edge of the row. NaN gives 0.
double analogFraction(const AnalogRange& range, double value) noexcept;

struct DisplayItem;

/// Row heights in pixels: the default of a row, and the limits of a height set by the user.
inline constexpr int kDefaultRowHeightPx = 22;
inline constexpr int kMinRowHeightPx = 12;
inline constexpr int kMaxRowHeightPx = 800;

/// A requested row height brought into [kMinRowHeightPx, kMaxRowHeightPx]; 0 or less stays 0 (the default).
int clampRowHeight(int px) noexcept;

/// The height a row is drawn with: the item's own height if set, else the default (analog traces:
/// analog.heightRows times the default). `analogTrace` tells whether the row is drawn as an analog trace.
int rowHeightPx(const DisplayItem& item, bool analogTrace) noexcept;

/// True if the numeric (analog) view of a Logic row reads its bits as two's complement: the analog setting
/// `signed`, or the signed radix. Signals and virtual buses follow the same rule, so a bus draws like the
/// vector of the same bits.
bool analogSigned(const DisplayItem& item) noexcept;

/// One member of a virtual bus: a whole signal, or a single bit of it (bit counted from the least
/// significant end, -1 for the whole signal).
struct BusMember {
    std::string path;
    int bit = -1;

    BusMember() = default;
    BusMember(std::string p, int b = -1)
        : path(std::move(p))
        , bit(b) {} // NOLINT: implicit by design
    BusMember(const char* p, int b = -1)
        : path(p)
        , bit(b) {} // NOLINT: implicit by design
    bool operator==(const BusMember& o) const { return path == o.path && bit == o.bit; }
};

/// One entry of the wave window. Signals are referenced by hierarchical path, not id, so a saved
/// layout survives reloading a different run of the same design.
struct DisplayItem {
    ItemId id = nextItemId(); ///< Stable identity, see ItemId.
    ItemKind kind = ItemKind::Signal;
    std::string label; ///< Display name; empty means use the path.
    std::string path;  ///< Signal items.
    std::vector<BusMember>
        members;        ///< VirtualBus items: members, most significant first. Compare: ref, test.
    std::string expr;   ///< Expression items: the expression text.
    Time tolerance = 0; ///< Compare items: differences this short or shorter are ignored.
    Radix radix = Radix::Default;
    AnalogSettings analog;
    bool expanded = true;  ///< Group items.
    bool showBits = false; ///< Signal items wider than one bit: show one row per bit.
    /// Height of the row in pixels; 0 is the default (kDefaultRowHeightPx, analog traces analog.heightRows
    /// rows). Rows of single bits under an expanded vector keep the default height.
    int heightPx = 0;
    std::optional<std::uint32_t> color; ///< 0xRRGGBB trace colour override.
    std::vector<DisplayItem> children;  ///< Group items.

    std::string displayName() const;
};

/// A visible line of the wave window after flattening collapsed groups.
struct DisplayRow {
    const DisplayItem* item;
    int depth;
};

/// Ordered tree of groups, dividers, signals and virtual buses.
class DisplayModel {
public:
    DisplayModel();

    DisplayItem& root() noexcept { return root_; }
    const DisplayItem& root() const noexcept { return root_; }

    static DisplayItem makeSignal(std::string path, Radix radix = Radix::Default);
    static DisplayItem makeGroup(std::string label, bool expanded = true);
    static DisplayItem makeDivider(std::string label = {});
    static DisplayItem makeBus(std::string label, std::vector<BusMember> members, Radix radix = Radix::Hex);
    static DisplayItem makeExpression(std::string label, std::string expr, Radix radix = Radix::Default);
    /// A compare row of two prefixed dotted paths (reference, test); red by default.
    static DisplayItem makeCompare(std::string label, std::string refPath, std::string testPath,
                                   Time tolerance = 0);

    /// Rows in display order, skipping the children of collapsed groups. O(items).
    std::vector<DisplayRow> visibleRows() const;

    /// Text form, round-trips through parse(). Line based, see docs/display-format.md.
    std::string serialize() const;
    static Result<DisplayModel> parse(std::string_view text);

private:
    DisplayItem root_;
};

/// Editing operations on a DisplayModel. Items are identified by pointer; every operation that
/// changes the tree invalidates all DisplayItem pointers obtained earlier.

/// Parent group of item and the index of item in its children, or nullptr if not in the tree.
DisplayItem* findParent(DisplayModel& model, const DisplayItem* item, std::size_t* indexOut = nullptr);

/// Move the items into a new group placed where the first (in display order) of them was.
/// All items must share one parent. Returns false and changes nothing otherwise.
bool groupItems(DisplayModel& model, const std::vector<const DisplayItem*>& items, std::string label);

/// Remove the items (and everything below them). Returns the number removed.
std::size_t removeItems(DisplayModel& model, const std::vector<const DisplayItem*>& items);

/// Add a virtual bus built from the Signal items in the selection (display order, first is the most
/// significant), placed right after the last selected item. Returns false if fewer than two Signal
/// items are selected or they do not share a parent.
bool busFromSignals(DisplayModel& model, const std::vector<const DisplayItem*>& items, std::string label,
                    Radix radix = Radix::Hex);

/// Insert item right after `after` (same parent). If `after` is null or not in the tree the item is
/// appended to the root. Invalidates all DisplayItem pointers.
void insertAfter(DisplayModel& model, const DisplayItem* after, DisplayItem item);

/// Value of a virtual bus at time t: member values concatenated most significant first, as a bit
/// string. Members that are missing from the store or have no value yet contribute their width of
/// 'U' (width 1 if the member is unknown or a single bit). Real and String members are skipped.
/// O(sum of member widths + members * log changes).
std::string busValueAt(const WaveStore& store, const std::vector<BusMember>& members, Time t);

} // namespace lvd::wave
