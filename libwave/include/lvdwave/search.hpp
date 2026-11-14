// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/lod.hpp>
#include <lvdwave/radix.hpp>
#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// Searching the change lists of the store: next or previous edge, next or previous time a value condition
/// starts to hold, threshold crossings. Used by the wave window's navigation and its "Find value" dialog.
///
/// Common rules:
/// - Several changes at one time (delta cycles) count as one event with the value of the last of them, so a
///   glitch inside one time step is not an edge.
/// - Forward searches report the first event strictly after `from`, backward searches the last event strictly
///   before it. Without `from` (nullopt) the search covers everything: forward from the first change,
///   backward from the last one. A caller that wants wrap-around searches again without `from`.
/// - Cost: one binary search to find the start plus a walk over the changes until the event (an O(1) test
///   for most changes); threshold searches with a NumericIndex skip whole blocks of changes in O(log n) steps.

enum class SearchDirection : std::int8_t { Backward = -1, Forward = 1 };

/// Edges. Any: the value differs from the one before (the first change of a signal is an edge). Rising: the
/// value becomes 1 (or H) from anything else, including the start of the data; Falling: becomes 0 (or L).
/// Rising and Falling apply to single bits (a 1 bit signal or one bit of a vector); for wider values they
/// find nothing.
enum class EdgeKind : std::uint8_t { Any, Rising, Falling };

/// What a search walks over: a whole signal or one bit of a Logic signal (bit >= 0, counted from the LSB).
struct SearchTarget {
    const Signal* signal = nullptr;
    int bit = -1;
};

std::optional<Time> findEdge(const SearchTarget& target, std::optional<Time> from, SearchDirection dir,
                             EdgeKind kind = EdgeKind::Any);

/// A value to compare with, written in a radix as the wave window shows values.
///
/// Logic values, by radix:
/// - binary: one character per bit, 0 1 X Z U W L H. `0` also matches L and `1` also matches H; `?` and `-`
///   match any bit.
/// - octal, hex: one digit per group of 3 or 4 bits, aligned at the least significant end. A digit 0-F
///   matches a group whose bits are all known with that value; `X` a group with an unknown bit that is not all Z and
///   not all U; `Z` and `U` a group of only Z or only U (the digits the wave window prints); `?` or `-` any
///   group.
/// - unsigned, signed: a decimal number (signed: may be negative), or `X`, `Z`, `U` for a whole unknown value
///   (same rule as above, over all bits), or `?` for anything.
/// - ascii: one character per 8 bits.
/// - default: binary for one bit, hex otherwise.
/// Shorter values are extended on the left with 0 (with X, Z or U when the leading digit is one of those), as
/// the store extends values. Underscores and a 0x / 0b prefix are ignored. A value with more significant
/// digits than the signal has bits is refused unless the extra digits are 0.
///
/// Real values: a number, compared exactly. String values (enumerations): the text, case-insensitive.
class ValuePattern {
public:
    static Result<ValuePattern> parse(std::string_view text, Radix radix, SignalKind kind,
                                      std::uint32_t width);

    SignalKind kind() const noexcept { return kind_; }
    std::uint32_t width() const noexcept { return width_; }

    /// Does change `change` of the target match? For a bit target the pattern must have width 1.
    bool matches(const SearchTarget& target, std::size_t change) const;
    /// A Logic value given as bits, most significant first (as Signal::bits and busValueAt make them).
    bool matchesBits(std::string_view msbFirst) const;

private:
    enum class GroupKind : std::uint8_t { Value, Unknown, AllZ, AllU, Any };
    struct Group {
        std::uint32_t lo = 0;    ///< lowest bit index of the group
        std::uint32_t count = 0; ///< bits in the group (<= 64 for Value groups)
        GroupKind kind = GroupKind::Value;
        std::uint64_t value = 0; ///< Value groups: bit k of value is bit lo + k
        char exact = 0; ///< binary radix: a letter that must match exactly (X Z U W L H), 0 otherwise
    };
    template <class BitAt>
    bool matchGroups(const BitAt& bitAt) const;

    SignalKind kind_ = SignalKind::Logic;
    std::uint32_t width_ = 1;
    std::vector<Group> groups_;
    double real_ = 0.0;
    std::string text_; ///< String: lower case
};

/// The condition of a value search.
enum class SearchCondition : std::uint8_t {
    Equals,       ///< the value matches the pattern
    NotEquals,    ///< the value does not match
    CrossRising,  ///< numeric value goes from below the threshold to at or above it
    CrossFalling, ///< from at or above to below
    CrossEither,
};

struct ValueQuery {
    SearchCondition condition = SearchCondition::Equals;
    std::optional<ValuePattern> pattern; ///< Equals / NotEquals
    double threshold = 0.0;              ///< crossings
    bool isSigned = false;               ///< crossings on Logic vectors: two's complement
};

/// The time at which the condition starts to hold (Equals / NotEquals: the value matches now and did not
/// match at the event before; the first change counts when it matches), or the time of a crossing (it needs a
/// known value before it; changes without a numeric value, such as X bits, are skipped). `index` speeds up
/// crossings: pass the NumericIndex of the signal with the same signedness (it may cover fewer changes than
/// the signal; the rest is scanned). Returns nothing when there is no such time or the query does not fit the
/// target.
std::optional<Time> findValue(const SearchTarget& target, const ValueQuery& query, std::optional<Time> from,
                              SearchDirection dir, const NumericIndex* index = nullptr);

/// A sequence of values that is not a stored signal (a virtual bus): times in non-decreasing order and the
/// value at each change as bits, most significant first.
class BitsSequence {
public:
    virtual ~BitsSequence() = default;
    virtual std::size_t count() const = 0;
    virtual Time time(std::size_t change) const = 0;
    virtual std::string bits(std::size_t change) const = 0;
};

std::optional<Time> findEdge(const BitsSequence& seq, std::optional<Time> from, SearchDirection dir);
/// Equals / NotEquals only.
std::optional<Time> findValue(const BitsSequence& seq, const ValueQuery& query, std::optional<Time> from,
                              SearchDirection dir);

} // namespace lvd::wave
